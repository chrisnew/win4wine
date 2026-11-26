#!/usr/bin/env python3
"""session/screen.py: the XP settings, and when the screen saver and the lock
screen start, with stand-ins for idle-watch, the screen saver, the lock screen
and logind-watch. No X and no Wine.

    tests/screen_guard.py
"""
from pathlib import Path
import select
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'session'))

import screen  # noqa: E402

# A stand-in: it notes its arguments and what it reads, and does what the
# test writes into ROLE.ctl ("say WORD", "exit STATUS").
FAKE = r'''
import os, sys, threading, time
role, directory = sys.argv[1], sys.argv[2]
base = os.path.join(directory, role)
with open(base + '.args', 'a') as f:
    f.write(' '.join(sys.argv[3:]) + '\n')
def read():
    for line in sys.stdin:
        with open(base + '.in', 'a') as f:
            f.write(line)
threading.Thread(target=read, daemon=True).start()
while True:
    if os.path.exists(base + '.ctl'):
        lines = open(base + '.ctl').read().splitlines()
        os.unlink(base + '.ctl')
        for line in lines:
            word, value = line.split()
            if word == 'exit':
                os._exit(int(value))
            print(value, flush=True)
    time.sleep(0.01)
'''


class Guard(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for name in ('prefix/drive_c/windows/syswow64', 'ipc', 'logs', 'fake'):
            (self.root / name).mkdir(parents=True)
        (self.root / 'prefix/drive_c/windows/syswow64/ssstars.scr').write_bytes(b'')
        (self.root / 'fake.py').write_text(FAKE)
        self.registry()
        self.guard = None

    def tearDown(self):
        if self.guard:
            self.guard.close(lambda *processes: self.guard.halting(processes))

    def fake(self, role):
        return [sys.executable, str(self.root / 'fake.py'), role, str(self.root / 'fake')]

    def registry(self, active='1', saver=r'C:\\windows\\system32\\ssstars.scr', timeout='60', secure='0'):
        target = self.root / 'prefix/user.reg'
        temporary = target.with_name('user.tmp')
        temporary.write_text('WINE REGISTRY Version 2\n\n[Control Panel\\\\Desktop] 1\n'
                             f'"ScreenSaveActive"="{active}"\n"SCRNSAVE.EXE"="{saver}"\n'
                             f'"ScreenSaveTimeOut"="{timeout}"\n"ScreenSaverIsSecure"="{secure}"\n')
        temporary.replace(target)

    def start(self, lock=True, logind=False):
        self.guard = screen.Screen(
            prefix=self.root / 'prefix', ipc=self.root / 'ipc', logs=self.root / 'logs', env=None,
            saver_command=self.fake('saver'), idle_command=self.fake('idle'),
            lock_command=self.fake('lock') if lock else None,
            logind_command=self.fake('logind') if logind else None, dpms=0)
        self.guard.start()

    def read(self, name):
        try:
            return (self.root / 'fake' / name).read_text().splitlines()
        except FileNotFoundError:
            return []

    def pump(self, condition, timeout=10):
        """What supervisor.Session.wait does, until CONDITION holds."""
        end = time.monotonic() + timeout
        while not condition():
            self.assertLess(time.monotonic(), end, 'timed out; ' + repr(
                {p.name: p.read_text() for p in (self.root / 'fake').iterdir()}))
            select.select(self.guard.fds(), [], [], 0.02)
            self.guard.handle()

    def settle(self, seconds=0.5):
        end = time.monotonic() + seconds
        self.pump(lambda: time.monotonic() > end)

    def tell(self, role, *lines):
        self.pump(lambda: not (self.root / 'fake' / f'{role}.ctl').exists())
        temporary = self.root / 'fake' / f'{role}.new'
        temporary.write_text(''.join(line + '\n' for line in lines))
        temporary.replace(self.root / 'fake' / f'{role}.ctl')

    def request(self):
        temporary = self.root / 'ipc/lock-request.tmp'
        temporary.write_text('lock\n')
        temporary.replace(self.root / 'ipc/lock-request')

    @property
    def locked(self):
        return (self.root / 'ipc/lock').exists()

    def test_settings(self):
        prefix = self.root / 'prefix'
        self.assertEqual(screen.settings(prefix), screen.Settings(r'C:\windows\system32\ssstars.scr', 60, False))
        self.registry(active='0')
        self.assertEqual(screen.settings(prefix).timeout, 0)
        self.registry(saver='')
        self.assertEqual(screen.settings(prefix).timeout, 0)
        self.registry(secure='1')
        self.assertTrue(screen.settings(prefix).secure)

    def test_program(self):
        prefix = self.root / 'prefix'
        for path in (r'C:\windows\system32\ssstars.scr', r'C:\WINDOWS\System32\SSSTARS.SCR', 'ssstars.scr'):
            self.assertRegex(screen.program(prefix, path).lower(), r'^c:\\windows\\syswow64\\ssstars\.scr$')
        for path in (r'D:\savers\ssstars.scr', r'C:\other\ssstars.scr'):
            self.assertEqual(screen.program(prefix, path), path)

    def test_saver_once_per_idle_time(self):
        self.start()
        self.pump(lambda: self.read('idle.in') == ['dpms 0', 'idle 60'])
        self.tell('idle', 'say idle')
        self.pump(lambda: self.read('saver.args'))
        self.assertTrue(self.read('saver.args')[0].endswith(r'syswow64\ssstars.scr /s'))
        self.assertFalse(self.locked)
        self.tell('saver', 'exit 0')
        self.pump(lambda: self.guard.saver is None)
        self.settle()
        self.assertEqual(len(self.read('saver.args')), 1)
        self.tell('idle', 'say active', 'say idle')
        self.pump(lambda: len(self.read('saver.args')) == 2)

    def test_settings_followed(self):
        self.start()
        self.pump(lambda: self.read('idle.in')[-1:] == ['idle 60'])
        self.registry(timeout='120')
        self.pump(lambda: self.read('idle.in')[-1:] == ['idle 120'])
        self.registry(timeout='120', secure='1')  # no new time: nothing to say
        self.settle()
        self.registry(active='0')
        self.pump(lambda: self.read('idle.in')[-1:] == ['off'])
        self.assertEqual(self.read('idle.in'), ['dpms 0', 'idle 60', 'idle 120', 'off'])

    def test_secure_saver(self):
        self.registry(secure='1')
        self.start()
        self.tell('idle', 'say idle')
        self.pump(lambda: self.read('lock.args') and self.read('saver.args'))
        self.assertEqual(self.read('lock.args'), ['--grace 5 --behind'])
        self.assertTrue(self.locked)
        self.tell('saver', 'exit 0')
        self.pump(lambda: self.read('lock.in') == ['saver-ended'])
        self.tell('lock', 'exit 0')
        self.pump(lambda: not self.locked)
        self.assertIsNone(self.guard.lock)

    def test_saver_over_locked_screen(self):
        self.start()
        self.request()
        self.pump(lambda: self.read('lock.args') == ['--grace 0'])
        self.tell('lock', 'say locked')
        self.tell('idle', 'say idle')
        self.pump(lambda: self.read('lock.in') == ['yield'] and self.read('saver.args'))
        self.request()  # asked again while locked
        self.pump(lambda: self.read('lock.in') == ['yield', 'lock'])
        self.assertEqual(len(self.read('lock.args')), 1)

    def test_lock_during_saver(self):
        self.start()
        self.tell('idle', 'say idle')
        self.pump(lambda: self.read('saver.args'))
        self.request()
        self.pump(lambda: self.read('lock.args') == ['--grace 0 --behind'])

    def test_lock_started_again(self):
        self.start()
        self.request()
        for count in (1, 2, 3, 4, 5):
            self.pump(lambda: len(self.read('lock.args')) == count)
            self.tell('lock', 'say locked', 'exit 1')
        self.pump(lambda: len(self.read('lock.args')) == 6)
        self.assertTrue(self.locked)

    def test_lock_that_never_locks_is_given_up(self):
        self.start()
        self.request()
        for count in (1, 2, 3):
            self.pump(lambda: len(self.read('lock.args')) == count)
            self.tell('lock', 'exit 1')
        self.pump(lambda: not self.locked)
        self.settle()
        self.assertEqual(len(self.read('lock.args')), 3)

    def test_lock_not_set_up(self):
        self.start()
        self.request()
        self.tell('lock', 'exit 2')
        self.pump(lambda: self.read('lock.args') and self.guard.lock is None)
        self.assertFalse(self.locked)

    def test_no_lock_screen(self):
        self.registry(secure='1')
        self.start(lock=False)
        self.request()
        self.tell('idle', 'say idle')
        self.pump(lambda: self.read('saver.args'))
        self.assertFalse(self.locked)

    def test_suspend_waits_for_the_lock(self):
        self.start(logind=True)
        self.tell('logind', 'say lock')
        self.pump(lambda: self.read('lock.args'))
        self.settle()
        self.assertEqual(self.read('logind.in'), [])
        self.tell('lock', 'say locked')
        self.pump(lambda: self.read('logind.in') == ['locked'])
        self.tell('logind', 'say lock')  # locked already: no wait
        self.pump(lambda: self.read('logind.in') == ['locked', 'locked'])

    def test_idle_watch_restarted(self):
        self.start()
        for count in (1, 2, 3, 4):
            self.pump(lambda: len(self.read('idle.args')) == count)
            self.tell('idle', 'exit 1')
        self.pump(lambda: self.guard.idle is None)
        self.settle()
        self.assertEqual(len(self.read('idle.args')), 4)


if __name__ == '__main__':
    unittest.main()
