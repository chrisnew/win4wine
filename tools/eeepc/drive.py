"""Drive an X display with XTest (the EeePC's, or the bench's): drive.py ACTION...

  move X Y | click X Y | rclick X Y | dblclick X Y     the pointer and button 1 (rclick: button 3)
  scroll up|down N                                    N wheel steps at the pointer
  key KEYSYM                                          one key (a name like Return or F5, or 0x... )
  chord KEYSYM+KEYSYM...                              pressed in order, released in reverse (Super_L+r, Alt_L+F4)
  type TEXT                                           text, each character on the key and level the
                                                      layout has it on (Shift, AltGr: the EeePC's is German)
  wait TEXT [SECONDS]                                 wait for a viewable window whose title holds TEXT;
                                                      prints the seconds since the action before (default 40)
  windows                                             print the viewable windows' titles
  sleep S
  shot NAME                                           shot.py to /tmp/w4w-NAME.ppm

Several actions in one call: drive.py chord Super_L+r sleep 1 type 'notepad' key Return wait Notepad
"""
from pathlib import Path
import subprocess, sys, time
from Xlib import X, XK, display, error
from Xlib.ext import xtest
XK.load_keysym_group('xf86')
d = display.Display()
root = d.screen().root
utf8, net_name = d.intern_atom('UTF8_STRING'), d.intern_atom('_NET_WM_NAME')


def ev(kind, detail=0, **where):
    xtest.fake_input(d, kind, detail, **where); d.sync(); time.sleep(.02)


def keysym(name):
    value = int(name, 16) if name.startswith('0x') else XK.string_to_keysym(name)
    assert value, f'unknown keysym {name}'
    return value


def code(name):
    c = d.keysym_to_keycode(keysym(name))
    assert c, f'no key for {name}'
    return c


def pointer(x, y):
    ev(X.MotionNotify, x=x, y=y); time.sleep(.1)


def press(button, times=1):
    for _ in range(times):
        ev(X.ButtonPress, button); time.sleep(.03); ev(X.ButtonRelease, button); time.sleep(.06)


def type_char(ch):
    sym = XK.string_to_keysym({'\n': 'Return', '\t': 'Tab', ' ': 'space'}.get(ch, '')) or (
        ord(ch) if ord(ch) < 0x100 else 0x1000000 | ord(ch))
    # Levels: 0 plain, 1 Shift, 4 AltGr (ISO_Level3_Shift), 5 Shift+AltGr.
    found = [p for p in d.keysym_to_keycodes(sym) if p[1] in (0, 1, 4, 5)]
    assert found, f'no key for {ch!r} in this layout'
    keycode, level = min(found, key=lambda p: p[1])
    mods = ([code('Shift_L')] if level in (1, 5) else []) + ([code('ISO_Level3_Shift')] if level >= 4 else [])
    for m in mods: ev(X.KeyPress, m)
    ev(X.KeyPress, keycode); ev(X.KeyRelease, keycode)
    for m in reversed(mods): ev(X.KeyRelease, m)


def titles(window=root, depth=0):
    for child in window.query_tree().children:
        try:
            if child.get_attributes().map_state != X.IsViewable:
                continue
            prop = child.get_full_property(net_name, utf8)
            title = prop.value.decode(errors='replace') if prop else str(child.get_wm_name() or '')
            if title:
                yield title
            if depth < 2:
                yield from titles(child, depth + 1)
        except error.XError:
            pass


args = sys.argv[1:]
if not args or args[0] in ('-h', '--help'):
    sys.exit(__doc__)
last = time.monotonic()
while args:
    op = args.pop(0)
    if op in ('move', 'click', 'rclick', 'dblclick'):
        pointer(int(args.pop(0)), int(args.pop(0)))
        if op != 'move':
            press(3 if op == 'rclick' else 1, 2 if op == 'dblclick' else 1)
    elif op == 'scroll':
        button = {'up': 4, 'down': 5}[args.pop(0)]
        for _ in range(int(args.pop(0))):
            press(button); time.sleep(.05)
    elif op == 'key':
        c = code(args.pop(0)); ev(X.KeyPress, c); ev(X.KeyRelease, c)
    elif op == 'chord':
        codes = [code(n) for n in args.pop(0).split('+')]
        for c in codes: ev(X.KeyPress, c)
        for c in reversed(codes): ev(X.KeyRelease, c)
    elif op == 'type':
        for ch in args.pop(0):
            type_char(ch)
    elif op == 'wait':
        text = args.pop(0)
        limit = float(args.pop(0)) if args and args[0].replace('.', '', 1).isdigit() else 40
        while not any(text in t for t in titles()):
            if time.monotonic() - last > limit:
                sys.exit(f'no window with {text!r} after {limit:g} s; windows: {sorted(set(titles()))}')
            time.sleep(.1)
        print(f'{text!r} after {time.monotonic() - last:.1f} s')
    elif op == 'windows':
        print('\n'.join(sorted(set(titles()))))
    elif op == 'sleep':
        time.sleep(float(args.pop(0)))
    elif op == 'shot':
        shot = Path(__file__).with_name('shot.py')
        shot = shot if shot.exists() else Path('/tmp/w4w-shot.py')
        subprocess.run([sys.executable, str(shot), f'/tmp/w4w-{args.pop(0)}.ppm'], check=True)
    else:
        sys.exit(f'unknown action {op!r}\n{__doc__}')
    if op != 'wait':
        last = time.monotonic()
