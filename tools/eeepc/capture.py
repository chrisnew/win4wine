"""Capture a screen region frame by frame after a trigger, stacking each window's own image
(root reads miss redirected windows). Keeps changed frames only.
capture.py X Y W H SECONDS OUTDIR ACTION... (actions as in drive.py: click X Y, dblclick X Y, key NAME, sleep S)"""
import hashlib, os, sys, time
from Xlib import X, XK, display, error
from Xlib.ext import xtest
XK.load_keysym_group('xf86')
rx, ry, rw, rh, seconds, out = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5]), sys.argv[6]
actions = sys.argv[7:]
d = display.Display(); root = d.screen().root
os.makedirs(out, exist_ok=True)
def frame():
    canvas = bytearray(root.get_image(rx, ry, rw, rh, X.ZPixmap, 0xffffffff).data)
    for w in root.query_tree().children:
        try:
            if w.get_attributes().map_state != X.IsViewable: continue
            g = w.get_geometry()
            if g.depth not in (24, 32): continue
            x0, y0 = max(g.x, rx), max(g.y, ry); x1, y1 = min(g.x + g.width, rx + rw), min(g.y + g.height, ry + rh)
            if x1 <= x0 or y1 <= y0: continue
            img = w.get_image(x0 - g.x, y0 - g.y, x1 - x0, y1 - y0, X.ZPixmap, 0xffffffff).data
        except error.XError:
            continue
        row = (x1 - x0) * 4
        for y in range(y1 - y0):
            o = ((y0 - ry + y) * rw + (x0 - rx)) * 4
            canvas[o:o + row] = img[y * row:(y + 1) * row]
    return bytes(canvas)
def act(args):
    while args:
        op = args.pop(0)
        if op in ('click', 'dblclick', 'move'):
            x, y = int(args.pop(0)), int(args.pop(0)); xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync(); time.sleep(.05)
            for _ in range({'click': 1, 'dblclick': 2, 'move': 0}[op]):
                xtest.fake_input(d, X.ButtonPress, 1); d.sync(); time.sleep(.03); xtest.fake_input(d, X.ButtonRelease, 1); d.sync(); time.sleep(.06)
        elif op == 'key':
            code = d.keysym_to_keycode(XK.string_to_keysym(args.pop(0))); xtest.fake_input(d, X.KeyPress, code); xtest.fake_input(d, X.KeyRelease, code); d.sync()
        elif op == 'sleep':
            time.sleep(float(args.pop(0)))
frames, last = [], None
frames.append((0.0, frame())); last = hashlib.md5(frames[0][1]).digest()
t0 = time.monotonic(); act(list(actions)); count = 0
while time.monotonic() - t0 < seconds:
    f = frame(); count += 1; h = hashlib.md5(f).digest()
    if h != last: frames.append((time.monotonic() - t0, f)); last = h
for i, (t, f) in enumerate(frames):
    rgb = bytearray(rw * rh * 3); rgb[0::3], rgb[1::3], rgb[2::3] = f[2::4], f[1::4], f[0::4]
    with open(f'{out}/{i:03d}-{int(t*1000):05d}ms.ppm', 'wb') as fh:
        fh.write(b'P6 %d %d 255\n' % (rw, rh)); fh.write(rgb)
print(f'{count} frames in {seconds}s ({count/seconds:.0f}/s), {len(frames)} distinct')
