#!/usr/bin/env python3
"""Watch who gets the keyboard: the X input focus, _NET_ACTIVE_WINDOW, and the
activation requests and map/unmap/destroy events on the root, with times.

  python3 tools/eeepc/focuswatch.py [SECONDS]      (default 5; needs DISPLAY)

Start it, then press the keys (Alt+F4, Alt+Tab) or close the window to look
at. A "request _NET_ACTIVE_WINDOW" line names the window someone asked the WM
to activate: from Wine (source 2) after a window of the foreground went, it
shows which window Wine chose. Runs on the EeePC (copy it there) or the bench.
"""
import sys
import time

from Xlib import X, display

d = display.Display()
root = d.screen().root
root.change_attributes(event_mask=X.SubstructureNotifyMask)
ACTIVE = d.intern_atom('_NET_ACTIVE_WINDOW')
NAME = d.intern_atom('_NET_WM_NAME')
EVENTS = {X.MapNotify: 'map', X.UnmapNotify: 'unmap', X.DestroyNotify: 'destroy'}


def name(window_id):
    if window_id in (X.NONE, X.PointerRoot):
        return {X.NONE: 'None', X.PointerRoot: 'PointerRoot'}[window_id]
    try:
        window = d.create_resource_object('window', window_id)
        title = window.get_full_property(NAME, 0)
        return (title.value.decode('utf-8', 'replace') if title else window.get_wm_name()) or f'{window_id:#x}'
    except Exception:
        return f'{window_id:#x} (gone)'


def state():
    focus = d.get_input_focus().focus
    focus = focus.id if hasattr(focus, 'id') else focus
    active = root.get_full_property(ACTIVE, 0)
    return focus, active.value[0] if active and len(active.value) else 0


start, last = time.monotonic(), None
end = start + (float(sys.argv[1]) if len(sys.argv) > 1 else 5)
while time.monotonic() < end:
    stamp = f'{time.monotonic() - start:7.3f}'
    while d.pending_events():
        event = d.next_event()
        if event.type == X.ClientMessage:
            print(stamp, 'request', d.get_atom_name(event.client_type), repr(name(event.window.id)),
                  'data', list(event.data[1][:3]), flush=True)
        elif event.type in EVENTS:
            print(stamp, EVENTS[event.type], f'{event.window.id:#x}', flush=True)
    now = state()
    if now != last:
        print(stamp, f'focus={name(now[0])!r} active={name(now[1])!r}', flush=True)
        last = now
    time.sleep(.005)
