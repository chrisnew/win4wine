#!/usr/bin/env python3
"""Drive a WM through frames, commands, icons, maximizing and stacking, and
print its state after each step, for comparing bridge-wm/wm.py with wm.c:

    tests/wm_parity.py --wm 'python3 bridge-wm/wm.py' > py.txt
    tests/wm_parity.py --wm .local/bridge/wm > c.txt
    diff py.txt c.txt

Needs an empty X display of its own (DISPLAY, e.g. Xvfb at 1024x600) and
python3-xlib. No Wine: a plain window with _WIN4WINE_CLASS Win4WineProxy and
a frames.tsv written here stand in for the proxy's frame."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import time
from Xlib import X, Xatom, Xutil, display, protocol

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--wm', required=True, help='the WM command, without --ipc')
parser.add_argument('--settle', type=float, default=0.4, help='seconds to wait after each step')
args = parser.parse_args()

ipc = Path(tempfile.mkdtemp(prefix='wm-parity-'))
wm = subprocess.Popen(shlex.split(args.wm) + ['--ipc', str(ipc)])
time.sleep(1)
d = display.Display()
root = d.screen().root
atom = d.intern_atom
names = {}
sequence = 0


def settle():
    d.sync()
    time.sleep(args.settle)
    d.sync()


def make(name, x, y, width, height, cls=None, hwnd=None, override=False, mapped=True):
    w = root.create_window(x, y, width, height, 0, X.CopyFromParent, X.InputOutput, X.CopyFromParent,
                           background_pixel=0x406080, override_redirect=override,
                           event_mask=X.StructureNotifyMask)
    names[w.id] = name
    if cls:
        w.change_property(atom('_WIN4WINE_CLASS'), Xatom.STRING, 8, cls.encode())
    if hwnd is not None:
        w.change_property(atom('_WIN4WINE_HWND'), Xatom.CARDINAL, 32, [hwnd])
    if mapped:
        w.map()
    return w


def name(i):
    return names.get(i, 'root' if i == root.id else 'none' if i == 0 else 'other')


def prop(w, key):
    p = w.get_full_property(atom(key), X.AnyPropertyType)
    return list(p.value) if p else []


def command(text):
    global sequence
    sequence += 1
    temporary = ipc / f'999-{sequence}.tmp'
    temporary.write_text(text)
    temporary.replace(ipc / f'999-{sequence}.cmd')


def frames(text):
    temporary = ipc / 'frames.tmp'
    temporary.write_text(text)
    temporary.replace(ipc / 'frames.tsv')


def message(w, kind, values):
    root.send_event(protocol.event.ClientMessage(window=w, client_type=atom(kind), data=(32, values + [0] * (5 - len(values)))),
                    event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)


def ipc_text(file):
    path = ipc / file
    if not path.exists():
        return None
    text = path.read_text(errors='replace')
    for i, n in names.items():
        text = text.replace(f'{i:x}', n)
    return text


def dump(step):
    settle()
    print(f'== {step}')
    print('root', 'active', [name(i) for i in prop(root, '_NET_ACTIVE_WINDOW')],
          'workarea', prop(root, '_NET_WORKAREA'))
    print('root', 'clients', [name(i) for i in prop(root, '_NET_CLIENT_LIST')])
    print('root', 'stacking', [name(i) for i in prop(root, '_NET_CLIENT_LIST_STACKING')])
    print('stack', [names[w.id] for w in root.query_tree().children if w.id in names])
    focus = d.get_input_focus().focus
    print('focus', name(focus if isinstance(focus, int) else focus.id))
    for i, n in sorted(names.items(), key=lambda item: item[1]):
        w = d.create_resource_object('window', i)
        try:
            a = w.get_attributes()
            g = w.get_geometry()
        except Exception:
            print(n, 'gone')
            continue
        states = sorted(d.get_atom_name(s) for s in prop(w, '_NET_WM_STATE'))
        print(n, ['unmapped', 'unviewable', 'viewable'][a.map_state], (g.x, g.y, g.width, g.height, g.border_width),
              'WM_STATE', prop(w, 'WM_STATE'), 'state', states, 'extents', prop(w, '_NET_FRAME_EXTENTS'))
    for file in ('windows.tsv', 'classes.tsv', 'geometry.tsv', 'shellkeys.tsv'):
        print(file, repr(ipc_text(file)))
    for path in sorted(ipc.glob('icon-*.argb')):
        n = path.name
        for i, window in names.items():
            n = n.replace(f'-{i:x}', f'-{window}')
        data = path.read_bytes()
        width, height = struct.unpack_from('<II', data, 4)
        print('icon', n, width, height, hashlib.sha1(data).hexdigest()[:12])
    print('commands left', sorted(p.name for p in ipc.glob('*.cmd')))
    synthetic = []
    while d.pending_events():
        e = d.next_event()
        if e.type == X.ConfigureNotify and e.send_event:
            synthetic.append((name(e.window.id), e.x, e.y, e.width, e.height))
        elif e.type == X.ClientMessage:
            synthetic.append((name(e.window.id), d.get_atom_name(e.client_type),
                              d.get_atom_name(e.data[1][0]) if e.data[1][0] else 0))
    print('received', synthetic)


def net_icon(sizes):
    data = []
    for side, color in sizes:
        data += [side, side] + [color | (i % 7) << 3 for i in range(side * side)]
    return data


try:
    g = root.get_geometry()
    desktop = make('desktop', 0, 0, g.width, g.height, 'PROGMAN', hwnd=0x10)
    dock = make('dock', 0, g.height - 30, g.width, 30, 'Shell_TrayWnd', hwnd=0x20)
    dump('shell')

    a = make('native-a', 0, 0, 500, 300, mapped=False)
    a.set_wm_class('xterm', 'XTerm')
    a.change_property(atom('_NET_WM_NAME'), atom('UTF8_STRING'), 8, 'Tab\there é \xff'.encode('utf-8') + b'\xc3(')
    a.change_property(atom('_NET_WM_ICON'), Xatom.CARDINAL, 32, net_icon([(16, 0xff102030), (48, 0x80405060)]))
    a.set_wm_normal_hints(flags=Xutil.PMinSize, min_width=200, min_height=100)
    a.set_wm_protocols([atom('WM_DELETE_WINDOW')])
    a.map()
    dump('native a')

    b = make('native-b', 100, 100, 300, 200, mapped=False)
    b.set_wm_name('Latin \xe9 name')
    pixmap = b.create_pixmap(32, 32, d.screen().root_depth)
    gc = pixmap.create_gc(foreground=0x00c08040)
    pixmap.fill_rectangle(gc, 0, 0, 32, 32)
    mask = b.create_pixmap(32, 32, 1)
    mgc = mask.create_gc(foreground=0)
    mask.fill_rectangle(mgc, 0, 0, 32, 32)
    mgc.change(foreground=1)
    mask.fill_rectangle(mgc, 4, 4, 24, 24)
    b.set_wm_hints(flags=Xutil.IconPixmapHint | Xutil.IconMaskHint, icon_pixmap=pixmap, icon_mask=mask)
    b.set_wm_normal_hints(flags=Xutil.USPosition, x=100, y=100)
    b.map()
    dump('native b with a WM_HINTS icon')

    c = make('owned-c', 0, 0, 900, 700, mapped=False)
    c.set_wm_transient_for(a)
    c.map()
    dump('owned c, too large')

    frame = make('frame-a', 0, 0, 508, 334, 'Win4WineProxy', hwnd=0x1234)
    dump('frame without frames.tsv')
    frames(f'1234 {a.id:x} 4 4 30 4 0 0\n')
    dump('frame of a')

    frame.configure(x=40, y=50)
    dump('frame moved')
    frame.configure(width=600, height=400)
    dump('frame resized')

    geometry = [int(v) for v in (ipc / 'geometry.tsv').read_text().split('\n')[0].split()[1:5]]
    command(f'geometry {a.id:x} {geometry[0]} {geometry[1]} {geometry[2]} {geometry[3]}\n')
    command(f'geometry {a.id:x} 60 70 320 240\n')
    dump('geometry commands')

    command(f'maximize {a.id:x}\n')
    dump('maximize command')
    frames(f'1234 {a.id:x} 4 4 30 4 1 0 4 4\n')
    dump('maximized frame')
    command(f'restore {a.id:x}\n')
    frames(f'1234 {a.id:x} 4 4 30 4 0 0\n')
    dump('restored')

    message(b, '_NET_WM_STATE', [1, atom('_NET_WM_STATE_ABOVE')])
    dump('b above')
    message(b, '_NET_WM_STATE', [0, atom('_NET_WM_STATE_ABOVE')])
    message(b, 'WM_CHANGE_STATE', [3])
    dump('b minimized')
    message(b, '_NET_ACTIVE_WINDOW', [1])
    dump('b activated')
    message(a, '_NET_WM_STATE', [2, atom('_NET_WM_STATE_MAXIMIZED_VERT'), atom('_NET_WM_STATE_MAXIMIZED_HORZ')])
    dump('a maximized by message')
    message(frame, '_NET_WM_STATE', [0, atom('_NET_WM_STATE_MAXIMIZED_VERT'), atom('_NET_WM_STATE_MAXIMIZED_HORZ')])
    dump('a restored through its frame')

    a.change_property(atom('_NET_WM_NAME'), atom('UTF8_STRING'), 8, b'New title')
    dump('title')
    dock.change_property(atom('_NET_WM_STRUT_PARTIAL'), Xatom.CARDINAL, 32, [0, 0, 0, 40] + [0] * 8)
    dump('strut')

    popup = make('popup', 200, 200, 100, 100, 'DV2ControlHost', hwnd=0x40)
    dump('popup')
    overlay = make('start-button', 0, g.height - 30, 50, 30, 'Win4WineStartButton', override=True)
    dump('overlay')
    menu = make('menu', 10, 300, 100, 200, override=True)
    settle()
    command(f'activate {b.id:x}\n')
    dump('menu over b')

    command(f'close {a.id:x}\n')
    command(f'minimize {b.id:x}\n')
    command(f'switch {b.id:x}\n')
    command('bogus\n')
    dump('more commands')

    a.destroy()
    dump('a destroyed')
    b.unmap()
    b.send_event(protocol.event.UnmapNotify(window=b, event=root, from_configure=False),
                 event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
    dump('b withdrawn')
finally:
    wm.terminate()
    wm.wait()
