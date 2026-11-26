"""Drag a native client's XP frame by its caption with XTest; report each process's CPU and the client's lag.
dragbench.py XID [STEPS] [HZ]"""
import os, sys, time
from pathlib import Path
from Xlib import X, display
from Xlib.ext import xtest
d = display.Display(); root = d.screen().root
xid = int(sys.argv[1], 16); steps = int(sys.argv[2]) if len(sys.argv) > 2 else 240; hz = float(sys.argv[3]) if len(sys.argv) > 3 else 60
w = d.create_resource_object('window', xid)
ext = list(w.get_full_property(d.intern_atom('_NET_FRAME_EXTENTS'), X.AnyPropertyType).value)
def pos():
    g = w.get_geometry(); t = w.translate_coords(root, 0, 0)
    return -t.x, -t.y
def procs():
    found = {}
    for e in Path('/proc').iterdir():
        if not e.name.isdigit(): continue
        try: cmd = (e / 'cmdline').read_bytes().replace(b'\0', b' ').decode('utf-8', 'replace')
        except OSError: continue
        for key, pat in (('wm', 'bridge-wm/wm.py'), ('wm', '.local/bridge/wm --ipc'), ('proxy', 'proxy.exe'), ('wineserver', 'bin/wineserver'),
                         ('Xorg', 'Xorg'), ('xterm', 'xterm'), ('xp-explorer', 'xp-explorer.exe'), ('supervisor', 'supervisor.py'),
                         ('tray', 'tray.py')):
            if pat in cmd and 'dragbench' not in cmd: found[key] = int(e.name)
    return found
def ticks(pid):
    f = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split(); return int(f[11]) + int(f[12])
x0, y0 = pos()
cx, cy = x0 + 60, y0 - ext[2] // 2   # a point on the caption
pids = procs(); before = {k: ticks(p) for k, p in pids.items()}
xtest.fake_input(d, X.MotionNotify, x=cx, y=cy); d.sync(); time.sleep(.3)
xtest.fake_input(d, X.ButtonPress, 1); d.sync(); time.sleep(.2)
t0 = time.monotonic(); lags = []
for i in range(1, steps + 1):
    dx = int(200 * (i / steps)); dy = int(100 * (i / steps))
    xtest.fake_input(d, X.MotionNotify, x=cx + dx, y=cy + dy); d.sync()
    target = t0 + i / hz
    if i % 10 == 0:
        px, py = pos(); lags.append(abs((x0 + dx) - px) + abs((y0 + dy) - py))
    time.sleep(max(0, target - time.monotonic()))
drag = time.monotonic() - t0
xtest.fake_input(d, X.ButtonRelease, 1); d.sync()
end = (x0 + 200, y0 + 100); t1 = time.monotonic()
while pos() != end and time.monotonic() - t1 < 10: time.sleep(.005)
settle = time.monotonic() - t1
time.sleep(.5)
after = {k: ticks(p) for k, p in pids.items()}
hz_clk = os.sysconf('SC_CLK_TCK')
print(f'drag {steps} steps in {drag:.2f}s; client reached the end {settle*1000:.0f} ms after release; final {pos()} want {end}')
print(f'lag during drag (px, every 10th step): median {sorted(lags)[len(lags)//2]}, max {max(lags)}')
for k in sorted(pids, key=lambda k: -(after[k] - before[k])):
    print(f'  {k:12} {100 * (after[k] - before[k]) / hz_clk / drag:5.1f}% CPU')
