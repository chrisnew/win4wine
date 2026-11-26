#!/usr/bin/env python3
"""session/idle-watch.c on a real X server: its reports, input and
XResetScreenSaver holding them off, DPMS, and that it sleeps while idle.

    tests/idle_watch.py IDLE_WATCH

Needs an X display of its own (DISPLAY, e.g. Xvfb) with SYNC and XTEST, and
python3-xlib. Build it with: gcc -o idle-watch session/idle-watch.c -lXext -lX11
"""
import argparse
from pathlib import Path
import select
import subprocess
import time

from Xlib import X, display
from Xlib.ext import dpms, xtest

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('binary', type=Path)
args = parser.parse_args()

d = display.Display()
d.set_screen_saver(600, 600, X.PreferBlanking, X.AllowExposures)
pointer = 0


def move():
    global pointer
    pointer = (pointer + 7) % 200
    xtest.fake_input(d, X.MotionNotify, x=100 + pointer, y=100)
    d.sync()


move()
watch = subprocess.Popen([str(args.binary.resolve())], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         bufsize=0)


def say(command):
    watch.stdin.write(command.encode() + b'\n')


def line(timeout):
    """The next report, or None after TIMEOUT seconds without one."""
    if not select.select([watch.stdout], [], [], timeout)[0]:
        return None
    return watch.stdout.readline().decode().strip()


def switches():
    text = Path(f'/proc/{watch.pid}/status').read_text()
    return sum(int(row.split()[1]) for row in text.splitlines() if 'ctxt_switches' in row)


def check(what, condition):
    print('ok' if condition else 'FAILED', what, flush=True)
    if not condition:
        watch.kill()
        raise SystemExit(1)


try:
    say('dpms 0')
    say('idle 2')
    time.sleep(0.3)
    check("the server's own blanking is off", d.get_screen_saver().timeout == 0)
    for round_ in (1, 2):
        move()
        start = time.monotonic()
        check(f'idle after 2 s ({round_})', line(4) == 'idle' and 1.7 < time.monotonic() - start < 3)
        check('nothing more while idle', line(2.5) is None)
        move()
        check(f'active on input ({round_})', line(1) == 'active')
    quiet = True
    for _ in range(6):
        time.sleep(0.7)
        d.force_screen_saver(X.ScreenSaverReset)
        d.sync()
        quiet = quiet and line(0) is None
    check('XResetScreenSaver holds it off', quiet)
    check('and then idle', line(4) == 'idle')
    say('idle 1')
    check('a new time starts afresh, reached already', line(1) == 'idle')
    say('idle 100')
    time.sleep(0.3)
    before = switches()
    time.sleep(5)
    check(f'sleeps while idle ({switches() - before} context switches in 5 s)', switches() == before)
    say('off')
    say('nonsense')
    move()
    check('off: no reports', line(2) is None)
    say('idle 1')
    check('on again', line(3) == 'idle')
    if d.has_extension('DPMS') and d.dpms_capable().capable:
        say('dpms 90')
        time.sleep(0.3)
        times = d.dpms_get_timeouts()
        check('DPMS off time', (times.standby_timeout, times.suspend_timeout, times.off_timeout) == (0, 0, 90))
        say('dpms 0')
        time.sleep(0.3)
        check('DPMS never', d.dpms_get_timeouts().off_timeout == 0)
    else:
        print('skipped DPMS: the server has none')
    watch.stdin.close()
    check('ends with its input', watch.wait(timeout=3) == 0)
finally:
    if watch.poll() is None:
        watch.kill()
