"""Log the taskbar's X window state, the screen size and the session status while something happens."""
import json, sys, time
from pathlib import Path
from Xlib import X, display, error
d = display.Display(); root = d.screen().root
cls = d.intern_atom('_WIN4WINE_CLASS')
status = Path.home() / '.local/state/win4wine/status.json'
end = time.time() + float(sys.argv[1]); last = None; t0 = time.time()
while time.time() < end:
    tray = None
    try:
        for w in root.query_tree().children:
            p = w.get_full_property(cls, X.AnyPropertyType)
            if p and bytes(p.value).rstrip(b'\0').lower() == b'shell_traywnd':
                a, g = w.get_attributes(), w.get_geometry()
                tray = (hex(w.id), ('unmapped', 'unviewable', 'viewable')[a.map_state], g.x, g.y, g.width, g.height)
    except error.XError as e:
        tray = ('xerror', type(e).__name__)
    g = root.get_geometry()
    try: s = json.loads(status.read_text()); st = (s.get('state'), s.get('attempt'), s.get('failures'))
    except Exception: st = None
    now = (tray, (g.width, g.height), st)
    if now != last:
        print(f'{time.time()-t0:7.2f}', *now, flush=True); last = now
    time.sleep(.05)
