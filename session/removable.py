#!/usr/bin/env python3
"""Mount USB sticks and memory cards as they come, for the XP shell.

  removable.py PREFIX --wine WINE

The session (session/supervisor.py, setting "removable") runs it. UDisks2 does
the mounting, at /media/USER/LABEL, so Linux programs see the files too, and
Wine's mountmgr.sys, which follows UDisks2, makes the removable disk of it
(Wine patches 0168 to 0170). The rest of XP's hotplug behaviour is Windows
programs' (xptray.exe, autoplay.exe), which follow WM_DEVICECHANGE.

It follows "udisksctl monitor", sleeping in read() while nothing happens, and
after each change to a block device reads "udisksctl dump" once. A file system
of a removable drive is mounted once, when it appears: one that was ejected and
stayed (a card in its reader) is left unmounted until it goes and comes back.
Nothing but mount is ever asked of a stick.

A file system that cannot be mounted gets XP's "The disk in drive E: is not
formatted." message (xptray.exe --not-formatted).
"""
import argparse
import os
import re
import select
import subprocess
import sys
import time

BLOCKS = '/org/freedesktop/UDisks2/block_devices/'
BLOCK = 'org.freedesktop.UDisks2.Block'
FILESYSTEM = 'org.freedesktop.UDisks2.Filesystem'
DRIVE = 'org.freedesktop.UDisks2.Drive'
QUIET = 0.3      # seconds without another change before the devices are read
LATEST = 2.0     # and at the latest after the first
RESTART = 60     # "udisksctl monitor" is started again at most this often


def log(text):
    print(text, flush=True)


def parse_dump(text):
    """{object path: {interface: {property: text}}} of "udisksctl dump".
    A property over several lines (a list) is joined with newlines."""
    objects, interfaces, properties, name = {}, None, None, None
    for line in text.splitlines():
        indent = len(line) - len(line.lstrip(' '))
        stripped = line.strip()
        if not stripped:
            name = None
        elif indent == 0 and stripped.startswith('/') and stripped.endswith(':'):
            interfaces = objects.setdefault(stripped[:-1], {})
            properties = name = None
        elif indent == 2 and stripped.endswith(':') and interfaces is not None:
            properties = interfaces.setdefault(stripped[:-1], {})
            name = None
        elif indent == 4 and properties is not None and ':' in stripped:
            name, _, value = stripped.partition(':')
            properties[name] = value.strip()
        elif indent > 4 and name:
            properties[name] += '\n' + stripped
    return objects


def removable(objects):
    """{object path: (device, identity, mounted)} of the file systems on
    removable drives, the ones to mount: sticks, cards and USB CD drives."""
    found = {}
    for path, interfaces in objects.items():
        block, filesystem = interfaces.get(BLOCK), interfaces.get(FILESYSTEM)
        if not path.startswith(BLOCKS) or block is None or filesystem is None:
            continue
        drive = objects.get(block.get('Drive', '').strip("'"), {}).get(DRIVE, {})
        if drive.get('Removable') != 'true':
            continue
        if block.get('IdUsage') != 'filesystem' or block.get('Size', '0') == '0':
            continue
        if block.get('HintIgnore') == 'true' or block.get('HintSystem') == 'true':
            continue
        identity = (block.get('IdUUID', ''), block.get('Size'))
        found[path] = (block['Device'], identity, bool(filesystem.get('MountPoints')))
    return found


def concerns_devices(line):
    """Whether a line of "udisksctl monitor" tells of a change to a block device."""
    # "12:34:30.376: Added /org/.../block_devices/sdb"
    # "12:34:31.399: /org/.../block_devices/sdb1: org...Filesystem: Properties Changed"
    return bool(re.match(r'\d\d:\d\d:\d\d\.\d+: ', line)) and BLOCKS in line


def drive_letter(prefix, device, wait=3.0):
    """The Windows drive of DEVICE, as mountmgr links it in dosdevices, or None."""
    deadline = time.monotonic() + wait
    while True:
        for letter in 'defghijklmnopqrstuvwxy':
            try:
                if os.readlink(f'{prefix}/dosdevices/{letter}::') == device:
                    return letter.upper() + ':'
            except OSError:
                continue
        if time.monotonic() >= deadline:
            return None
        time.sleep(0.2)


class Mounter:
    def __init__(self, prefix, wine, run=subprocess.run):
        self.prefix, self.wine, self.run = prefix, wine, run
        self.handled = {}  # object path: identity of the file system mounted or tried

    def udisksctl(self, *arguments):
        env = dict(os.environ, LC_ALL='C.UTF-8')
        return self.run(['udisksctl', *arguments], env=env, stdin=subprocess.DEVNULL, capture_output=True,
                        text=True, errors='replace', timeout=60)

    def reconcile(self):
        """Mount the removable file systems that have appeared since the last time."""
        done = self.udisksctl('dump')
        if done.returncode:
            log(f'udisksctl dump failed: {done.stderr.strip()}')
            return
        found = removable(parse_dump(done.stdout))
        self.handled = {path: identity for path, identity in self.handled.items()
                        if path in found and found[path][1] == identity}
        for path, (device, identity, mounted) in sorted(found.items()):
            if path in self.handled:
                continue
            self.handled[path] = identity
            if not mounted:
                self.mount(device)

    def mount(self, device):
        started = time.monotonic()
        try:
            done = self.udisksctl('mount', '--no-user-interaction', '-b', device)
            error = done.stderr.strip() if done.returncode else None
        except subprocess.TimeoutExpired:
            error = 'udisksctl did not answer'
        if error is None:
            log(f'{done.stdout.strip()} ({time.monotonic() - started:.1f} s)')
        elif 'AlreadyMounted' not in error:
            log(f'{device} could not be mounted: {error}')
            self.tell(device)

    def tell(self, device):
        letter = drive_letter(self.prefix, device)
        if letter:
            subprocess.Popen([self.wine, r'C:\xp\bin\xptray.exe', '--not-formatted', letter],
                             stdin=subprocess.DEVNULL)


def follow(mounter):
    """Reconcile at the start and after every change, until the monitor ends."""
    # Its output must come line by line, not when a buffer fills.
    monitor = subprocess.Popen(['stdbuf', '-oL', 'udisksctl', 'monitor'], stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, env=dict(os.environ, LC_ALL='C.UTF-8'))
    output = monitor.stdout.fileno()
    pending = b''
    first = None
    try:
        # The monitor says it watches, and then no change can be missed.
        select.select([output], [], [], 5)
        mounter.reconcile()
        while True:
            timeout = None if first is None else max(0, min(QUIET, first + LATEST - time.monotonic()))
            if select.select([output], [], [], timeout)[0]:
                data = os.read(output, 65536)
                if not data:
                    return
                *lines, pending = (pending + data).split(b'\n')
                if first is None and any(concerns_devices(line.decode('utf-8', 'replace')) for line in lines):
                    first = time.monotonic()
                if first is None or time.monotonic() < first + LATEST:
                    continue
            if first is not None:
                first = None
                mounter.reconcile()
    finally:
        monitor.kill()
        monitor.wait()
        monitor.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('prefix')
    parser.add_argument('--wine', required=True)
    args = parser.parse_args()
    mounter = Mounter(args.prefix, args.wine)
    while True:
        started = time.monotonic()
        try:
            follow(mounter)
        except FileNotFoundError as error:
            sys.exit(f'{error.filename} is missing (the Debian package udisks2)')
        except (OSError, subprocess.TimeoutExpired) as error:
            log(f'{type(error).__name__}: {error}')
        log('udisksctl monitor ended')
        time.sleep(max(0, started + RESTART - time.monotonic()))


if __name__ == '__main__':
    main()
