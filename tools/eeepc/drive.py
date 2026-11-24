"""Drive the EeePC's X display: drive.py ACTION... (move X Y | click X Y | shot NAME | sleep S | key KEYSYM)."""
import subprocess, sys, time
from Xlib import X, XK, display
from Xlib.ext import xtest
XK.load_keysym_group("xf86")
d = display.Display()
args = sys.argv[1:]
while args:
    op = args.pop(0)
    if op == 'move':
        x, y = int(args.pop(0)), int(args.pop(0)); xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync()
    elif op == 'click':
        x, y = int(args.pop(0)), int(args.pop(0)); xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync(); time.sleep(.1)
        xtest.fake_input(d, X.ButtonPress, 1); d.sync(); time.sleep(.05); xtest.fake_input(d, X.ButtonRelease, 1); d.sync()
    elif op == 'rclick':
        x, y = int(args.pop(0)), int(args.pop(0)); xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync(); time.sleep(.1)
        xtest.fake_input(d, X.ButtonPress, 3); d.sync(); time.sleep(.05); xtest.fake_input(d, X.ButtonRelease, 3); d.sync()
    elif op == 'dblclick':
        x, y = int(args.pop(0)), int(args.pop(0)); xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync(); time.sleep(.1)
        for _ in range(2):
            xtest.fake_input(d, X.ButtonPress, 1); d.sync(); time.sleep(.05); xtest.fake_input(d, X.ButtonRelease, 1); d.sync(); time.sleep(.08)
    elif op == 'key':
        name = args.pop(0); keysym = int(name, 16) if name.startswith('0x') else XK.string_to_keysym(name)
        code = d.keysym_to_keycode(keysym)
        assert code, name
        xtest.fake_input(d, X.KeyPress, code); d.sync(); xtest.fake_input(d, X.KeyRelease, code); d.sync()
    elif op == 'sleep':
        time.sleep(float(args.pop(0)))
    elif op == 'shot':
        subprocess.run([sys.executable, '/tmp/w4w-shot.py', f'/tmp/w4w-{args.pop(0)}.ppm'], check=True)
