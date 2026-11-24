"""Screenshot that stacks each viewable top-level window's own image (xwd -root misses redirected windows): shot.py OUT.ppm"""
import sys
from Xlib import X, display, error
d = display.Display(); root = d.screen().root
g = root.get_geometry(); W, H = g.width, g.height
canvas = bytearray(root.get_image(0, 0, W, H, X.ZPixmap, 0xffffffff).data)
for w in root.query_tree().children:
    try:
        if w.get_attributes().map_state != X.IsViewable: continue
        wg = w.get_geometry()
        x0, y0 = max(wg.x, 0), max(wg.y, 0)
        x1, y1 = min(wg.x + wg.width, W), min(wg.y + wg.height, H)
        if x1 <= x0 or y1 <= y0 or wg.depth not in (24, 32): continue
        img = w.get_image(x0 - wg.x, y0 - wg.y, x1 - x0, y1 - y0, X.ZPixmap, 0xffffffff).data
    except error.XError:
        continue
    row = (x1 - x0) * 4
    for y in range(y1 - y0):
        canvas[((y0 + y) * W + x0) * 4:((y0 + y) * W + x1) * 4] = img[y * row:(y + 1) * row]
out = bytearray(W * H * 3)
out[0::3], out[1::3], out[2::3] = canvas[2::4], canvas[1::4], canvas[0::4]
with open(sys.argv[1], 'wb') as f:
    f.write(b'P6 %d %d 255\n' % (W, H)); f.write(out)
