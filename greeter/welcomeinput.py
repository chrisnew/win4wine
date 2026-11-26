"""The mouse and keyboard of XP's Welcome screen, for the greeter and the lock screen.

It knows no toolkit: the program passes on pointer positions and key names.
The class that uses it has the welcome.Screen as self.screen and its size as
self.width and self.height, and provides refresh(), select(index) and
submit()."""

CARET_BLINK = 530      # ms, Windows' default
LONGEST = 127          # characters of a password, as XP's box takes


class Input:
    pressed = None

    def target(self, x, y):
        """What is under the pointer: ('user', i), ('go',), ('power',) or None."""
        s = self.screen
        if s.mode != 'list':
            return None
        if s.power:
            px, py, pw, ph = s.power_rect(self.width, self.height)
            if px <= x < px + pw and py <= y < py + ph:
                return ('power',)
        if s.selected is not None and s.prompt:
            gx, gy, gw, gh = s.password_rects(self.width, self.height)['go']
            if gx <= x < gx + gw and gy <= y < gy + gh:
                return ('go',)
        index = s.item_at(self.width, self.height, x, y)
        return ('user', index) if index is not None else None

    def motion(self, x, y):
        """The pointer moved; whether it is now a hand."""
        s = self.screen
        target = self.target(x, y)
        hover = target[1] if target and target[0] == 'user' else None
        power = target == ('power',)
        if (hover, power) != (s.hover, s.power_hover):
            s.hover, s.power_hover = hover, power
            self.refresh()
        return target is not None and target != ('user', s.selected)

    def press(self, x, y):
        """The left button went down."""
        self.pressed = self.target(x, y)

    def release(self, x, y):
        """The left button came up: a click where it also went down."""
        s = self.screen
        target, pressed = self.target(x, y), self.pressed
        self.pressed = None
        if target != pressed:
            self.refresh()
            return
        if target is None:
            if not s.dialog and s.selected is not None and s.item_at(self.width, self.height, x, y) is None:
                self.select(None)
        elif target[0] == 'user':
            if target[1] != s.selected:
                self.select(target[1])
        elif target[0] == 'go':
            self.submit()
        else:
            self.activate(target)
        self.refresh()

    def activate(self, target):
        """A click on a target that only the greeter has."""

    def key(self, name, character):
        """A key by its X keysym name, with the character it types ('' for none)."""
        s = self.screen
        if s.mode != 'list':
            return
        if s.selected is not None and s.prompt:
            if name in ('Return', 'KP_Enter'):
                self.submit()
            elif name == 'Escape':
                self.select(None)
            elif name == 'BackSpace':
                s.password = s.password[:-1]
            elif character and character.isprintable() and len(s.password) < LONGEST:
                s.password += character
                s.error = False
            s.caret = True
            self.refresh()
            return
        if name in ('Up', 'Down'):
            count = len(s.users)
            current = s.hover if s.hover is not None else (-1 if name == 'Down' else count)
            s.hover = max(0, min(count - 1, current + (1 if name == 'Down' else -1)))
            self.refresh()
        elif name in ('Return', 'KP_Enter', 'space') and (s.hover is not None or len(s.users) == 1):
            self.select(s.hover or 0)
        elif name == 'Escape' and s.selected is not None:
            self.select(None)
