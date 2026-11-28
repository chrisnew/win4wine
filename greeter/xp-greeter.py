#!/usr/bin/env python3
"""A LightDM greeter that looks like XP's Welcome screen.

LightDM and PAM log the user in; this program only draws the screen, from the
user's own XP files (greeter/install.sh copies them). When those files, GTK or
LightDM's GObject bindings are missing, it starts the usual greeter instead
(/etc/win4wine/greeter.json "fallback", lightdm-gtk-greeter by default).
"""
import json
import os
from pathlib import Path
import shutil
import sys
import time
import traceback

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from welcomeinput import CARET_BLINK, Input
TOOLTIP_DELAY = 500    # ms, Windows' default initial delay
FADE_DELAY, FADE_TIME = 1.0, 2.5  # s; XP starts fading the screen about a second after the dialog


def log(message):
    print(f'xp-greeter: {message}', file=sys.stderr, flush=True)


def fallback(reason):
    """Hand the greeter's place to the usual greeter (before talking to LightDM)."""
    try:
        name = json.loads(Path('/etc/win4wine/greeter.json').read_text()).get('fallback')
    except (OSError, ValueError):
        name = None
    name = name or 'lightdm-gtk-greeter'
    command = [name]
    try:
        for line in (Path('/usr/share/xgreeters') / f'{name}.desktop').read_text().splitlines():
            if line.startswith('Exec='):
                command = line[5:].split()
                break
    except OSError:
        pass
    # LightDM finds greeters in sbin too, which the greeter's PATH lacks.
    search = os.pathsep.join((os.environ.get('PATH', ''), '/usr/local/sbin', '/usr/sbin', '/sbin'))
    program = shutil.which(command[0], path=search) or command[0]
    log(f'{reason}; starting {" ".join(command)}')
    os.execv(program, command)


class Greeter(Input):
    def __init__(self, config, assets):
        import welcome
        self.welcome = welcome
        self.assets = assets
        self.config = config
        self.lightdm = LightDM.Greeter()
        users = LightDM.UserList.get_instance().get_users()
        if not users:
            raise RuntimeError('LightDM lists no users')
        self.accounts = users
        default = welcome.default_picture(config, assets)
        self.screen = welcome.Screen(assets, [
            welcome.User(u.get_name(), u.get_display_name() or u.get_name(), self.picture(u, default))
            for u in users], dpi=welcome.config_dpi(config))
        self.screen.prompt = False
        self.screen.can_standby = LightDM.get_can_suspend()
        self.message = None
        self.fade_start = None
        self.pointer = (0, 0)
        self.tooltip_timer = None
        self.autologin_session = None
        self.lightdm.connect('show-prompt', self.on_prompt)
        self.lightdm.connect('show-message', self.on_message)
        self.lightdm.connect('authentication-complete', self.on_complete)
        self.lightdm.connect('autologin-timer-expired', self.on_autologin)
        self.lightdm.connect_to_daemon_sync()

        root = Gdk.get_default_root_window()
        self.width, self.height = root.get_width(), root.get_height()
        self.window = Gtk.Window(type=Gtk.WindowType.TOPLEVEL)
        self.window.set_decorated(False)
        self.window.set_app_paintable(True)
        self.window.move(0, 0)
        self.window.set_default_size(self.width, self.height)
        self.area = Gtk.DrawingArea()
        self.area.set_can_focus(True)
        self.area.add_events(Gdk.EventMask.POINTER_MOTION_MASK | Gdk.EventMask.BUTTON_PRESS_MASK |
                             Gdk.EventMask.BUTTON_RELEASE_MASK | Gdk.EventMask.KEY_PRESS_MASK)
        self.area.connect('draw', self.on_draw)
        self.area.connect('motion-notify-event', self.on_motion)
        self.area.connect('button-press-event', self.on_press)
        self.area.connect('button-release-event', self.on_release)
        self.area.connect('key-press-event', self.on_key)
        self.window.add(self.area)
        self.frame = None
        self.cursors = {name: Gdk.Cursor.new_from_name(Gdk.Display.get_default(), name)
                        for name in ('default', 'pointer')}
        self.window.show_all()
        self.window.get_window().focus(Gdk.CURRENT_TIME)
        self.area.grab_focus()
        GLib.timeout_add(CARET_BLINK, self.blink)

    @staticmethod
    def picture(user, default):
        path = user.get_image()
        if path and os.path.isfile(path) and os.access(path, os.R_OK):
            return path
        return default

    # Drawing --------------------------------------------------------

    def refresh(self):
        self.frame = None
        self.area.queue_draw()

    def fade(self):
        if not self.screen.dialog or self.fade_start is None:
            return 1.0
        return max(0.0, min(1.0, (time.monotonic() - self.fade_start - FADE_DELAY) / FADE_TIME))

    def on_draw(self, area, cr):
        if self.frame is None:
            self.frame = self.screen.render(self.width, self.height, self.fade())
        cr.set_source_surface(self.frame, 0, 0)
        cr.paint()
        return True

    def blink(self):
        if self.screen.selected is not None and self.screen.prompt and not self.screen.dialog:
            self.screen.caret = not self.screen.caret
            self.refresh()
        return True

    def tick(self):
        self.refresh()
        return self.screen.dialog and self.fade() < 1

    # Authentication -------------------------------------------------

    def on_autologin(self, greeter):
        if self.screen.selected is not None or greeter.get_in_authentication():
            return
        username = greeter.get_autologin_user_hint()
        for index, account in enumerate(self.accounts):
            if account.get_name() == username:
                self.screen.selected = index
                self.autologin_session = greeter.get_autologin_session_hint()
                greeter.authenticate_autologin()
                return

    def select(self, index):
        if self.lightdm.get_in_authentication():
            self.lightdm.cancel_authentication()
        s = self.screen
        s.selected, s.password, s.error, s.prompt, s.caret = index, '', False, False, True
        self.message = None
        self.autologin_session = None
        if index is not None:
            self.lightdm.cancel_autologin()
            self.lightdm.authenticate(self.accounts[index].get_name())
        self.refresh()

    def on_prompt(self, greeter, text, kind):
        if kind == LightDM.PromptType.SECRET:
            self.screen.prompt = True
            self.screen.password = ''
            self.refresh()
        else:
            # A question other than the password (we always name the user): answer nothing.
            log(f'unexpected prompt {text!r}')
            self.lightdm.cancel_authentication()

    def on_message(self, greeter, text, kind):
        self.message = text

    def on_complete(self, greeter):
        index = self.screen.selected
        if index is None:
            return
        if self.lightdm.get_is_authenticated():
            self.log_on(index)
            return
        # XP: the balloon says the password was wrong; the box is empty again.
        self.autologin_session = None
        self.screen.error = True
        self.screen.password = ''
        self.screen.prompt = False
        self.lightdm.authenticate(self.accounts[index].get_name())
        self.refresh()

    def submit(self):
        if self.screen.prompt and self.lightdm.get_in_authentication():
            self.screen.prompt = False
            self.lightdm.respond(self.screen.password)
            self.refresh()

    def log_on(self, index):
        s = self.screen
        s.mode, s.status = 'logon', self.assets.string('winlogon', 1682)
        s.hover = None
        self.refresh()
        surface = s.render(self.width, self.height)
        try:
            from Xlib import display
            import xdraw
            d = display.Display()
            xdraw.set_root_background(d, surface)
            d.close()
        except Exception:
            traceback.print_exc()
        session = (self.autologin_session or self.accounts[index].get_session() or
                   self.lightdm.get_default_session_hint())
        try:
            self.lightdm.start_session_sync(session)
        except GLib.Error as e:
            log(f'cannot start session {session}: {e.message}')
            s.mode, s.status = 'list', ''
            self.select(None)

    # Input (welcomeinput.py; here also the shutdown dialog) ----------

    def target(self, x, y):
        """As welcomeinput.Input, and ('dialog', name) while the dialog is open."""
        s = self.screen
        if s.dialog:
            rects = s.dialog_rects(self.width, self.height)
            for name in ('standby', 'off', 'restart', 'cancel'):
                bx, by, bw, bh = rects[name]
                if bx <= x < bx + bw and by <= y < by + bh:
                    if name == 'standby' and not s.can_standby:
                        return None
                    return ('dialog', name)
            return None
        return super().target(x, y)

    def motion(self, x, y):
        s = self.screen
        target = self.target(x, y)
        dialog_hover = target[1] if target and target[0] == 'dialog' else None
        self.pointer = (int(x), int(y))
        if dialog_hover != s.dialog_hover:
            # The dialog's buttons explain themselves after the pointer rests on them.
            if self.tooltip_timer:
                GLib.source_remove(self.tooltip_timer)
                self.tooltip_timer = None
            s.tooltip = None
            if dialog_hover in ('standby', 'off', 'restart'):
                self.tooltip_timer = GLib.timeout_add(TOOLTIP_DELAY, self.show_tooltip, dialog_hover)
            s.dialog_hover = dialog_hover
            self.refresh()
        return super().motion(x, y)

    def press(self, x, y):
        super().press(x, y)
        if self.pressed and self.pressed[0] == 'dialog':
            self.screen.dialog_pressed = self.pressed[1]
            self.refresh()

    def release(self, x, y):
        self.screen.dialog_pressed = None
        super().release(x, y)

    def activate(self, target):
        if target[0] == 'power':
            self.open_dialog()
        elif target[0] == 'dialog':
            self.dialog_action(target[1])

    def key(self, name, character):
        s = self.screen
        if not s.dialog:
            super().key(name, character)
            return
        order = [n for n in ('standby', 'off', 'restart') if n != 'standby' or s.can_standby]
        if name == 'Escape':
            self.dialog_action('cancel')
        elif name in ('Left', 'Right', 'Tab', 'ISO_Left_Tab'):
            step = -1 if name in ('Left', 'ISO_Left_Tab') else 1
            current = order.index(s.dialog_focus) if s.dialog_focus in order else 0
            s.dialog_focus = order[(current + step) % len(order)]
        elif name in ('Return', 'KP_Enter', 'space'):
            self.dialog_action(s.dialog_focus)
        else:
            mnemonics = {'s': 'standby', 'u': 'off', 'r': 'restart'}
            if name.lower() in mnemonics and mnemonics[name.lower()] in order:
                self.dialog_action(mnemonics[name.lower()])
        self.refresh()

    def show_tooltip(self, name):
        self.tooltip_timer = None
        if self.screen.dialog and self.screen.dialog_hover == name:
            self.screen.tooltip = (name, *self.pointer)
            self.refresh()
        return False

    # The events GTK delivers ----------------------------------------

    def on_motion(self, area, event):
        hand = self.motion(event.x, event.y)
        self.window.get_window().set_cursor(self.cursors['pointer' if hand else 'default'])
        return True

    def on_press(self, area, event):
        if event.button == 1:
            self.press(event.x, event.y)
        return True

    def on_release(self, area, event):
        if event.button == 1:
            self.release(event.x, event.y)
        return True

    def on_key(self, area, event):
        code = Gdk.keyval_to_unicode(event.keyval)
        self.key(Gdk.keyval_name(event.keyval) or '', chr(code) if code else '')
        return True

    def open_dialog(self):
        s = self.screen
        s.dialog, s.dialog_focus, s.dialog_hover = True, 'off', None
        s.layers.clear()
        self.fade_start = time.monotonic()
        GLib.timeout_add(100, self.tick)
        self.refresh()

    def dialog_action(self, name):
        s = self.screen
        s.tooltip = None
        if name == 'cancel':
            s.dialog = False
            self.fade_start = None
        elif name == 'standby' and s.can_standby:
            s.dialog = False
            LightDM.suspend()
        elif name == 'off':
            LightDM.shutdown()
        elif name == 'restart':
            LightDM.restart()
        self.refresh()


def main():
    try:
        import welcome
        cache = Path(os.environ.get('HOME') or '/var/lib/lightdm') / '.cache/xp-greeter'
        config, assets = welcome.setup(cache)
        # XP's cursors (greeter/cursors.py), for this window and the root.
        theme = config.get('cursor_theme', 'win4wine-xp')
        if theme and (Path('/usr/share/icons') / theme / 'cursors/left_ptr').is_file():
            # XP's cursors double above 144 DPI.
            os.environ.update(XCURSOR_THEME=theme, XCURSOR_SIZE='64' if welcome.config_dpi(config) > 144 else '32')
        import gi
        gi.require_version('Gtk', '3.0')
        gi.require_version('Gdk', '3.0')
        gi.require_version('LightDM', '1')
        from gi.repository import Gdk as gdk, GLib as glib, Gtk as gtk, LightDM as lightdm
        globals().update(Gdk=gdk, GLib=glib, Gtk=gtk, LightDM=lightdm)
    except Exception as e:
        traceback.print_exc()
        fallback(f'the Welcome screen is not available ({e})')
    try:
        greeter = Greeter(config, assets)
    except Exception as e:
        # Before LightDM's connection is used, the usual greeter can still take over.
        traceback.print_exc()
        fallback(f'cannot start ({e})')
    Gtk.main()


if __name__ == '__main__':
    main()
