#!/usr/bin/env python3
"""session/removable.py with a stand-in for udisksctl and the text udisksctl
prints (taken from udisks 2.10 with tools/bench/udisks-mock.py, and shortened).
No Wine, no UDisks2, no devices.

    tests/removable.py
"""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('removable_helper', ROOT / 'session/removable.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


def block(node, drive, usage='filesystem', size=8589934592, uuid='266E-80E4', ignore='false', system='false',
          mounts=None):
    text = f'''/org/freedesktop/UDisks2/block_devices/{node}:
  org.freedesktop.UDisks2.Block:
    Configuration:              []
    Device:                     /dev/{node}
    Drive:                      '/org/freedesktop/UDisks2/drives/{drive}'
    HintIgnore:                 {ignore}
    HintSystem:                 {system}
    IdLabel:                    HOLIDAY
    IdType:                     {'vfat' if usage == 'filesystem' else ''}
    IdUUID:                     {uuid}
    IdUsage:                    {usage}
    Size:                       {size}
    Symlinks:                   /dev/disk/by-diskseq/9
                                /dev/disk/by-id/usb-Generic_Flash_Disk-0:0
'''
    if usage == 'filesystem':
        lines = (mounts or [''])
        text += f'''  org.freedesktop.UDisks2.Filesystem:
    MountPoints:        {lines[0]}
''' + ''.join(f'                        {m}\n' for m in lines[1:]) + f'''    Size:               {size}
'''
    return text + '\n'


def drive(name, removable='true', bus='usb'):
    return f'''/org/freedesktop/UDisks2/drives/{name}:
  org.freedesktop.UDisks2.Drive:
    ConnectionBus:              {bus}
    MediaCompatibility:
    Removable:                  {removable}

'''


# The EeePC's own disk, a stick with a partition table, and an empty card reader.
DUMP = (block('sda1', 'Disk', system='true', mounts=['/boot']) + drive('Disk', 'false', '') +
        block('sdb', 'Stick', usage='', uuid='') + block('sdb1', 'Stick') +
        block('sdc', 'Reader', usage='', size=0, uuid='') + drive('Stick') + drive('Reader'))


class Parsing(unittest.TestCase):
    def test_dump(self):
        objects = helper.parse_dump(DUMP)
        block = objects['/org/freedesktop/UDisks2/block_devices/sdb1']['org.freedesktop.UDisks2.Block']
        self.assertEqual(block['Device'], '/dev/sdb1')
        self.assertEqual(block['Symlinks'].split('\n')[1], '/dev/disk/by-id/usb-Generic_Flash_Disk-0:0')
        boot = objects['/org/freedesktop/UDisks2/block_devices/sda1']['org.freedesktop.UDisks2.Filesystem']
        self.assertEqual(boot['MountPoints'], '/boot')

    def test_only_removable_file_systems(self):
        found = helper.removable(helper.parse_dump(DUMP))
        self.assertEqual([(d, m) for d, _, m in found.values()], [('/dev/sdb1', False)])

    def test_hints_and_empty(self):
        text = (block('sdb1', 'Stick', ignore='true') + block('sdb2', 'Stick', system='true') +
                block('sdb3', 'Stick', size=0) + block('sdb4', 'Stick', usage='other') + drive('Stick'))
        self.assertEqual(helper.removable(helper.parse_dump(text)), {})

    def test_mounted_twice(self):
        text = block('sdb1', 'Stick', mounts=['/media/cr/HOLIDAY', '/mnt']) + drive('Stick')
        self.assertTrue(helper.removable(helper.parse_dump(text))['/org/freedesktop/UDisks2/block_devices/sdb1'][2])

    def test_monitor_lines(self):
        yes = ['12:34:30.376: Added /org/freedesktop/UDisks2/block_devices/sdb',
               '12:34:31.399: /org/freedesktop/UDisks2/block_devices/sdb1: org.freedesktop.UDisks2.Filesystem: '
               'Properties Changed', '12:34:33.440: Removed /org/freedesktop/UDisks2/block_devices/sdb1']
        no = ['Monitoring the udisks daemon. Press Ctrl+C to exit.',
              '12:34:29.318: The udisks-daemon is running (name-owner :1.71).',
              '12:40:01.000: /org/freedesktop/UDisks2/drives/TOSHIBA: org.freedesktop.UDisks2.Drive.Ata: '
              'Properties Changed',
              '  Drive:                      \'/org/freedesktop/UDisks2/drives/Stick\'',
              '    Partitions:         /org/freedesktop/UDisks2/block_devices/sdb1']
        self.assertTrue(all(map(helper.concerns_devices, yes)))
        self.assertFalse(any(map(helper.concerns_devices, no)))
        self.assertEqual([helper.removed(line) for line in yes],
                         [None, None, '/org/freedesktop/UDisks2/block_devices/sdb1'])


class Mounting(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.prefix = Path(temporary.name)
        (self.prefix / 'dosdevices').mkdir()
        self.dump, self.calls, self.fail = DUMP, [], ''
        self.mounter = helper.Mounter(str(self.prefix), 'wine', run=self.run_udisksctl)
        self.told = []
        real = helper.subprocess.Popen
        self.addCleanup(setattr, helper.subprocess, 'Popen', real)
        helper.subprocess.Popen = lambda args, **_: self.told.append(args)

    def run_udisksctl(self, args, **_):
        self.assertEqual(args[0], 'udisksctl')
        if args[1] == 'dump':
            return subprocess.CompletedProcess(args, 0, self.dump, '')
        self.calls.append(args[1:])
        if self.fail:
            return subprocess.CompletedProcess(args, 1, '', self.fail)
        return subprocess.CompletedProcess(args, 0, 'Mounted', '')

    def test_mounts_once(self):
        self.mounter.reconcile()
        # Ejected, and still there (a card in a reader that stays powered).
        self.mounter.reconcile()
        self.assertEqual(self.calls, [['mount', '--no-user-interaction', '-b', '/dev/sdb1']])

    def test_again_after_it_went(self):
        self.mounter.reconcile()
        self.dump = drive('Stick')
        self.mounter.reconcile()
        self.dump = DUMP
        self.mounter.reconcile()
        self.assertEqual(len(self.calls), 2)

    def test_back_before_read(self):
        """Pulled out and put back between two readings."""
        self.mounter.reconcile()
        self.mounter.forget('/org/freedesktop/UDisks2/block_devices/sdb1')
        self.mounter.reconcile()
        self.assertEqual(len(self.calls), 2)

    def test_again_for_another_card(self):
        self.mounter.reconcile()
        self.dump = DUMP.replace('266E-80E4', 'AAAA-0001')
        self.mounter.reconcile()
        self.assertEqual(len(self.calls), 2)

    def test_leaves_the_mounted(self):
        self.dump = block('sdb1', 'Stick', mounts=['/media/cr/HOLIDAY']) + drive('Stick')
        self.mounter.reconcile()
        self.assertEqual(self.calls, [])

    def test_not_formatted(self):
        os.symlink('/dev/sdb1', self.prefix / 'dosdevices/e::')
        self.fail = 'Error mounting /dev/sdb1: GDBus.Error:org.freedesktop.UDisks2.Error.Failed: wrong fs type'
        self.mounter.reconcile()
        self.assertEqual(self.told, [['wine', r'C:\xp\bin\xptray.exe', '--not-formatted', 'E:']])

    def test_already_mounted(self):
        self.fail = 'GDBus.Error:org.freedesktop.UDisks2.Error.AlreadyMounted: already mounted'
        self.mounter.reconcile()
        self.assertEqual(self.told, [])

    def test_drive_letter(self):
        self.assertIsNone(helper.drive_letter(str(self.prefix), '/dev/sdb1', wait=0))
        os.symlink('/dev/sdb1', self.prefix / 'dosdevices/f::')
        self.assertEqual(helper.drive_letter(str(self.prefix), '/dev/sdb1', wait=0), 'F:')


# udisksctl: "monitor" tells of a device after a while, "dump" shows it from
# then on, and calls are noted.
FAKE = r'''#!/usr/bin/env python3
import os, sys, time
d = os.path.dirname(os.path.abspath(sys.argv[0]))
if sys.argv[1] == 'monitor':
    print('Monitoring the udisks daemon. Press Ctrl+C to exit.', flush=True)
    print('12:00:00.000: The udisks-daemon is running (name-owner :1.1).', flush=True)
    time.sleep(0.5)
    os.rename(d + '/later', d + '/dump')
    for n in range(3):
        print(f'12:00:00.00{n}: Added /org/freedesktop/UDisks2/block_devices/sdb1\n  Detail: x', flush=True)
        time.sleep(0.05)
    time.sleep(1.5)
elif sys.argv[1] == 'dump':
    print(open(d + '/dump').read() if os.path.exists(d + '/dump') else '', end='')
    open(d + '/calls', 'a').write('dump\n')
else:
    open(d + '/calls', 'a').write(' '.join(sys.argv[1:]) + '\n')
'''


class Following(unittest.TestCase):
    def test_follow(self):
        """One reading at the start, one for a burst of changes, then the mount."""
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / 'udisksctl').write_text(FAKE)
        (root / 'udisksctl').chmod(0o755)
        (root / 'later').write_text(DUMP)
        old = os.environ['PATH']
        os.environ['PATH'] = f'{root}:{old}'
        self.addCleanup(os.environ.__setitem__, 'PATH', old)
        started = time.monotonic()
        helper.follow(helper.Mounter(str(root), 'wine'))
        self.assertLess(time.monotonic() - started, 10)
        self.assertEqual((root / 'calls').read_text().splitlines(),
                         ['dump', 'dump', 'mount --no-user-interaction -b /dev/sdb1'])


if __name__ == '__main__':
    unittest.main()
