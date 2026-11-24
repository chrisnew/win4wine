"""Timeline of a Linux window getting its XP frame: timeline.py IPC COMMAND..."""
import ctypes, os, select, struct, subprocess, sys, time
from Xlib import X, display, error
ipc = sys.argv[1]; cmd = sys.argv[2:]
d = display.Display(); root = d.screen().root
root.change_attributes(event_mask=X.SubstructureNotifyMask | X.PropertyChangeMask)
cls, ext, hwnd = d.intern_atom('_WIN4WINE_CLASS'), d.intern_atom('_NET_FRAME_EXTENTS'), d.intern_atom('_WIN4WINE_HWND')
libc = ctypes.CDLL(None)
ino = libc.inotify_init1(os.O_NONBLOCK); libc.inotify_add_watch(ino, ipc.encode(), 0x8 | 0x80)
t0 = time.monotonic(); log = lambda s: print(f'{(time.monotonic()-t0)*1000:7.1f} ms  {s}', flush=True)
proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); log('started ' + ' '.join(cmd))
watched = set(); done = None
while time.monotonic() - t0 < 5 and (done is None or time.monotonic() - done < .3):
    r = select.select([d.fileno(), ino], [], [], .05)[0]
    if ino in r:
        data = os.read(ino, 4096); off = 0
        while off + 16 <= len(data):
            n = struct.unpack_from('iIII', data, off)[3]; name = data[off+16:off+16+n].rstrip(b'\0').decode()
            if not name.endswith('.tmp') and not name.startswith(('icon-',)): log('ipc ' + name)
            off += 16 + n
    while d.pending_events():
        e = d.next_event()
        try:
            if e.type == X.CreateNotify:
                log(f'create {e.window.id:#x} {e.width}x{e.height}')
                e.window.change_attributes(event_mask=X.PropertyChangeMask); watched.add(e.window.id)
            elif e.type == X.MapNotify:
                p = e.window.get_full_property(cls, X.AnyPropertyType)
                log(f'map {e.window.id:#x} {bytes(p.value).rstrip(bytes(1)).decode() if p else ""}')
            elif e.type == X.PropertyNotify and e.atom in (ext, hwnd, cls):
                log(f'property {d.get_atom_name(e.atom)} on {e.window.id:#x}')
                if e.atom == ext: done = time.monotonic()
        except error.XError:
            pass
log('end'); proc.terminate()
