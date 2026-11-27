#!/usr/bin/env python3
"""session/iso_mount.py with a stand-in for udisksctl and made-up /sys and
/proc/self/mounts. No Wine, no UDisks2, no loop devices.

    tests/iso_mount.py
"""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('iso_mount_helper', ROOT / 'session/iso_mount.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)

# udisksctl: notes its arguments and answers as the file COMMAND.ctl says
# ("STATUS" on the first line, then the text), else with success. A mount adds
# /dev/loop3 to the file "mounts", of the type in "type".
FAKE = r'''#!/usr/bin/env python3
import os, sys
d = os.path.dirname(os.path.abspath(sys.argv[0]))
command = sys.argv[1]
with open(d + '/calls', 'a') as f:
    f.write(' '.join(sys.argv[1:]) + ' LC_ALL=' + os.environ.get('LC_ALL', '') + '\n')
if os.path.exists(f'{d}/{command}.ctl'):
    status, _, text = open(f'{d}/{command}.ctl').read().partition('\n')
    print(text, end='', file=sys.stderr if int(status) else sys.stdout)
    sys.exit(int(status))
if command == 'loop-setup':
    print(f'Mapped file {sys.argv[-1]} as /dev/loop3.')
if command == 'mount':
    kind = open(d + '/type').read() if os.path.exists(d + '/type') else 'iso9660'
    with open(d + '/mounts', 'a') as f:
        f.write(f'/dev/loop3 /media/me/OFFICE\\04010 {kind} ro 0 0\n')
'''


class Mount(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        (self.root / 'udisksctl').write_text(FAKE)
        (self.root / 'udisksctl').chmod(0o755)
        (self.root / 'mounts').write_text('/dev/sda3 / ext4 rw 0 0\n')
        # A name with what a shell would read.
        self.image = self.root / 'Office "XP" $(x).iso'
        self.image.write_bytes(b'image')
        old_path = os.environ['PATH']
        old_runtime = os.environ.get('XDG_RUNTIME_DIR')
        os.environ['PATH'] = f'{self.root}:{old_path}'
        os.environ['XDG_RUNTIME_DIR'] = str(self.root)
        self.addCleanup(os.environ.__setitem__, 'PATH', old_path)
        if old_runtime is not None:
            self.addCleanup(os.environ.__setitem__, 'XDG_RUNTIME_DIR', old_runtime)
        real_loop, real_mounted = helper.loop_device, helper.mounted
        self.addCleanup(setattr, helper, 'loop_device', real_loop)
        self.addCleanup(setattr, helper, 'mounted', real_mounted)
        helper.loop_device = lambda image: real_loop(image, str(self.root / 'sys'))
        helper.mounted = lambda device: real_mounted(device, str(self.root / 'mounts'))

    def calls(self):
        path = self.root / 'calls'
        return [line.split()[0] for line in path.read_text().splitlines()] if path.exists() else []

    def result(self, image=None):
        helper.main(str(image or self.image), str(self.root / 'result'))
        return (self.root / 'result').read_text().split('\n')

    def answer(self, command, status, text):
        (self.root / f'{command}.ctl').write_text(f'{status}\n{text}')

    def loop(self, number, backing):
        directory = self.root / f'sys/loop{number}/loop'
        directory.mkdir(parents=True)
        (directory / 'backing_file').write_text(f'{backing}\n')

    def test_mount(self):
        self.assertEqual(self.result(), ['ok', '/dev/loop3', '/media/me/OFFICE 10', 'new', ''])
        self.assertEqual(self.calls(), ['loop-setup', 'mount'])
        first = (self.root / 'calls').read_text().splitlines()[0]
        self.assertEqual(first, f'loop-setup --no-user-interaction -r --no-partition-scan -f {self.image} '
                                'LC_ALL=C.UTF-8')

    def test_symlink(self):
        link = self.root / 'link.iso'
        link.symlink_to(self.image)
        self.loop(3, self.image)
        (self.root / 'mounts').write_text('/dev/loop3 /media/me/DISC iso9660 ro 0 0\n')
        self.assertEqual(self.result(link), ['ok', '/dev/loop3', '/media/me/DISC', 'old', ''])
        self.assertEqual(self.calls(), [])

    def test_attached_but_not_mounted(self):
        self.loop(10, '/somewhere/else.iso')
        self.loop(3, self.image)
        self.assertEqual(self.result(), ['ok', '/dev/loop3', '/media/me/OFFICE 10', 'new', ''])
        self.assertEqual(self.calls(), ['mount'])

    def test_not_a_disc(self):
        self.answer('mount', 1, 'Error mounting /dev/loop3: GDBus.Error:org.freedesktop.UDisks2.Error.Failed: '
                                'Object /org/freedesktop/UDisks2/block_devices/loop3 is not a mountable filesystem.\n')
        self.assertEqual(self.result(), ['error', 'It is not a disc image (ISO 9660 or UDF).', ''])
        self.assertEqual(self.calls(), ['loop-setup'] + ['mount'] * 5 + ['loop-delete'])

    def test_another_file_system(self):
        (self.root / 'type').write_text('ext4')
        self.assertEqual(self.result(), ['error', 'It is not a disc image (ISO 9660 or UDF).', ''])
        self.assertEqual(self.calls(), ['loop-setup', 'mount', 'unmount', 'loop-delete'])

    def test_refused(self):
        self.answer('loop-setup', 1, 'Error setting up loop device for x: GDBus.Error:'
                                     'org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain: Not authorized\n')
        self.assertEqual(self.result(), ['error', 'Error setting up loop device for x: Not authorized', ''])
        self.assertEqual(self.calls(), ['loop-setup'])

    def test_mount_fails(self):
        self.answer('mount', 1, 'wrong fs type\n')
        self.assertEqual(self.result(), ['error', 'wrong fs type', ''])
        self.assertEqual(self.calls(), ['loop-setup', 'mount', 'loop-delete'])

    def test_missing(self):
        self.assertEqual(self.result(self.root / 'gone.iso'), ['error', 'No such file or directory.', ''])
        self.assertEqual(self.result(self.root), ['error', 'It is not a file.', ''])
        self.assertEqual(self.calls(), [])

    def test_no_udisks(self):
        os.environ['PATH'] = str(self.root / 'empty')
        self.assertEqual(self.result()[1], 'UDisks2 is not installed (the Debian package udisks2).')


if __name__ == '__main__':
    unittest.main()
