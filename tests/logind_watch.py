#!/usr/bin/env python3
"""session/logind-watch.py against a stand-in for logind on a private bus: the
Lock signal, sleep delayed until the screen is locked or three seconds pass.

    tests/logind_watch.py

Needs dbus-daemon and python3-gi; no logind and no system bus.
"""
import os
from pathlib import Path
import select
import subprocess
import sys
import threading
import time

from gi.repository import Gio, GLib

ROOT = Path(__file__).resolve().parents[1]
NAME, PATH, SESSION_PATH = 'org.freedesktop.login1', '/org/freedesktop/login1', '/org/freedesktop/login1/session/_1'
XML = '''<node><interface name="org.freedesktop.login1.Manager">
<method name="GetSession"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
<method name="Inhibit"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/>
<arg type="s" direction="in"/><arg type="h" direction="out"/></method>
</interface></node>'''

inhibitors = []  # the read end of each inhibitor's pipe: at its end when released


def call(connection, sender, path, interface, method, parameters, invocation):
    if method == 'GetSession':
        assert parameters.unpack() == ('1',)
        invocation.return_value(GLib.Variant('(o)', (SESSION_PATH,)))
    else:
        assert parameters.unpack()[0] == 'sleep' and parameters.unpack()[3] == 'delay'
        read, write = os.pipe()
        descriptors = Gio.UnixFDList.new()
        index = descriptors.append(write)
        os.close(write)
        inhibitors.append(read)
        invocation.return_value_with_unix_fd_list(GLib.Variant('(h)', (index,)), descriptors)


def released(inhibitor, timeout):
    return bool(select.select([inhibitor], [], [], timeout)[0]) and os.read(inhibitor, 1) == b''


def check(what, condition):
    print('ok' if condition else 'FAILED', what, flush=True)
    if not condition:
        raise SystemExit(1)


daemon = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address'], stdout=subprocess.PIPE,
                          text=True)
watch = None
try:
    address = daemon.stdout.readline().strip()
    context = GLib.MainContext.default()
    loop = GLib.MainLoop()
    bus = Gio.DBusConnection.new_for_address_sync(
        address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
        None, None)
    bus.register_object(PATH, Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0], call, None, None)
    bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'RequestName',
                  GLib.Variant('(su)', (NAME, 0)), None, Gio.DBusCallFlags.NONE, -1, None)
    threading.Thread(target=loop.run, daemon=True).start()

    watch = subprocess.Popen([sys.executable, str(ROOT / 'session/logind-watch.py')], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, bufsize=0,
                             env=dict(os.environ, DBUS_SYSTEM_BUS_ADDRESS=address, XDG_SESSION_ID='1'))

    def line(timeout):
        if not select.select([watch.stdout], [], [], timeout)[0]:
            return None
        return watch.stdout.readline().decode().strip()

    def signal(path, interface, member, parameters=None):
        bus.emit_signal(None, path, interface, member, parameters)
        bus.flush_sync(None)

    def sleep(going):
        signal(PATH, NAME + '.Manager', 'PrepareForSleep', GLib.Variant('(b)', (going,)))

    end = time.monotonic() + 5
    while not inhibitors and time.monotonic() < end:
        time.sleep(0.05)
    check('takes a delay inhibitor', len(inhibitors) == 1)
    time.sleep(0.3)
    signal(SESSION_PATH, NAME + '.Session', 'Lock')
    check('Lock signal: lock', line(3) == 'lock')
    watch.stdin.write(b'locked\n')
    check('which keeps the inhibitor', not released(inhibitors[0], 0.5))

    sleep(True)
    check('before sleep: lock', line(3) == 'lock')
    check('and waits for the screen', not released(inhibitors[0], 0.5))
    watch.stdin.write(b'locked\n')
    check('locked: the machine may sleep', released(inhibitors[0], 1))
    sleep(False)
    time.sleep(0.5)
    check('after waking: a new inhibitor', len(inhibitors) == 2)

    sleep(True)
    check('before sleep again: lock', line(3) == 'lock')
    start = time.monotonic()
    check('no answer: sleeps after three seconds', released(inhibitors[1], 6) and
          2.5 < time.monotonic() - start < 4)
    watch.stdin.close()
    check('ends with its input', watch.wait(timeout=3) == 0)
finally:
    if watch and watch.poll() is None:
        watch.kill()
    daemon.terminate()
