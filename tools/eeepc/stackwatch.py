import sys, time
from Xlib import X, display, error
d = display.Display(); root = d.screen().root
cls = d.intern_atom('_WIN4WINE_CLASS')
root.change_attributes(event_mask=X.SubstructureNotifyMask)
def name(wid):
    wid = getattr(wid, "id", wid)
    try:
        w = d.create_resource_object('window', wid); p = w.get_full_property(cls, X.AnyPropertyType)
        return f"{wid:#x}:{bytes(p.value).rstrip(bytes(1)).decode() if p else '-'}"
    except error.XError: return f'{wid:#x}:gone'
t0 = time.time(); end = t0 + float(sys.argv[1])
while time.time() < end:
    while d.pending_events():
        e = d.next_event()
        if e.type == X.ConfigureNotify and e.window.id != root.id:
            print(f'{time.time()-t0:6.2f} configure {name(e.window.id)} above={name(e.above_sibling) if e.above_sibling else 0} {e.width}x{e.height}+{e.x}+{e.y}', flush=True)
        elif e.type in (X.MapNotify, X.UnmapNotify, X.DestroyNotify):
            print(f'{time.time()-t0:6.2f} {e.__class__.__name__} {name(e.window.id)}', flush=True)
    time.sleep(.01)
