"""Capture a window's own image as fast as possible from the moment a window of class CLASS maps.
mapcap.py CLASS SECONDS OUTDIR ACTION..."""
import hashlib, os, sys, time
from Xlib import X, XK, display, error
from Xlib.ext import xtest
cls_name, seconds, out, actions = sys.argv[1].lower().encode(), float(sys.argv[2]), sys.argv[3], sys.argv[4:]
d = display.Display(); root = d.screen().root; cls = d.intern_atom('_WIN4WINE_CLASS')
root.change_attributes(event_mask=X.SubstructureNotifyMask); os.makedirs(out, exist_ok=True)
def act(args):
    while args:
        op = args.pop(0)
        if op in ('click', 'dblclick'):
            x, y = int(args.pop(0)), int(args.pop(0)); xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync(); time.sleep(.05)
            for _ in range(1 if op == 'click' else 2):
                xtest.fake_input(d, X.ButtonPress, 1); d.sync(); time.sleep(.03); xtest.fake_input(d, X.ButtonRelease, 1); d.sync(); time.sleep(.06)
        elif op == 'sleep': time.sleep(float(args.pop(0)))
t0 = time.monotonic(); act(list(actions)); target = None
while target is None and time.monotonic() - t0 < 5:
    e = d.next_event()
    if e.type == X.MapNotify:
        try:
            p = e.window.get_full_property(cls, X.AnyPropertyType)
            if p and bytes(p.value).rstrip(b'\0').lower() == cls_name: target = e.window
        except error.XError: pass
tm = time.monotonic(); frames, last, count = [], None, 0
while time.monotonic() - tm < seconds:
    try:
        g = target.get_geometry(); img = target.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff).data
    except error.XError: break
    count += 1; h = hashlib.md5(img).digest()
    if h != last: frames.append((time.monotonic() - tm, g.width, g.height, img)); last = h
for i, (t, w, h, f) in enumerate(frames):
    rgb = bytearray(w * h * 3); rgb[0::3], rgb[1::3], rgb[2::3] = f[2::4], f[1::4], f[0::4]
    open(f'{out}/{i:03d}-{t*1000:06.1f}ms.ppm', 'wb').write(b'P6 %d %d 255\n' % (w, h) + rgb)
print(f'mapped at {(tm-t0)*1000:.0f} ms after the trigger; {count} captures in {seconds}s, {len(frames)} distinct')
