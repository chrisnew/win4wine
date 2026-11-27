#!/usr/bin/env python3
"""Lock the XP session behind XP's Welcome screen, as Fast User Switching shows it.

The session supervisor runs this (session/screen.py). It covers the screen
with a window of its own, which the WM keeps on top (_WIN4WINE_LOCK), holds
the keyboard and the pointer, and shows the session's user with the number of
programs running. PAM checks the password (/etc/pam.d/win4wine-lock,
greeter/install.sh); no Microsoft code sees it.

With --behind a screen saver is starting: the window goes below the screen
saver's as soon as that one is there, takes no input, and comes to the front
when the screen saver's window goes. Within --grace seconds of the start that
unlocks without a password, as XP's grace period does.

With --standby it is started ahead of the lock: it loads and draws what it
can without the display, then waits on stdin for "start", followed by any
of --behind and --grace, and locks. Starting it and its first drawing take
about 2 s on an Atom, which a lock then does not wait for. It holds no X
connection while it waits, so nothing wakes it, and it reads the keyboard
layout and the screen's size when it locks.

Lines on stdin:  yield (a screen saver starts over the locked screen),
saver-ended, lock (no grace period any more).  Lines on stdout: locked (it
holds the keyboard and the pointer), behind.

Exit status: 0 unlocked, 2 it cannot lock on this machine (not set up), any
other: it failed while the screen should be locked. One that had waited
(--standby) and then fails exits with 1, so that a new one is tried.
"""
import argparse
import os
from pathlib import Path
import pwd
import select
import sys
import threading
import time
import traceback

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

SERVICE = 'win4wine-lock'
SETTLE = 0.3       # s a screen saver's window may be away within the grace period
GRAB_FAST, GRAB_SLOW, GRAB_HURRY = 0.1, 0.5, 2.0  # s between tries to grab, and how long they hurry


def log(message):
    print(f'xp-lock: {message}', file=sys.stderr, flush=True)


def report(word):
    try:
        print(word, flush=True)
    except OSError:
        pass  # The supervisor is gone; the screen stays locked.


def parse(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dpi', type=int, default=96, help="the XP shell's LogPixels")
    parser.add_argument('--assets', help="extract/extract.py's output, without the greeter's configuration")
    parser.add_argument('--behind', action='store_true', help='below the screen saver that is starting')
    parser.add_argument('--grace', type=float, default=0, help='seconds in which no password is asked for')
    parser.add_argument('--standby', action='store_true', help='get ready, then wait on stdin for "start [OPTIONS]"')
    return parser.parse_args(arguments)


def welcome_screen(welcome, config, assets, dpi):
    """XP's Welcome screen with the session's user alone."""
    entry = pwd.getpwuid(os.getuid())
    full_name = entry.pw_gecos.split(',')[0]
    picture = next((p for p in (Path('/var/lib/AccountsService/icons') / entry.pw_name,
                                Path(entry.pw_dir) / '.face') if p.is_file() and os.access(p, os.R_OK)),
                   welcome.default_picture(config, assets))
    screen = welcome.Screen(assets, [welcome.User(entry.pw_name, full_name or None, picture)], dpi=dpi)
    screen.power = False
    return screen


def warm(screen):
    """Draw off screen what the lock screen will show, so that its images and
    fonts are loaded: the list with the note, then the password box with the
    balloon. The user's picture stays out: reading one that is no BMP needs
    the display."""
    account = screen.users[0]
    picture, account.picture = account.picture, None
    account.note = screen.assets.string('logonui', 30).replace('%d', '2')
    screen.render(1024, 600)
    screen.selected, screen.error = 0, True
    screen.render(1024, 600)
    screen.selected, screen.error = None, False
    account.picture, account.note = picture, ''


def wait_for_start():
    """The options of the "start" line on stdin, or None when stdin ends.
    Read a byte at a time: what follows it is for Lock.run()."""
    line = b''
    while True:
        byte = os.read(sys.stdin.fileno(), 1)
        if not byte:
            return None
        if byte != b'\n':
            line += byte
            continue
        words = line.decode(errors='replace').split()
        if words[:1] == ['start']:
            return words[1:]
        line = b''


def programs(d, root, X):
    """The windows that have a button on the taskbar, as far as X tells them apart."""
    def values(window, name):
        found = window.get_full_property(d.intern_atom(name), X.AnyPropertyType)
        return found.value if found else []

    skip = {d.intern_atom('_NET_WM_STATE_SKIP_TASKBAR')}
    shell = {d.intern_atom('_NET_WM_WINDOW_TYPE_' + name) for name in ('DOCK', 'DESKTOP')}
    count = 0
    for number in values(root, '_NET_CLIENT_LIST'):
        window = d.create_resource_object('window', number)
        try:
            name = bytes(values(window, '_WIN4WINE_CLASS')).rstrip(b'\0').lower()
            names = window.get_wm_class()
            if name in (b'shell_traywnd', b'progman', b'workerw') or window.get_wm_transient_for() \
                    or skip & set(values(window, '_NET_WM_STATE')) \
                    or shell & set(values(window, '_NET_WM_WINDOW_TYPE')) \
                    or (names and names[0].lower().endswith('.scr')):
                continue
            count += 1
        except Exception:  # It closed meanwhile.
            pass
    return count


def main():
    args = parse()
    started = time.monotonic()
    waited = False  # asked to lock after waiting (--standby)
    try:
        if not Path('/etc/pam.d', SERVICE).is_file():
            raise RuntimeError(f'/etc/pam.d/{SERVICE} is missing (greeter/install.sh)')
        import welcome
        cache = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'win4wine/welcome'
        config, assets = welcome.setup(cache, args.assets)
        from Xlib import X, display
        import pamauth
        import xdraw
        from welcomeinput import CARET_BLINK, Input
        screen = welcome_screen(welcome, config, assets, args.dpi)
        if args.standby:
            warm(screen)
            options = wait_for_start()
            if options is None:
                return 0  # never asked to lock
            args = parse(sys.argv[1:] + options)
            waited = True
            started = time.monotonic()  # the grace period begins now
        # From here on the display: importing Gdk connects to it.
        import gi
        gi.require_version('Gdk', '3.0')
        gi.require_version('GdkX11', '3.0')
        from gi.repository import Gdk, GdkX11
        Gdk.init([])
        gdk = Gdk.Display.get_default()
        if gdk is None:
            raise RuntimeError('cannot open the display')
    except Exception as e:
        traceback.print_exc()
        log(f'cannot lock the screen: {e}')
        return 1 if waited else 2

    class Lock(Input):
        def __init__(self):
            self.screen = screen
            self.account = screen.users[0]
            self.user = self.account.name
            self.d = display.Display()
            self.root = self.d.screen().root
            geometry = self.root.get_geometry()
            self.width, self.height = geometry.width, geometry.height
            self.grace = args.grace
            self.state = 'wait'      # for the screen saver's window; 'behind' it; 'front'
            self.saver = None        # the screen saver's window, behind it
            self.grabbed = False
            self.grab_at = self.blink_at = self.settle_at = None
            self.grab_since = 0
            self.checking = None     # the thread that asks PAM
            self.result = False
            self.finished = os.pipe()
            self.dirty = True
            self.frames = {}         # the screen with the caret on and off, once drawn
            self.pixmap = self.shown = None
            self.cursors = {}
            for name in ('default', 'pointer'):
                cursor = Gdk.Cursor.new_from_name(gdk, name)
                self.cursors[name] = (cursor, GdkX11.X11Cursor.get_xcursor(cursor) if cursor else X.NONE)
            gdk.sync()
            self.hand = False
            self.window = self.root.create_window(
                0, 0, self.width, self.height, 0, self.d.screen().root_depth, override_redirect=True,
                event_mask=X.KeyPressMask | X.ButtonPressMask | X.ButtonReleaseMask | X.PointerMotionMask |
                X.VisibilityChangeMask, cursor=self.cursors['default'][1])
            self.window.set_wm_name('win4wine-lock')
            self.window.set_wm_class('xp-lock', 'Win4wineLock')
            self.window.change_property(self.d.intern_atom('_WIN4WINE_LOCK'), self.d.intern_atom('CARDINAL'),
                                        32, [1])
            self.root.change_attributes(event_mask=X.SubstructureNotifyMask | X.StructureNotifyMask)

        # Drawing ------------------------------------------------------

        def refresh(self):
            """What the screen shows has changed."""
            self.frames.clear()
            self.dirty = True

        def draw(self):
            """Draw the screen, and send the X server the rows that changed."""
            self.dirty = False
            # A blinking caret goes between two images; drawing one takes a
            # good part of the blink's half second on an Atom.
            caret = self.screen.caret
            if caret not in self.frames:
                surface = self.screen.render(self.width, self.height)
                self.frames[caret] = bytes(surface.get_data()), surface.get_stride()
            data, stride = self.frames[caret]
            if self.pixmap is None:
                self.pixmap = self.root.create_pixmap(self.width, self.height, self.d.screen().root_depth)
                top, bottom = 0, self.height
            else:
                changed = [y for y in range(self.height)
                           if data[y * stride:(y + 1) * stride] != self.shown[y * stride:(y + 1) * stride]]
                if not changed:
                    return
                top, bottom = changed[0], changed[-1] + 1
            xdraw.put_rows(self.d, self.pixmap, data, stride, self.width, top, bottom)
            self.shown = data
            # Set again: the server may have copied the pixmap.
            self.window.change_attributes(background_pixmap=self.pixmap)
            self.window.clear_area(0, top, self.width, bottom - top)

        def resize(self, width, height):
            self.width, self.height = width, height
            self.window.configure(width=width, height=height)
            if self.pixmap:
                self.pixmap.free()
                self.pixmap = None
            self.refresh()

        # In front and behind ------------------------------------------

        def note(self):
            count = programs(self.d, self.root, X)
            text = assets.string('logonui', 29 if count == 1 else 30)
            self.account.note = text.replace('%d', str(count)) if count and text else ''

        def reset(self):
            s = self.screen
            s.selected, s.hover, s.password, s.error, s.prompt = None, None, '', False, False
            self.refresh()

        def front(self):
            self.state, self.saver, self.settle_at = 'front', None, None
            self.reset()
            self.note()
            self.draw()
            self.window.map()
            self.window.configure(stack_mode=X.Above)
            self.d.sync()
            self.grab_since = time.monotonic()
            self.grab()

        def grab(self):
            self.grab_at = None
            keyboard = self.window.grab_keyboard(True, X.GrabModeAsync, X.GrabModeAsync, X.CurrentTime)
            pointer = self.window.grab_pointer(
                True, X.ButtonPressMask | X.ButtonReleaseMask | X.PointerMotionMask, X.GrabModeAsync,
                X.GrabModeAsync, X.NONE, self.cursors['default'][1], X.CurrentTime)
            if keyboard == X.GrabSuccess and pointer == X.GrabSuccess:
                self.window.set_input_focus(X.RevertToPointerRoot, X.CurrentTime)
                self.hand = False
                if not self.grabbed:
                    self.grabbed = True
                    report('locked')
                return
            # Another program holds one, such as an open menu: it lets go when
            # it notices that it lost the focus.
            self.window.set_input_focus(X.RevertToPointerRoot, X.CurrentTime)
            now = time.monotonic()
            self.grab_at = now + (GRAB_FAST if now - self.grab_since < GRAB_HURRY else GRAB_SLOW)

        def ungrab(self):
            self.d.ungrab_keyboard(X.CurrentTime)
            self.d.ungrab_pointer(X.CurrentTime)
            self.grabbed, self.grab_at = False, None

        def saver_window(self, window):
            """Whether WINDOW is a screen saver that covers the screen."""
            try:
                names = window.get_wm_class()
                if not names or not names[0].lower().endswith('.scr') \
                        or window.get_attributes().map_state != X.IsViewable:
                    return False
                geometry = window.get_geometry()
                return geometry.width >= self.width and geometry.height >= self.height
            except Exception:  # It is gone.
                return False

        def find_saver(self):
            return next((w for w in reversed(self.root.query_tree().children) if self.saver_window(w)), None)

        def behind(self, saver):
            """Wait below the screen saver's window, without the keyboard and the pointer."""
            if self.state == 'front':
                self.ungrab()
                self.reset()
            first = self.state != 'behind'
            self.state, self.saver, self.settle_at = 'behind', saver.id, None
            self.window.configure(sibling=saver, stack_mode=X.Below)
            if first:
                self.draw()
                self.window.map()
                self.window.configure(sibling=saver, stack_mode=X.Below)
                report('behind')
            self.d.sync()

        def saver_gone(self):
            if self.grace and time.monotonic() - started < self.grace:
                self.unlock()
            self.front()

        def unlock(self):
            self.window.unmap()
            self.ungrab()
            self.d.sync()
            sys.exit(0)

        def command(self, word):
            if word == 'yield' and self.state == 'front':
                self.grace = 0
                self.ungrab()
                self.reset()
                self.state, self.saver = 'behind', None  # its window raises itself above
                self.draw()
            elif word == 'lock':
                self.grace = 0
            elif word == 'saver-ended' and self.state != 'front':
                self.saver_gone()

        # Input --------------------------------------------------------

        def select(self, index):
            s = self.screen
            s.selected, s.password, s.error, s.caret = index, '', False, True
            s.prompt = index is not None
            self.refresh()

        def submit(self):
            s = self.screen
            if not s.prompt or self.checking:
                return
            s.prompt, s.error = False, False
            password = s.password

            def ask():
                try:
                    self.result = pamauth.check(SERVICE, self.user, password)
                except Exception:
                    traceback.print_exc()
                    self.result = False
                os.write(self.finished[1], b'.')

            self.checking = threading.Thread(target=ask, daemon=True)
            self.checking.start()
            self.refresh()

        def checked(self):
            os.read(self.finished[0], 1)
            self.checking.join()
            self.checking = None
            if self.result:
                self.unlock()
            if self.state != 'front':
                return
            # XP: the balloon says the password was wrong; the box is empty again.
            s = self.screen
            if s.selected is not None:
                s.error, s.prompt, s.password, s.caret = True, True, '', True
                self.refresh()

        def keyboard(self, event):
            ok, keyval, *_ = Gdk.Keymap.get_for_display(gdk).translate_keyboard_state(
                event.detail, Gdk.ModifierType(event.state & 0xff), event.state >> 13 & 3)
            if not ok or self.checking:
                return
            code = Gdk.keyval_to_unicode(keyval)
            if event.state & (X.ControlMask | X.Mod1Mask | X.Mod4Mask):
                code = 0  # Win+L's own L, Alt and Ctrl combinations type nothing (AltGr is Mod5)
            self.key(Gdk.keyval_name(keyval) or '', chr(code) if code else '')

        # Events -------------------------------------------------------

        def event(self, event):
            if event.type == X.KeyPress and self.state == 'front':
                self.keyboard(event)
            elif event.type in (X.ButtonPress, X.ButtonRelease) and self.state == 'front':
                if event.detail == 1 and not self.checking:
                    (self.press if event.type == X.ButtonPress else self.release)(event.root_x, event.root_y)
            elif event.type == X.MotionNotify and self.state == 'front':
                hand = self.motion(event.root_x, event.root_y)
                if hand != self.hand and self.grabbed:
                    self.hand = hand
                    self.d.change_active_pointer_grab(
                        X.ButtonPressMask | X.ButtonReleaseMask | X.PointerMotionMask,
                        self.cursors['pointer' if hand else 'default'][1], X.CurrentTime)
            elif event.type == X.VisibilityNotify and self.state == 'front':
                if event.state != X.VisibilityUnobscured:
                    self.window.configure(stack_mode=X.Above)
            elif event.type == X.ConfigureNotify and event.window.id == self.root.id:
                if (event.width, event.height) != (self.width, self.height):
                    self.resize(event.width, event.height)
            elif event.type in (X.MapNotify, X.ConfigureNotify) and event.window.id != self.window.id:
                if self.saver != event.window.id and self.saver_window(event.window):
                    self.behind(event.window)
            elif event.type in (X.UnmapNotify, X.DestroyNotify) and event.window.id == self.saver:
                if self.grace and time.monotonic() - started < self.grace:
                    # Wine may only be mapping it anew: look again shortly.
                    self.settle_at = time.monotonic() + SETTLE
                else:
                    self.front()

        def run(self):
            if args.behind:
                self.draw()  # ready to be shown at once
                saver = self.find_saver()
                if saver:
                    self.behind(saver)
            else:
                self.front()
            commands = sys.stdin.fileno()
            os.set_blocking(commands, False)
            buffer = b''
            while True:
                while self.d.pending_events():
                    self.event(self.d.next_event())
                now = time.monotonic()
                if self.grab_at and now >= self.grab_at:
                    self.grab()
                if self.settle_at and now >= self.settle_at:
                    self.settle_at = None
                    saver = self.find_saver()
                    if saver:
                        self.behind(saver)
                    else:
                        self.saver_gone()
                # The caret blinks only while the password box shows.
                s = self.screen
                if self.state == 'front' and s.selected is not None and s.prompt:
                    if self.blink_at is None:
                        self.blink_at = now + CARET_BLINK / 1000
                    elif now >= self.blink_at:
                        self.blink_at = now + CARET_BLINK / 1000
                        s.caret = not s.caret
                        self.dirty = True
                else:
                    self.blink_at = None
                if self.dirty and self.state == 'front':
                    self.draw()
                self.d.flush()
                if self.d.pending_events():
                    continue
                times = [t for t in (self.grab_at, self.settle_at, self.blink_at) if t]
                fds = [self.d.fileno(), self.finished[0]] + ([commands] if commands is not None else [])
                ready = select.select(fds, [], [], max(0, min(times) - time.monotonic()) if times else None)[0]
                if self.finished[0] in ready:
                    self.checked()
                if commands in ready:
                    data = os.read(commands, 256)
                    if not data:
                        # The supervisor is gone: stay locked, in front.
                        commands = None
                        if self.state != 'front':
                            self.grace = 0
                            self.front()
                    buffer += data
                    *lines, buffer = buffer.split(b'\n')
                    for line in lines:
                        self.command(line.decode(errors='replace').strip())

    Lock().run()


if __name__ == '__main__':
    sys.exit(main())
