"""Drive a WM on its own X server as Wine's move loop drags a native client's frame.
wmbench.py IPC STEPS HZ  (DISPLAY: the WM's server)"""
import os, sys, time
from pathlib import Path
from Xlib import X, Xatom, display
ipc = Path(sys.argv[1]); steps = int(sys.argv[2]); hz = float(sys.argv[3])
d = display.Display(); root = d.screen().root
def wait(pred, what, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        d.sync()
        if pred(): return
        time.sleep(.01)
    raise SystemExit('timeout: ' + what)
native = root.create_window(200, 200, 400, 250, 0, X.CopyFromParent, X.InputOutput, X.CopyFromParent,
                            background_pixel=0x208040, event_mask=X.StructureNotifyMask)
native.set_wm_name('bench native'); native.map()
frame = root.create_window(196, 170, 408, 284, 0, X.CopyFromParent, X.InputOutput, X.CopyFromParent,
                           background_pixel=0x0050e0)
frame.change_property(d.intern_atom('_WIN4WINE_CLASS'), Xatom.STRING, 8, b'Win4WineProxy')
frame.change_property(d.intern_atom('_WIN4WINE_HWND'), Xatom.CARDINAL, 32, [0x1234])
frame.set_wm_name('bench frame'); frame.map(); d.sync()
tmp = ipc / 'frames.tmp'; tmp.write_text(f'1234 {native.id:x} 4 4 30 4 0 0 0 0\n'); tmp.replace(ipc / 'frames.tsv')
extents = d.intern_atom('_NET_FRAME_EXTENTS')
wait(lambda: native.get_full_property(extents, X.AnyPropertyType) is not None, 'frame pairs')
time.sleep(1)
f = frame.get_geometry(); x0, y0 = f.x, f.y
t0 = time.monotonic()
for i in range(1, steps + 1):
    frame.configure(x=x0 + (i % 200), y=y0 + (i % 100) // 2); d.flush()
    time.sleep(max(0, t0 + i / hz - time.monotonic()))
wait(lambda: native.get_geometry().x == x0 + (steps % 200) + 4, 'native follows', 10)
print(f'{steps} frame moves in {time.monotonic()-t0:.2f}s')
