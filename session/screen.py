"""The XP session's screen savers and lock screen.

The supervisor owns this and sleeps on its pipes (fds) and processes
(processes) with everything else; handle() then does what has happened. It
polls nothing, so an idle machine is not woken up.

 * session/idle-watch.c says when the user is idle and when they come back,
   with XP's own settings from Display Properties (HKCU\\Control Panel\\Desktop
   in the prefix's user.reg, which is watched with inotify; the wineserver
   writes it within half a minute of a change).
 * The screen saver is the .scr the user chose, run with /s as XP runs it.
 * The lock screen (greeter/xp-lock.py) comes up when it is asked for: by
   LockWorkStation (Wine patch 0159 writes IPC/lock-request), by logind (lid
   and suspend, session/logind-watch.py) or with "On resume, password
   protect" behind a starting screen saver. IPC/lock exists while the screen
   is locked; a lock screen that dies without unlocking is started again.

Between this and the lock screen (its stdin and stdout):
  to it:    yield        a screen saver starts over the locked screen
            saver-ended  the screen saver's process ended
            lock         lock now, also within the screen saver's grace time
  from it:  locked       it holds the keyboard and the pointer
            behind       it waits below the screen saver
and it exits with 0 when unlocked, 2 when it cannot work on this machine.
"""
from collections import deque
import ctypes
import os
from pathlib import Path
import re
import struct
import subprocess
import time
from typing import NamedTuple

import keyboard

IN_CLOSE_WRITE, IN_MOVED_TO = 0x8, 0x80
DESKTOP_KEY = r'Control Panel\Desktop'
# Seconds in which a screen saver can be dismissed without the password, as
# XP's ScreenSaverGracePeriod.
GRACE = 5
# Lock screens that may die in a row before one has locked anything
LOCK_FAILURES = 3


class Settings(NamedTuple):
    saver: str      # the .scr as a Windows path, '' for none
    timeout: int    # seconds idle before it starts, 0 for never
    secure: bool    # "On resume, password protect"


def settings(prefix):
    """The Screen Saver tab's settings in the prefix, with Windows' defaults."""
    values = {k.lower(): v for k, v in keyboard.registry_values(Path(prefix), DESKTOP_KEY).items()}
    saver = values.get('scrnsave.exe', '').strip()
    try:
        timeout = int(values.get('screensavetimeout', 300))
    except ValueError:
        timeout = 300
    if values.get('screensaveactive', '1') == '0' or not saver or saver.lower() == '(none)':
        timeout = 0
    return Settings(saver, max(0, timeout), values.get('screensaverissecure', '0') == '1')


def program(prefix, path):
    """PATH of a screen saver as a program started from Linux finds it.

    The Screen Saver tab is a 32-bit program: its system32 is the prefix's
    syswow64, where XP's screen savers are (prefix/extras.py). What Wine
    starts from outside is 64-bit and would look in the real system32."""
    match = re.match(r'(?i)^(?:(c:\\windows)\\system32\\)?([^\\:]+)$', path)
    if not match:
        return path
    windows = Path(prefix) / 'drive_c/windows'
    name = match.group(2).lower()

    def there(directory):
        try:
            return any(entry.lower() == name for entry in os.listdir(windows / directory))
        except OSError:
            return False
    if there('system32') or not there('syswow64'):
        return path
    return f'{match.group(1) or "C:\\windows"}\\syswow64\\{match.group(2)}'


class Watch:
    """inotify on directories, for the files written into them or renamed there."""

    def __init__(self, *directories):
        libc = ctypes.CDLL(None, use_errno=True)
        self.fd = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
        if self.fd < 0:
            raise OSError(ctypes.get_errno(), 'inotify')
        self.directories = {}
        for directory in directories:
            number = libc.inotify_add_watch(self.fd, os.fsencode(directory), IN_CLOSE_WRITE | IN_MOVED_TO)
            if number < 0:
                os.close(self.fd)
                raise OSError(ctypes.get_errno(), f'inotify {directory}')
            self.directories[number] = Path(directory)

    def paths(self):
        """The files changed since the last call."""
        found = set()
        while True:
            try:
                data = os.read(self.fd, 65536)
            except BlockingIOError:
                return found
            offset = 0
            while offset + 16 <= len(data):
                number, _, _, length = struct.unpack_from('iIII', data, offset)
                name = data[offset + 16:offset + 16 + length].rstrip(b'\0')
                if number in self.directories:
                    found.add(self.directories[number] / os.fsdecode(name))
                offset += 16 + length

    def close(self):
        os.close(self.fd)


class Helper:
    """A child process that talks in lines on its stdin and stdout."""

    def __init__(self, command, log, env):
        with open(log, 'ab', buffering=0) as errors:
            errors.write(f'\n--- start {time.time()} ---\n'.encode())
            self.process = subprocess.Popen(command, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=errors, start_new_session=True)
        self.out = self.process.stdout.fileno()
        os.set_blocking(self.out, False)
        self.buffer = b''
        self.ended = False  # its stdout is at its end

    def send(self, word):
        try:
            self.process.stdin.write(word.encode() + b'\n')
            self.process.stdin.flush()
        except (OSError, ValueError):
            pass  # It exited; its exit is handled.

    def lines(self):
        while not self.ended:
            try:
                data = os.read(self.out, 4096)
            except BlockingIOError:
                break
            if not data:
                self.ended = True
            self.buffer += data
        *lines, self.buffer = self.buffer.split(b'\n')
        return [line.decode(errors='replace').strip() for line in lines]

    def fds(self):
        return [] if self.ended else [self.out]

    def close(self):
        for pipe in (self.process.stdin, self.process.stdout):
            try:
                pipe.close()
            except OSError:
                pass


class Screen:
    def __init__(self, *, prefix, ipc, logs, env, saver_command, idle_command=None, lock_command=None,
                 logind_command=None, dpms=0):
        """SAVER_COMMAND runs a Windows program (the session's Wine); a
        command that is None leaves its part out. DPMS: seconds idle before
        the monitor goes off, 0 for never."""
        self.prefix, self.ipc, self.logs, self.env = Path(prefix), Path(ipc), Path(logs), env
        self.saver_command = list(saver_command)
        self.idle_command, self.lock_command, self.logind_command = idle_command, lock_command, logind_command
        self.dpms = dpms
        self.idle = self.logind = self.lock = None  # Helpers
        self.saver = None                           # its process
        self.watch = None
        self.current = Settings('', 0, False)
        self.sent = None            # the idle command idle-watch has
        self.lock_ready = False     # the lock screen reported that it is up
        self.lock_failures = 0
        self.suspending = False     # logind waits for the screen to be locked
        self.idle_restarts = deque()
        self.started = False

    # The supervisor's side ------------------------------------------

    def start(self):
        if self.started:
            return
        self.started = True
        self.request.unlink(missing_ok=True)
        self.watch = Watch(self.prefix, self.ipc)
        if self.idle_command:
            self.start_idle()
        if self.logind_command and self.lock_command:
            self.logind = Helper(self.logind_command, self.logs / 'logind.log', self.env)

    @property
    def request(self):
        return self.ipc / 'lock-request'

    @property
    def lock_file(self):
        return self.ipc / 'lock'

    def fds(self):
        fds = [self.watch.fd] if self.watch else []
        for helper in (self.idle, self.logind, self.lock):
            if helper:
                fds += helper.fds()
        return fds

    def processes(self):
        helpers = [h.process for h in (self.idle, self.logind, self.lock) if h]
        return helpers + ([self.saver] if self.saver else [])

    def handle(self):
        if not self.started:
            return
        changed = self.watch.paths() if self.watch else set()
        if self.prefix / 'user.reg' in changed:
            self.apply_settings()
        if self.request in changed and self.request.exists():
            self.request.unlink(missing_ok=True)
            self.request_lock()
        if self.logind:
            for line in self.logind.lines():
                if line == 'lock':
                    self.suspending = True
                    self.request_lock()
            if self.logind.process.poll() is not None:
                print('logind-watch exited: no lock on suspend; see logind.log', flush=True)
                self.logind.close()
                self.logind = None
                self.suspending = False
        if self.saver and self.saver.poll() is not None:
            self.saver = None
            if self.lock:
                self.lock.send('saver-ended')
        if self.lock:
            self.handle_lock()
        if self.idle:
            self.handle_idle()

    def stop_saving(self):
        """No more screen savers (the shell failed); the lock stays."""
        if self.idle:
            self.halting([self.idle.process])
            self.idle.close()
            self.idle = None
        self.idle_command = None

    def close(self, halt):
        """End everything, with HALT (supervisor.Session.halt)."""
        processes = self.processes()
        if processes:
            halt(*processes)
        for helper in (self.idle, self.logind, self.lock):
            if helper:
                helper.close()
        self.idle = self.logind = self.lock = self.saver = None
        if self.watch:
            self.watch.close()
            self.watch = None
        self.lock_file.unlink(missing_ok=True)
        self.started = False

    @staticmethod
    def halting(processes):
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    # Idle time and screen saver -------------------------------------

    def start_idle(self):
        self.idle = Helper(self.idle_command, self.logs / 'idle-watch.log', self.env)
        self.idle.send(f'dpms {self.dpms}')
        self.sent = None
        self.apply_settings()

    def apply_settings(self):
        self.current = settings(self.prefix)
        if not self.idle:
            return
        command = f'idle {self.current.timeout}' if self.current.timeout else 'off'
        if command != self.sent:
            # Only a change: a new time starts idle-watch's report afresh.
            self.sent = command
            self.idle.send(command)

    def handle_idle(self):
        for line in self.idle.lines():
            if line == 'idle':
                self.start_saver()
        if self.idle.process.poll() is not None:
            self.idle.close()
            self.idle = None
            now = time.monotonic()
            self.idle_restarts.append(now)
            while self.idle_restarts and now - self.idle_restarts[0] > 120:
                self.idle_restarts.popleft()
            if len(self.idle_restarts) > 3:
                print('idle-watch keeps exiting: no screen saver; see idle-watch.log', flush=True)
            else:
                self.start_idle()

    def start_saver(self):
        """idle-watch reports "idle" once until the user comes back, so a
        screen saver that ends by itself is not started again before that."""
        current = settings(self.prefix)
        if self.saver or not current.timeout:
            return
        if self.lock:
            self.lock.send('yield')
        elif current.secure and self.lock_command:
            self.start_lock(behind=True, grace=GRACE)
        with open(self.logs / 'screensaver.log', 'ab', buffering=0) as log:
            log.write(f'\n--- {current.saver} {time.time()} ---\n'.encode())
            try:
                self.saver = subprocess.Popen([*self.saver_command, program(self.prefix, current.saver), '/s'],
                                              env=self.env,
                                              stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                              start_new_session=True)
            except OSError as e:
                log.write(f'{e}\n'.encode())
                if self.lock:
                    self.lock.send('saver-ended')

    # Lock screen ----------------------------------------------------

    def request_lock(self):
        if not self.lock_command:
            self.locked()
        elif self.lock:
            self.lock.send('lock')
            if self.lock_ready:
                self.locked()
        else:
            self.lock_failures = 0
            self.start_lock(behind=self.saver is not None, grace=0)

    def start_lock(self, behind, grace):
        self.lock_ready = False
        self.lock_file.write_text('locked\n')
        command = [*self.lock_command, '--grace', str(grace)] + (['--behind'] if behind else [])
        try:
            self.lock = Helper(command, self.logs / 'lock.log', self.env)
        except OSError as e:
            print(f'cannot start the lock screen: {e}', flush=True)
            self.unlocked()

    def handle_lock(self):
        status = self.lock.process.poll()  # first, so that its last lines are read below
        for line in self.lock.lines():
            if line in ('locked', 'behind'):
                self.lock_ready = True
                self.lock_failures = 0
                self.locked()
        if status is None:
            return
        self.lock.close()
        self.lock = None
        if status == 0:
            self.unlocked()
        elif status == 2:
            print('the lock screen cannot work here; see lock.log', flush=True)
            self.unlocked()
        else:
            # It died with the screen locked, or on its way there.
            if not self.lock_ready:
                self.lock_failures += 1
            if self.lock_failures < LOCK_FAILURES:
                print(f'the lock screen ended with status {status}: starting it again', flush=True)
                self.start_lock(behind=self.saver is not None, grace=0)
            else:
                print('the lock screen keeps failing: giving up; see lock.log', flush=True)
                self.unlocked()

    def unlocked(self):
        self.lock_file.unlink(missing_ok=True)
        self.locked()  # nothing left to wait for

    def locked(self):
        """Let logind suspend the machine, if it waits for the lock."""
        if self.suspending and self.logind:
            self.logind.send('locked')
        self.suspending = False
