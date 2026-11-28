#!/usr/bin/env python3
"""A fake UDisks2 for the bench: USB sticks and memory cards without hardware.

  tools/bench/bench.sh usb NAME plug STICK [options]   plug a stick in
  tools/bench/bench.sh usb NAME yank STICK             pull it out, mounted or not
  tools/bench/bench.sh usb NAME media STICK out|in     take the card out of its reader, or put it back
  tools/bench/bench.sh usb NAME busy STICK on|off      make unmounting it fail
  tools/bench/bench.sh usb NAME list

Options of plug: --dir DIR (its files, a folder in the container), --label TEXT, --fs vfat|exfat|ntfs|ext4|none,
--parts N (partitions, each with a file system), --whole (no partition table), --bus usb|sdio, --vendor TEXT,
--model TEXT, --serial TEXT, --no-power-off (a drive that cannot be powered off), --iso9660 (a hybrid live
stick).

bench.sh runs "udisks-mock.py serve" as root on the container's system bus, where it owns
org.freedesktop.UDisks2: Wine's mountmgr.sys, session/removable.py and udisksctl talk to it as to the real
daemon. The block devices are the host's loop devices bound as /dev/sdb, /dev/sdb1 and so on (bench.sh up):
they can be seen, not opened, as a stick's device on the EeePC. Mounting makes /media/tester/LABEL a link to the
stick's folder; unmounting fails while a process has a file of the folder open or is inside it.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

from gi.repository import Gio, GLib

NAME = 'org.freedesktop.UDisks2'
ROOT = '/org/freedesktop/UDisks2'
BLOCKS = ROOT + '/block_devices/'
DRIVES = ROOT + '/drives/'
CONTROL = 'org.lunaticos.UDisksMock'
MEDIA = '/media/tester'

# Interface: properties (name: signature) and methods (name: (in, out)).
INTERFACES = {
    'org.freedesktop.UDisks2.Manager': (
        {'Version': 's', 'SupportedFilesystems': 'as'}, {}),
    'org.freedesktop.UDisks2.Block': (
        {'Device': 'ay', 'PreferredDevice': 'ay', 'Symlinks': 'aay', 'DeviceNumber': 't', 'Id': 's',
         'Size': 't', 'ReadOnly': 'b', 'Drive': 'o', 'MDRaid': 'o', 'MDRaidMember': 'o', 'IdUsage': 's',
         'IdType': 's', 'IdVersion': 's', 'IdLabel': 's', 'IdUUID': 's', 'Configuration': 'a(sa{sv})',
         'CryptoBackingDevice': 'o', 'HintPartitionable': 'b', 'HintSystem': 'b', 'HintIgnore': 'b',
         'HintAuto': 'b', 'HintName': 's', 'HintIconName': 's', 'HintSymbolicIconName': 's',
         'UserspaceMountOptions': 'as'}, {}),
    'org.freedesktop.UDisks2.Filesystem': (
        {'MountPoints': 'aay', 'Size': 't'},
        {'Mount': ('a{sv}', 's'), 'Unmount': ('a{sv}', '')}),
    'org.freedesktop.UDisks2.PartitionTable': (
        {'Partitions': 'ao', 'Type': 's'}, {}),
    'org.freedesktop.UDisks2.Partition': (
        {'Number': 'u', 'Type': 's', 'Offset': 't', 'Size': 't', 'Flags': 't', 'Name': 's', 'UUID': 's',
         'Table': 'o', 'IsContainer': 'b', 'IsContained': 'b'}, {}),
    'org.freedesktop.UDisks2.Drive': (
        {'Vendor': 's', 'Model': 's', 'Revision': 's', 'Serial': 's', 'WWN': 's', 'Id': 's',
         'Configuration': 'a{sv}', 'Media': 's', 'MediaCompatibility': 'as', 'MediaRemovable': 'b',
         'MediaAvailable': 'b', 'MediaChangeDetected': 'b', 'Size': 't', 'TimeDetected': 't',
         'TimeMediaDetected': 't', 'Optical': 'b', 'OpticalBlank': 'b', 'OpticalNumTracks': 'u',
         'OpticalNumAudioTracks': 'u', 'OpticalNumDataTracks': 'u', 'OpticalNumSessions': 'u',
         'RotationRate': 'i', 'ConnectionBus': 's', 'Seat': 's', 'Removable': 'b', 'Ejectable': 'b',
         'SortKey': 's', 'CanPowerOff': 'b', 'SiblingId': 's'},
        {'PowerOff': ('a{sv}', ''), 'Eject': ('a{sv}', '')}),
}

DEFAULTS = {'s': '', 'b': False, 't': 0, 'u': 0, 'i': 0, 'o': '/', 'ay': b'\0', 'aay': [], 'as': [], 'ao': [],
            'a{sv}': {}, 'a(sa{sv})': []}

OBJECT_MANAGER = 'org.freedesktop.DBus.ObjectManager'


def interface_xml(name, properties, methods):
    parts = [f'<interface name="{name}">']
    parts += [f'<property name="{p}" type="{t}" access="read"/>' for p, t in properties.items()]
    for method, (takes, gives) in methods.items():
        parts.append(f'<method name="{method}">')
        parts.append(f'<arg type="{takes}" direction="in"/>')
        if gives:
            parts.append(f'<arg type="{gives}" direction="out"/>')
        parts.append('</method>')
    parts.append('</interface>')
    return ''.join(parts)


NODE = Gio.DBusNodeInfo.new_for_xml(
    '<node>' + ''.join(interface_xml(n, *i) for n, i in INTERFACES.items()) +
    f'<interface name="{OBJECT_MANAGER}">'
    '<method name="GetManagedObjects"><arg type="a{oa{sa{sv}}}" direction="out"/></method>'
    '<signal name="InterfacesAdded"><arg type="o"/><arg type="a{sa{sv}}"/></signal>'
    '<signal name="InterfacesRemoved"><arg type="o"/><arg type="as"/></signal></interface>'
    f'<interface name="{CONTROL}"><method name="Run"><arg type="s" direction="in"/>'
    '<arg type="s" direction="out"/></method></interface></node>')


class Failed(Exception):
    def __init__(self, text, kind='Failed'):
        super().__init__(text)
        self.kind = f'{NAME}.Error.{kind}'


class Mock:
    def __init__(self, bus):
        self.bus = bus
        self.objects = {}   # path: {interface: {property: value}}
        self.exported = {}  # path: [registration IDs]
        self.sticks = {}    # name: its plug options and object paths
        bus.register_object(ROOT, NODE.lookup_interface(OBJECT_MANAGER), self.call, None, None)
        bus.register_object(ROOT, NODE.lookup_interface(CONTROL), self.call, None, None)
        self.add(ROOT + '/Manager', {'org.freedesktop.UDisks2.Manager':
                                     {'Version': '2.10.1', 'SupportedFilesystems': ['vfat', 'exfat', 'ntfs', 'ext4']}})

    # ---- D-Bus ------------------------------------------------------------

    def variants(self, interface, values):
        signatures = INTERFACES[interface][0]
        return {p: GLib.Variant(t, values.get(p, DEFAULTS[t])) for p, t in signatures.items()}

    def add(self, path, interfaces):
        self.objects[path] = interfaces
        self.exported[path] = [
            self.bus.register_object(path, NODE.lookup_interface(name), self.call, self.get, None)
            for name in interfaces]
        self.emit(ROOT, OBJECT_MANAGER, 'InterfacesAdded', GLib.Variant(
            '(oa{sa{sv}})', (path, {n: self.variants(n, v) for n, v in interfaces.items()})))

    def remove(self, path):
        if path not in self.objects:
            return
        interfaces = list(self.objects.pop(path))
        for registration in self.exported.pop(path):
            self.bus.unregister_object(registration)
        self.emit(ROOT, OBJECT_MANAGER, 'InterfacesRemoved', GLib.Variant('(oas)', (path, interfaces)))

    def change(self, path, interface, **values):
        self.objects[path][interface].update(values)
        signatures = INTERFACES[interface][0]
        changed = {p: GLib.Variant(signatures[p], v) for p, v in values.items()}
        self.emit(path, 'org.freedesktop.DBus.Properties', 'PropertiesChanged',
                  GLib.Variant('(sa{sv}as)', (interface, changed, [])))

    def emit(self, path, interface, signal, body):
        self.bus.emit_signal(None, path, interface, signal, body)

    def get(self, bus, sender, path, interface, name):
        return self.variants(interface, self.objects[path][interface])[name]

    def call(self, bus, sender, path, interface, method, parameters, invocation):
        try:
            if method == 'GetManagedObjects':
                result = GLib.Variant('(a{oa{sa{sv}}})', ({
                    p: {n: self.variants(n, v) for n, v in i.items()} for p, i in self.objects.items()},))
            elif method == 'Run':
                result = GLib.Variant('(s)', (self.run(json.loads(parameters.unpack()[0])),))
            elif method == 'Mount':
                result = GLib.Variant('(s)', (self.mount(path),))
            elif method == 'Unmount':
                self.unmount(path)
                result = None
            elif method == 'PowerOff':
                self.power_off(path)
                result = None
            else:
                raise Failed(f'{method} is not in the mock', 'NotSupported')
            print(f'{method} {path}', flush=True)
            invocation.return_value(result)
        except Failed as error:
            print(f'{method} {path}: {error}', flush=True)
            invocation.return_dbus_error(error.kind, str(error))

    # ---- what udisksd does ------------------------------------------------

    def stick_of(self, path):
        for stick in self.sticks.values():
            if path == stick['drive'] or path in stick['blocks']:
                return stick
        raise Failed('No such device')

    def mount(self, path):
        block = self.objects[path]
        filesystem = block['org.freedesktop.UDisks2.Filesystem']
        device = block['org.freedesktop.UDisks2.Block']['Device'][:-1].decode()
        if filesystem['MountPoints']:
            raise Failed(f'Device {device} is already mounted', 'AlreadyMounted')
        stick = self.stick_of(path)
        if stick['options'].get('broken'):
            raise Failed(f'Error mounting {device}: wrong fs type, bad option, bad superblock')
        label = block['org.freedesktop.UDisks2.Block']['IdLabel'] or \
            block['org.freedesktop.UDisks2.Block']['IdUUID']
        os.makedirs(MEDIA, exist_ok=True)
        point, n = f'{MEDIA}/{label}', 1
        while os.path.lexists(point):
            point, n = f'{MEDIA}/{label}{n}', n + 1
        os.symlink(stick['blocks'][path], point)
        self.change(path, 'org.freedesktop.UDisks2.Filesystem', MountPoints=[point.encode() + b'\0'])
        return point

    def users(self, folder):
        """A process that is in FOLDER or has a file of it open, or None."""
        folder = os.path.realpath(folder)
        # As the session's user: the root of a rootless container may not look.
        links = subprocess.run(
            ['setpriv', '--reuid=tester', '--regid=tester', '--clear-groups', 'sh', '-c',
             "find /proc/[0-9]*/cwd /proc/[0-9]*/fd -type l -printf '%p\\t%l\\n' 2>/dev/null"],
            capture_output=True, text=True, errors='replace', check=False).stdout
        for line in links.splitlines():
            link, _, target = line.partition('\t')
            if target == folder or target.startswith(folder + '/'):
                return f'{link.split("/")[2]} ({target})'
        return None

    def unmount(self, path):
        filesystem = self.objects[path]['org.freedesktop.UDisks2.Filesystem']
        if not filesystem['MountPoints']:
            raise Failed('Not mounted', 'NotMounted')
        point = filesystem['MountPoints'][0][:-1].decode()
        stick = self.stick_of(path)
        user = self.users(point)
        if stick.get('busy') or user:
            raise Failed(f'Error unmounting {point}: target is busy {user or ""}', 'DeviceBusy')
        os.unlink(point)
        self.change(path, 'org.freedesktop.UDisks2.Filesystem', MountPoints=[])

    def power_off(self, path):
        stick = self.stick_of(path)
        if not stick['can_power_off']:
            raise Failed('The drive cannot be powered off', 'NotSupported')
        for block in stick['blocks']:
            if self.objects.get(block, {}).get('org.freedesktop.UDisks2.Filesystem', {}).get('MountPoints'):
                raise Failed('The drive is in use', 'DeviceBusy')
        self.yank(stick['name'])

    # ---- the control commands ---------------------------------------------

    def run(self, command):
        verb = command.pop('verb')
        if verb == 'list':
            return '\n'.join(f'{name}: ' + ' '.join(sorted(p.rsplit('/', 1)[1] for p in s['blocks']))
                             for name, s in self.sticks.items()) or 'nothing plugged in'
        name = command.pop('name')
        if verb == 'plug':
            return self.plug(name, command)
        if name not in self.sticks:
            raise Failed(f'{name} is not plugged in')
        if verb == 'yank':
            self.yank(name)
        elif verb == 'busy':
            self.sticks[name]['busy'] = command['state'] == 'on'
        elif verb == 'media':
            self.media(name, command['state'] == 'in')
        return 'ok'

    def free_disk(self):
        used = {p.rsplit('/', 1)[1][:3] for s in self.sticks.values() for p in s['blocks']}
        for node in sorted(glob.glob('/dev/sd[b-z]')):
            if node[5:] not in used:
                return node[5:]
        raise Failed('No free /dev/sdX node: the container was made without them (bench.sh up)')

    def plug(self, name, options):
        if name in self.sticks:
            raise Failed(f'{name} is plugged in already')
        disk = self.free_disk()
        bus = options.get('bus') or 'usb'
        vendor, model = options.get('vendor') or 'Generic', options.get('model') or 'Flash Disk'
        serial = options.get('serial') or f'{abs(hash(name)) % 10 ** 8:08d}'
        drive = DRIVES + ''.join(c if c.isalnum() else '_' for c in f'{vendor}_{model}_{serial}')
        stick = {'name': name, 'drive': drive, 'blocks': {}, 'options': options,
                 'can_power_off': not options.get('no_power_off')}
        self.sticks[name] = stick
        self.add(drive, {'org.freedesktop.UDisks2.Drive': {
            'Vendor': vendor, 'Model': model, 'Serial': serial,
            'Id': f'{vendor}-{model}-{serial}'.replace(' ', '-'), 'ConnectionBus': bus, 'Removable': True,
            'MediaRemovable': True, 'MediaAvailable': True, 'CanPowerOff': stick['can_power_off'],
            'Ejectable': True, 'Size': 8 << 30, 'Seat': 'seat0'}})
        try:
            self.media(name, True, disk)
        except Failed:
            self.yank(name)
            raise
        return f'{name} is /dev/{disk}'

    def media(self, name, present, disk=None):
        """The block devices of a stick's medium: appearing, or going."""
        stick = self.sticks[name]
        if not present:
            for path in sorted(stick['blocks'], reverse=True):
                self.unlink_mount(path)
                self.remove(path)
            stick['disk'] = next(iter(sorted(stick['blocks']))).rsplit('/', 1)[1][:3] if stick['blocks'] else None
            stick['blocks'] = {}
            return
        if stick['blocks']:
            return
        options = stick['options']
        disk = disk or stick['disk']
        filesystem = options.get('fs') or 'vfat'
        whole = options.get('whole')
        parts = 1 if whole else options.get('parts') or 1
        label = options.get('label')
        label = name.upper() if label is None else label
        size = 8 << 30
        names = [disk] if whole else [f'{disk}{n}' for n in range(1, parts + 1)]
        if not whole:
            table = {'org.freedesktop.UDisks2.Block': self.block(disk, stick, size, ''),
                     'org.freedesktop.UDisks2.PartitionTable': {
                         'Type': 'dos', 'Partitions': [BLOCKS + n for n in names]}}
            if options.get('iso9660'):
                table['org.freedesktop.UDisks2.Block'].update(IdUsage='filesystem', IdType='iso9660', IdLabel=label)
            stick['blocks'][BLOCKS + disk] = None
            self.add(BLOCKS + disk, table)
        for number, node in enumerate(names, 1):
            path = BLOCKS + node
            if not os.path.exists('/dev/' + node):
                raise Failed(f'No node /dev/{node} in this container')
            folder = options.get('dir') or f'/tmp/udisks-mock/{name}'
            if parts > 1:
                folder = f'{folder}.{number}'
            if not os.path.isdir(folder):
                os.makedirs(folder)
                os.chmod(folder, 0o777)
            interfaces = {'org.freedesktop.UDisks2.Block': self.block(node, stick, size // parts, filesystem)}
            if filesystem != 'none':
                this = label if parts == 1 else f'{label}{number}'
                uuid = f'{abs(hash((name, number))) & 0xffffffff:08X}'
                interfaces['org.freedesktop.UDisks2.Block'].update(
                    IdLabel=this, IdUUID=f'{uuid[:4]}-{uuid[4:]}')
                interfaces['org.freedesktop.UDisks2.Filesystem'] = {'MountPoints': [], 'Size': size // parts}
            if not whole:
                interfaces['org.freedesktop.UDisks2.Partition'] = {'Number': number, 'Table': BLOCKS + disk,
                                                                   'Size': size // parts}
            stick['blocks'][path] = folder
            self.add(path, interfaces)

    def block(self, node, stick, size, filesystem):
        device = f'/dev/{node}'.encode() + b'\0'
        return {'Device': device, 'PreferredDevice': device, 'DeviceNumber': os.stat(f'/dev/{node}').st_rdev,
                'Drive': stick['drive'], 'Size': size, 'HintPartitionable': True, 'HintAuto': True,
                'IdUsage': 'filesystem' if filesystem not in ('', 'none') else '',
                'IdType': filesystem if filesystem not in ('', 'none') else ''}

    def unlink_mount(self, path):
        points = self.objects.get(path, {}).get('org.freedesktop.UDisks2.Filesystem', {}).get('MountPoints')
        for point in points or []:
            try:
                os.unlink(point[:-1].decode())
            except OSError:
                pass

    def yank(self, name):
        stick = self.sticks[name]
        # The real daemon says the device is gone, and tidies its mount point up afterwards.
        points = []
        for path in sorted(stick['blocks'], reverse=True):
            points += self.objects.get(path, {}).get('org.freedesktop.UDisks2.Filesystem', {}).get('MountPoints', [])
            self.remove(path)
        for point in points:
            try:
                os.unlink(point[:-1].decode())
            except OSError:
                pass
        self.remove(stick['drive'])
        del self.sticks[name]


def serve():
    shutil.rmtree(MEDIA, ignore_errors=True)
    bus = Gio.bus_get_sync(Gio.BusType.SYSTEM)
    Mock(bus)
    loop = GLib.MainLoop()
    Gio.bus_own_name_on_connection(bus, NAME, Gio.BusNameOwnerFlags.NONE,
                                   lambda *a: print('serving', flush=True),
                                   lambda *a: (print(f'{NAME} is taken', file=sys.stderr), loop.quit()))
    loop.run()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    verbs = parser.add_subparsers(dest='verb', required=True)
    verbs.add_parser('serve')
    verbs.add_parser('list')
    plug = verbs.add_parser('plug')
    plug.add_argument('name')
    for option in ('dir', 'label', 'fs', 'bus', 'vendor', 'model', 'serial'):
        plug.add_argument('--' + option)
    plug.add_argument('--parts', type=int)
    for flag in ('whole', 'no-power-off', 'iso9660', 'broken'):
        plug.add_argument('--' + flag, action='store_true')
    verbs.add_parser('yank').add_argument('name')
    for verb, states in (('media', ('in', 'out')), ('busy', ('on', 'off'))):
        sub = verbs.add_parser(verb)
        sub.add_argument('name')
        sub.add_argument('state', choices=states)
    args = parser.parse_args()
    if args.verb == 'serve':
        serve()
        return
    bus = Gio.bus_get_sync(Gio.BusType.SYSTEM)
    try:
        answer = bus.call_sync(NAME, ROOT, CONTROL, 'Run', GLib.Variant('(s)', (json.dumps(vars(args)),)),
                               GLib.VariantType('(s)'), Gio.DBusCallFlags.NONE, 10000, None)
    except GLib.Error as error:
        sys.exit(error.message)
    print(answer.unpack()[0])


if __name__ == '__main__':
    main()
