#!/usr/bin/env python3
"""Ask for the lock screen when logind wants the session locked, or before sleep.

The session supervisor runs this (session/screen.py). It prints "lock" on the
session's Lock signal (loginctl lock-session) and before the machine suspends
(a closed lid, Stand By). Until it reads "locked" on stdin, or three seconds
pass, it delays the suspend with a logind inhibitor, so that the Welcome
screen is there when the machine wakes. It ends when stdin does.
"""
import os
import sys

from gi.repository import Gio, GLib

NAME, PATH = 'org.freedesktop.login1', '/org/freedesktop/login1'
MANAGER, SESSION = NAME + '.Manager', NAME + '.Session'
WAIT = 3  # s for the lock screen; logind itself waits 5 at most (InhibitDelayMaxSec)


class Watch:
    def __init__(self, loop):
        self.loop = loop
        self.bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        self.inhibitor = None
        self.timer = None
        session = os.environ.get('XDG_SESSION_ID')
        method, argument = (('GetSession', GLib.Variant('(s)', (session,))) if session else
                            ('GetSessionByPID', GLib.Variant('(u)', (os.getpid(),))))
        path = self.bus.call_sync(NAME, PATH, MANAGER, method, argument, GLib.VariantType('(o)'),
                                  Gio.DBusCallFlags.NONE, -1, None).unpack()[0]
        self.bus.signal_subscribe(NAME, SESSION, 'Lock', path, None, Gio.DBusSignalFlags.NONE, self.on_lock)
        self.bus.signal_subscribe(NAME, MANAGER, 'PrepareForSleep', PATH, None, Gio.DBusSignalFlags.NONE,
                                  self.on_sleep)
        self.inhibit()
        self.buffer = b''
        GLib.unix_fd_add_full(GLib.PRIORITY_DEFAULT, sys.stdin.fileno(), GLib.IOCondition.IN | GLib.IOCondition.HUP,
                              self.on_input)

    def inhibit(self):
        if self.inhibitor is not None:
            return
        try:
            result, descriptors = self.bus.call_with_unix_fd_list_sync(
                NAME, PATH, MANAGER, 'Inhibit',
                GLib.Variant('(ssss)', ('sleep', 'win4wine', 'Lock the screen before sleep', 'delay')),
                GLib.VariantType('(h)'), Gio.DBusCallFlags.NONE, -1, None, None)
            self.inhibitor = descriptors.get(result.unpack()[0])
        except GLib.Error as e:
            print(f'logind-watch: cannot delay sleep: {e.message}', file=sys.stderr, flush=True)

    def release(self):
        if self.timer:
            GLib.source_remove(self.timer)
            self.timer = None
        if self.inhibitor is not None:
            os.close(self.inhibitor)
            self.inhibitor = None

    def on_lock(self, *_):
        print('lock', flush=True)

    def on_sleep(self, bus, sender, path, interface, signal, parameters):
        if parameters.unpack()[0]:
            if self.inhibitor is not None and not self.timer:
                self.timer = GLib.timeout_add_seconds(WAIT, self.on_timeout)
            print('lock', flush=True)
        else:
            self.inhibit()

    def on_timeout(self):
        print('logind-watch: the screen was not locked in time', file=sys.stderr, flush=True)
        self.timer = None
        self.release()
        return False

    def on_input(self, fd, condition):
        data = os.read(fd, 256)
        if not data:
            self.loop.quit()
            return False
        self.buffer += data
        *lines, self.buffer = self.buffer.split(b'\n')
        # "locked" lets the machine sleep; at other times there is nothing to do.
        if b'locked' in lines and self.timer:
            self.release()
        return True


def main():
    loop = GLib.MainLoop()
    Watch(loop)
    loop.run()


if __name__ == '__main__':
    main()
