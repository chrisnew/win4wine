# Plan: screen savers and a lock screen

Status (2026-11-27): phases 0 to 3 are **deployed on the EeePC** and passed the manual tests there. What happened is in
[tools/eeepc/LOG.md](../tools/eeepc/LOG.md), the numbers in
[tools/eeepc/README.md](../tools/eeepc/README.md#screen-savers-and-the-lock-screen). Next: "Left to do after the deployment".
How it works now: [docs/architecture.md](../docs/architecture.md), "Screen savers and the lock screen"; the other sections below
are the plan as it was.

## Deployment on the EeePC (done 2026-11-27)

State on 2026-11-27: the code is on `origin/eeepc` up to a3abf37 (the commits after it only change this plan). The development
machine's `.local/wine-install` is built with patches up to 0159. Nothing was changed on the EeePC; it stopped answering SSH
(`No route to host`) at the end of that session, so it may need waking. `IDEAS.md` holds the user's own uncommitted notes: leave
it out of commits.

**Rules.** It is the user's live machine: get a go-ahead before step 2, and ask again before ending the session or killing
anything. The user has to be at the EeePC for steps 3 and 7 to 10. `sudo` may want a password (`ssh 192.168.1.9 sudo -n true` tells);
then the user runs the `sudo` steps (`ssh -t`, or at the machine). Keep one SSH shell open from step 4 on: it is the way back if
the login screen or the lock screen traps you.

Commands run on the EeePC in `~/win4wine` unless they say "development machine". Commands that use the display need
`export DISPLAY=:0 XAUTHORITY=~/.Xauthority`.

### 1. Before anything changes (read only)

```sh
git status --short; git rev-parse --short HEAD      # clean? note the commit for the way back
sudo -n true; echo $?; df -h ~
dpkg -l libx11-dev libxext-dev python3-gi python3-gi-cairo gir1.2-gtk-3.0 | grep -c ^ii     # 5
ls /etc/pam.d/win4wine-lock /etc/X11/xorg.conf.d/ 2>&1
localectl status
grep -A1 -i '^\[boot\]' .local/prefix/drive_c/windows/system.ini
```

**Baseline** for "no more wakeups than before": copy the sampling script (below) to `/tmp/w4w-sample.py`, have the user leave the
machine alone in the old session, and keep the output: `python3 /tmp/w4w-sample.py 60 20` (waits 20 s, so that the SSH login's
network traffic is over, then samples 60 s).

### 2. Update the checkout

```sh
git fetch fork && git checkout -B eeepc fork/eeepc
python3 session/keyboard.py env ~/win4wine/.local/prefix        # compare with localectl status
python3 -c "import sys, pathlib; sys.path.insert(0, 'session'); import screen; print(screen.settings(pathlib.Path('.local/prefix')))"
```

- The keyboard line is the layout the **next login will really apply** (the fixed bug in `session/keyboard.py`). If it is not the
  keyboard the user types on, fix it before any password is typed on the lock screen: XP's Regional and Language Options, or
  `localectl`'s layout added to `HKCU\Keyboard Layout\Preload`.
- The settings line: with `secure=True` and a screen saver set, the first idle time after the login locks the screen. Steps 6
  and 7 make sure the password check works before that.

### 3. Log off

The user logs off XP (Start, Log Off) and stays at the login screen. Everything up to step 7 happens logged off.

### 4. Install the new Wine

On the **development machine** (the checkout on `eeepc`, clean apart from `IDEAS.md`):

```sh
wine/container-build.sh --deploy 192.168.1.9
```

It must end with "now has this Wine". If it says the session still runs, log off and run it again. **Do not run it again after it
succeeded**: that would overwrite `.local/wine-install-old` (the way back) with the new Wine.

```sh
cat .local/wine-install/win4wine-patches; python3 wine/patch-state.py --digest wine/patches     # the same
pgrep -x wineserver || echo stopped
```

### 5. Back up the prefix's registry

```sh
b=.local/backup-$(date +%F); mkdir -p $b
cp .local/prefix/*.reg .local/prefix/drive_c/windows/system.ini $b/
cp .local/prefix/.xp-extras $b/ 2>/dev/null; ls -l $b
```

### 6. `setup.sh` (bridge, prefix, greeter, PAM)

```sh
scripts/setup.sh --media ~/Images/en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso --greeter
```

Expected: no new packages (log any it installs); Wine "Built" (**if it starts compiling Wine, stop it**: step 4 did not take);
the bridge builds `wm` and `idle-watch` with no "no screen saver" warning; "Adding the XP Explorer additions" (`extras.py`,
`VERSION` 7); the Welcome screen installed. `extras.py` applies XP's defaults again (sounds, Explorer settings, theme
registrations), so the user's changes to those may come back to XP's defaults; step 5's copies are the way back. This is also the
open check of phase 0 (`setup.sh` on a prefix it updates).

```sh
ls -l /etc/pam.d/win4wine-lock .local/bridge/wm .local/bridge/idle-watch
cat .local/prefix/.xp-extras                                   # starts with 7
```

### 7. Check the password and the login screen before relying on them

The user runs this and types their password; it must print `True`, and `False` for a wrong one (about 2 s):

```sh
python3 -c "import sys, getpass; sys.path.insert(0, 'greeter'); import pamauth; print(pamauth.check('win4wine-lock', getpass.getuser(), getpass.getpass()))"
```

**If it is not `True`, do not go on**: `sudo rm /etc/pam.d/win4wine-lock` (the session then does not lock) and find out why.

The LightDM greeter's input code was refactored and only tried against stand-ins, never under LightDM. With no session running:
`sudo systemctl restart lightdm`. At the login screen try the mouse, the arrow keys, a wrong password and "Turn off computer"
(then Cancel). If it is broken: `sudo greeter/install.sh --remove && sudo systemctl restart lightdm` (the usual greeter; this also
removes the PAM file).

### 8. Log in and check

```sh
pgrep -af 'idle-watc[h]|logind-watc[h]|supervisor\.p[y]'          # all three
tail -n +1 ~/.local/state/win4wine/{idle-watch,logind}.log; grep -i -E 'lock|idle|logind' ~/.xsession-errors | tail
DISPLAY=:0 XAUTHORITY=~/.Xauthority xset q | tail -12           # blanking 0, DPMS off time 0
systemd-inhibit --list | grep -i win4wine                       # the delay lock for sleep
setxkbmap -query
python3 tests/screen_guard.py && python3 tests/logind_watch.py
```

Ways out of a lock screen that does not take the password (ask first):

- ends the whole session: `pkill -TERM -f 'session/supervisor\.p[y]'`
- keeps the session: `sudo mv /etc/pam.d/win4wine-lock /root/ && pkill -f 'xp-loc[k]'` (started again, it finds no PAM file and
  gives up). Put the file back with `greeter/install.sh`.
- switch it off: `"lock_screen": false` (also `"screen_saver": false`) in `~/.config/win4wine/session.json`, then log in again.

### 9. Try it at the machine

| Do | Expect |
|---|---|
| Win+L from the desktop, from Notepad, from a Linux terminal | Welcome screen with "N programs running"; no "l" typed anywhere |
| Click the tile, type a wrong password, then the right one | XP's balloon; then unlocked, the focus back where it was, no Start menu |
| While locked: Alt+Tab, Win+D, volume keys | Nothing gets past (volume keys do nothing: note whether that bothers) |
| `rundll32.exe user32.dll,LockWorkStation` in Start, Run | Locks |
| Screen Saver tab: pick one, wait 1 minute, Apply; reopen the tab | The choice is still shown; it starts after a minute (up to 30 s late right after a change) |
| "On resume, password protect": move the mouse within 5 s, then after more than 5 s | No password; then the Welcome screen, with no glimpse of the desktop |
| A screen saver over the locked screen (lock, wait) | Saver shows; moving the mouse returns to the Welcome screen |
| A game or video in full screen, left alone past the wait time | Note what happens (XP would start the saver) |
| `loginctl lock-session N` (N from `loginctl list-sessions`) | Locks |
| Close the lid, wait, open it | Asleep and awake again, the Welcome screen first, the keyboard works. See `logind.log` |
| Lock, then `pkill -f 'xp-loc[k]'` over SSH | The lock screen comes back |
| Start, Log Off | Log off as before, nothing left running |

Optionally the probe of patches 0158 and 0159 (it puts the settings back):

```sh
i686-w64-mingw32-gcc -std=c11 -O2 tests/screen-saver.c -o /tmp/screen-saver-test.exe -luser32 -ladvapi32
DISPLAY=:0 XAUTHORITY=~/.Xauthority python3 session/wine.py /tmp/screen-saver-test.exe
```

### 10. Measure

All with `export DISPLAY=:0 XAUTHORITY=~/.Xauthority`; the screenshots with `tools/eeepc/shot.py`.

1. **Idle**, as in step 1: `python3 /tmp/w4w-sample.py 60 20` with the wait time longer than 2 minutes. No more wakeups than the
   baseline; `idle-watch`, `logind-watch` and `supervisor.py` near 0.
2. **Screen savers**: the saver script (below) as `/tmp/w4w-savers.py`, run from `~/win4wine`. It gives each one's CPU, whether its
   window covers the screen and whether mouse movement ends it. Frame rate by eye, or `tools/eeepc/capture.py`.
3. **Time to lock**: the lock script (below), `python3 /tmp/w4w-locktime.py`; the user unlocks after each of 3 runs. **Over about
   1 s: start the lock screen ahead of time** (a standby mode in `greeter/xp-lock.py` that has drawn and waits on stdin for "lock",
   started by `session/screen.py` after the shell; check its memory in the sample's RSS column).
4. **CPU while the caret blinks**: lock, click the tile, then `python3 /tmp/w4w-sample.py 20 5`; see `xp-lock.py` and `Xorg`.

### 11. Record

- `tools/eeepc/LOG.md`, an entry for the deployment (what, why, result with the numbers, undo) and "Current state": the Wine row
  (patches up to 0159), a row for `/etc/pam.d/win4wine-lock`, the reinstalled greeter, the prefix's `extras.py` `VERSION` 7 (which
  replaces the row about the screen savers copied by hand), and any package `setup.sh` installed.
- Numbers in `tools/eeepc/README.md`; known problems in `BUGS.md` (local only); this plan's status.
- Commit and push (`eeepc`).

### The way back

```sh
# logged off, wineserver stopped
git checkout -B eeepc <commit from step 1>
rm -rf .local/wine-install && mv .local/wine-install-old .local/wine-install
cp .local/backup-<date>/*.reg .local/prefix/ && cp .local/backup-<date>/system.ini .local/prefix/drive_c/windows/
cp .local/backup-<date>/.xp-extras .local/prefix/ 2>/dev/null
bridge-wm/build.sh && rm -f .local/bridge/idle-watch
greeter/install.sh .local/assets && sudo rm -f /etc/pam.d/win4wine-lock      # the old greeter
```

To switch off only the new features, the two `session.json` settings above are enough.

### Scripts for steps 1 and 10

`/tmp/w4w-sample.py SECONDS [DELAY]`: CPU, wakeups and memory of the user's processes and Xorg.

```python
import os, sys, time

seconds, delay = float(sys.argv[1]), float(sys.argv[2]) if len(sys.argv) > 2 else 0


def sample():
    found = {}
    for pid in filter(str.isdigit, os.listdir('/proc')):
        try:
            base = f'/proc/{pid}'
            command = open(base + '/cmdline', 'rb').read().replace(b'\0', b' ').decode(errors='replace').strip()
            if os.stat(base).st_uid != os.getuid() and 'Xorg' not in command or int(pid) == os.getpid():
                continue
            stat = open(base + '/stat').read().rsplit(') ', 1)[1].split()
            switches = 0
            for task in os.listdir(base + '/task'):
                with open(f'{base}/task/{task}/status') as status:
                    switches += sum(int(line.split()[1]) for line in status if 'ctxt_switches' in line)
            rss = int(stat[21]) * os.sysconf('SC_PAGE_SIZE') // 1048576
            found[pid] = (int(stat[11]) + int(stat[12]), switches, rss, command[-70:])
        except OSError:
            pass
    return found


time.sleep(delay)
before = sample()
time.sleep(seconds)
after = sample()
rows = [((after[p][0] - before[p][0]) / os.sysconf('SC_CLK_TCK') / seconds * 100,
         (after[p][1] - before[p][1]) / seconds, after[p][2], p, after[p][3]) for p in after if p in before]
print(f'all: {sum(r[0] for r in rows):.1f}% of one CPU, {sum(r[1] for r in rows):.0f} wakeups/s')
print('  CPU%  wakes/s  RSS MB     pid  command')
ours = ('idle-watch', 'logind-watch', 'supervisor.py', 'xp-lock')
for row in sorted(rows, reverse=True, key=lambda r: (r[0], r[1])):
    if row[0] >= 0.5 or row[1] >= 2 or any(name in row[4] for name in ours):
        print('{:6.1f} {:8.1f} {:7} {:>7}  {}'.format(*row))
```

`/tmp/w4w-savers.py`, run from `~/win4wine` (it moves the pointer; nobody touches the machine meanwhile):

```python
import os, subprocess, sys, time
from Xlib import X, display
from Xlib.ext import xtest

d = display.Display()
root = d.screen().root
screen = root.get_geometry()


def move(x):
    xtest.fake_input(d, X.MotionNotify, x=x, y=x // 2)
    d.sync()


def window(name):
    shown = []  # the viewable top-level windows, bottom to top
    for w in root.query_tree().children:
        try:
            if w.get_attributes().map_state == X.IsViewable:
                shown.append(w)
        except Exception:
            pass
    for position, w in enumerate(shown):
        try:
            names = w.get_wm_class()
            if names and names[0].lower() == name:
                g = w.get_geometry()
                return f'{g.width}x{g.height} at {g.x},{g.y}, {len(shown) - position - 1} windows above'
        except Exception:
            pass
    return 'no window'


for name in sys.argv[1:] or ['logon', 'scrnsave', 'ssstars', 'ssmarque', 'ssmyst', 'ssbezier', 'ssmypics',
                             'sspipes', 'ss3dfo', 'sstext3d', 'ssflwbox']:
    move(300)
    saver = subprocess.Popen([sys.executable, 'session/wine.py', rf'C:\windows\syswow64\{name}.scr', '/s'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(15)
    print(f'== {name}: screen {screen.width}x{screen.height}; {window(name + ".scr")}', flush=True)
    subprocess.run([sys.executable, '/tmp/w4w-sample.py', '10'])
    for x in (400, 600, 500):
        move(x)
        time.sleep(0.3)
    try:
        saver.wait(timeout=10)
        print('ended by the mouse', flush=True)
    except subprocess.TimeoutExpired:
        print('DID NOT END: end it by hand', flush=True)
        break
    time.sleep(3)
```

`/tmp/w4w-locktime.py` (it locks the screen):

```python
import glob, os, time
from Xlib import X, display

d = display.Display()
root = d.screen().root


def shown():
    for w in root.query_tree().children:
        try:
            if w.get_wm_name() == 'win4wine-lock' and w.get_attributes().map_state == X.IsViewable:
                return True
        except Exception:
            pass
    return False


ipc = glob.glob(f'/run/user/{os.getuid()}/win4wine-{os.getuid()}-*')[0]
assert not shown(), 'locked already'
time.sleep(5)
start = time.monotonic()
with open(ipc + '/lock-request.tmp', 'w') as request:
    request.write('lock\n')
os.replace(ipc + '/lock-request.tmp', ipc + '/lock-request')
while not shown():
    assert time.monotonic() - start < 30, 'not locked after 30 s: see ~/.local/state/win4wine/lock.log'
    time.sleep(0.05)
print(f'lock screen after {time.monotonic() - start:.2f} s')
```

## Left to do after the deployment

- **Lock screen started ahead of time** (`greeter/xp-lock.py --standby`): written, **not yet on the EeePC**. Locking took 2.2 s
  there, over the 1 s limit of step 10: starting Python and its imports 0.7 s, the first drawing 0.8 s. The waiting one has done
  those and holds no X connection; opening the display, drawing with the program count and the upload are left, about 0.4 s by
  the same profile. In the container: 51 MB resident, no context switches while it waits, shown 0.09 s after "start" (0.2 s when
  started on demand). To check on the EeePC: the time to lock, the memory, and the first lock after hours idle (swapped pages
  come from the hard disk). It needs only the checkout and a relogin.
- The Windows XP logo screen saver shows only black ([BUGS.md](../BUGS.md)).
- Decide whether a full-screen game should hold the screen saver off. On the EeePC the saver and then the lock screen came over
  VS Code left alone, and XP would start it too. A full-screen game or video was not tried (see also phase 4).
- Not measured: the screen savers' frame rates. Not run: the probe `tests/screen-saver.c` on the EeePC.
- Fast User Switching is still off, so the checkbox reads "On resume, password protect". Turning it on would also add "Switch
  User" to XP's Log Off dialog, which has nothing behind it yet.
- Not done: hardening against VT switches (2.5, only if wanted), "Switch User" (3.4), phase 4 (only if needed).

## Found while building it

- XP's Explorer does not register Win+L (phase 3.1), so the WM takes it, and patch 0159 keeps the "l" from Windows programs.
- XP's tab writes `SCRNSAVE.EXE` to `system.ini` and reads it from the registry. Wine supports Windows' `IniFileMapping`; the
  prefix lacked the entry (`prefix/extras.py`).
- The tab's `system32` is `syswow64` for a program started from Linux.
- Wine writes `user.reg` up to 30 seconds after a change, so a changed setting takes that long.
- In the container, XP's popup menus show only their shadows on Xvfb, with the old WM too; unrelated, and not seen on the EeePC.

Goal: XP's own screen savers start after the idle time set in Display Properties, and Win+L (and "On resume, display Welcome
screen") shows XP's Welcome screen as a lock screen inside the session, without a VT switch. On the EeePC it must be instant and must
not add wakeups while the machine is idle.

## What exists, and what is missing

- XP's Display Properties (desk.cpl, themeui.dll) run in the session, Screen Saver tab included. Until now only `logon.scr`
  ("Windows XP") was extracted, into `C:\xp\native`, where the tab does not look.
- Wine stores `ScreenSaveActive` and `ScreenSaveTimeOut` (win32u `sysparams.c`), but nothing starts a screen saver.
  - winex11 answers `SPI_GETSCREENSAVEACTIVE` and `SPI_SETSCREENSAVEACTIVE` with the X server's own blanking timer
    (`XGetScreenSaver`/`XSetScreenSaver`, `x11drv_main.c`). The tab would show and switch Xorg's blanker, not ours.
  - `DefWindowProc`'s `SC_SCREENSAVE` does nothing (`win32u/defwnd.c`). `SetThreadExecutionState` is a stub.
  - Only dinput posts `WM_WINE_NOTIFY_ACTIVITY`, which calls `XResetScreenSaver` (a joystick counts as activity in Wine games).
- `LockWorkStation` is a stub that returns FALSE (`user32/user_main.c`).
- `greeter/welcome.py` draws XP's Welcome screen. `greeter/xp-splash.py` shows it in the session as an override-redirect window
  that stays on top. `greeter/xp-greeter.py` handles the input (user tile, password field, blinking caret, keys), wired to LightDM.
- The supervisor sleeps in `select()` on the display, a wakeup pipe and pidfds (`Session.wait`), so a pipe from a helper fits in.
  `session/keyboard.py` already reads values from the prefix's `user.reg`.

## Decisions

- **Lock screen: inside the session (option B)**, not LightDM's `dm-tool lock`. A VT switch with a second X server and a Python/GTK
  greeter would take seconds on the Atom and flicker, while XP's lock is instant. The cost is the usual X lock-screen limit: if it
  crashes, the session is open. The supervisor restarts it while the lock is on, and the WM keeps it on top.
- **No Microsoft code handles the password**, as with the greeter: PAM checks it. msgina and logonui only supply images and strings.
- **Idle detection in a small C helper** (`session/idle-watch.c`), not in the WM, so `wm.c` and `wm.py` do not both have to do it.
  It uses XSync's `IDLETIME` counter with alarms, so there are no wakeups while the machine is idle.
- **XP's look of Fast User Switching**: the Welcome screen with the user's tile and "N programs running", not the classic
  "Computer Locked" dialog.

## Phase 0: the screen saver files (done)

- `extract/extract.py`: new group `screensavers` with `scrnsave.scr` (Blank), `ss3dfo.scr` (3D Flying Objects), `ssbezier.scr`
  (Beziers), `ssflwbox.scr` (3D FlowerBox), `ssmarque.scr` (Marquee), `ssmypics.scr` (My Pictures Slideshow), `ssmyst.scr`
  (Mystify), `sspipes.scr` (3D Pipes), `ssstars.scr` (Starfield) and `sstext3d.scr` (3D Text). It is in `ADDED_GROUPS`, so
  `setup.sh` adds it to an earlier extraction by itself (all ten are in I386 as `.SC_` on the XP SP2 CD as well).
- `prefix/extras.py`: copies them and `cpl/logon.scr` into `drive_c/windows/syswow64`, the 32-bit system directory the tab lists.
  `VERSION` 5 → 6, so `setup.sh` applies it to the existing prefix.
- Checked on the EeePC (2026-11-27, files copied by hand, see `tools/eeepc/LOG.md`): all ten come out of the SP3 ISO, and the tab
  lists all eleven by name. The small monitor's preview (`/p HWND`) of Windows XP works; for a moment it shows a stale piece of
  the screen first. The checkbox reads "On resume, password protect", not "On resume, display Welcome screen": XP shows the second
  only with Fast User Switching on. Phase 2's lock screen is XP's FUS one, so the prefix should turn FUS on (the Winlogon setting
  that XP's User Accounts sets; find out which with a trace) once the lock screen exists.
- Run with `/s` and applied by `setup.sh` to the existing prefix on 2026-11-27 (see "Deployment on the EeePC").

## Phase 1: screen savers start when the machine is idle

1. **Measure first** (decides how much work comes next):
   - Run each `.scr /s` on the EeePC. Record CPU and frame rate (GDI ones: Starfield, Marquee, Mystify, Beziers; OpenGL ones: Pipes,
     Flying Objects, Text, FlowerBox on the GMA 3150). Check that mouse or key input ends each one and that its window covers the
     taskbar. Add the numbers to `tools/eeepc/`.
   - The EeePC has `/etc/X11/xorg.conf.d/10-nodpms.conf` (blanking and DPMS off, reason unknown, see `tools/eeepc/LOG.md`).
     idle-watch must not depend on it being there or not.
2. **Wine patch** (opt-in, XP session): winex11 no longer answers `SPI_GET/SETSCREENSAVEACTIVE` with the X server's timer. win32u's
   registry entry answers instead, so the tab shows and stores `ScreenSaveActive`. In the patch table of `docs/patches.md`.
3. **`session/idle-watch.c`** (built by `bridge-wm/build.sh` as `.local/bridge/idle-watch`):
   - Turns off the X server's own blanking (`XSetScreenSaver` timeout 0), since we replace it.
   - Reads timeouts on stdin (`idle SECONDS`, `dpms SECONDS`, `off`) and writes `idle` and `active` lines on stdout.
   - XSync alarm on `IDLETIME` ≥ timeout → `idle`; then an alarm for the counter going back down → `active`.
   - Linux players that call `XResetScreenSaver` (mpv, VLC) reset `IDLETIME`, so they hold the screen saver off without extra work.
4. **Supervisor** (`session/supervisor.py`):
   - Reads `HKCU\Control Panel\Desktop`: `ScreenSaveActive`, `ScreenSaveTimeOut`, `SCRNSAVE.EXE` and `ScreenSaverIsSecure` (reuse
     the reader from `session/keyboard.py`). Watches `user.reg` with inotify and sends the new timeout to idle-watch when it changes.
     wineserver writes `user.reg` a few seconds after a change, which is soon enough.
   - On `idle`: if no screen saver is running, start `wine <SCRNSAVE.EXE> /s` with the session's Wine environment and remember the
     process. If it exits with idle-watch still reporting idle, do not start it again until the next `active`.
   - Skip the screen saver while a full-screen window is active (a game). Decide this after measuring: XP itself does not skip them.
5. **WM** (`wm.c` and `wm.py`, `tests/wm_parity.py`): the screen saver's full-screen topmost Wine window is above the taskbar and
   has focus. Check what the WM already does with it before changing anything.
6. **Monitor off**: `dpms` in `session.json` (seconds, default off). idle-watch sets the X server's DPMS timeouts, so the X server
   does it with no wakeups. XP's Power Options tab is not used for it.

## Phase 2: the lock screen

1. **Shared input code**: move the Welcome screen's input handling out of `greeter/xp-greeter.py` into a module that both use. The
   greeter keeps LightDM as the way to check the password; the lock screen uses PAM.
2. **`greeter/xp-lock.py`**:
   - A full-screen override-redirect window drawn by `welcome.py`, like `xp-splash.py`. It shows only the session's user, with
     "N programs running" under the tile (the string from logonui's resources; the number from the taskbar buttons, i.e. the WM's
     list of windows plus the Wine windows with taskbar buttons; check what is available).
   - It grabs the keyboard and the pointer. If another client holds a grab (an open Wine menu), it retries for a short time and stays
     drawn meanwhile.
   - It checks the password with PAM, through a new `/etc/pam.d/win4wine-lock` (`@include common-auth`, `@include common-account`),
     installed by `greeter/install.sh`. As a normal user, `pam_unix` checks through `unix_chkpwd`, so no root is needed. Python
     needs `python3-pam` or a small ctypes binding to `libpam`; choose when writing it.
   - XP's wrong-password balloon and the password hint come from logonui's strings, as in the greeter.
   - "Turn off computer" on the lock screen goes to the supervisor's shut-down path (later; at first it is hidden).
3. **Supervisor**:
   - Starts `xp-lock.py` on a lock request and writes a lock file in the IPC directory. While that file exists, it starts the lock
     screen again if it exits without unlocking (exit status 0 = unlocked).
   - Plays XP's sounds if they exist (there is no sound for locking; unlocking has none in XP either, so check on real XP).
4. **WM**: the lock window is the topmost window, whatever is mapped or raised after it, including override-redirect menus, Windows 7's
   overlays (`raise_overlays`) and full-screen windows. Recognise it by a property (`_WIN4WINE_LOCK`) instead of relying only on the
   window restacking itself as `xp-splash.py` does. Both `wm.c` and `wm.py`.
5. **Optional hardening** (only if wanted at the event): `DontVTSwitch` and `DontZap` in Xorg's config, so Ctrl+Alt+F1 and
   Ctrl+Alt+Backspace cannot get past the lock. This is a change on the EeePC (`/etc`): ask first, and log it in `tools/eeepc/LOG.md`.

## Phase 3: ways to lock

1. **Win+L**: first find out, with a trace of public APIs (`+relay` limited to `user32.LockWorkStation`), whether XP's Explorer
   calls `LockWorkStation` for Win+L.
   - If it does: a Wine patch makes `LockWorkStation` (opt-in, XP session) write `lock` to a file named by an environment variable,
     like patch 0024 does for log off. The supervisor watches it with inotify, since log off's file is only read when the shell exits.
   - If not: the WM, which already grabs the Windows key, handles Win+L itself and writes the request.
2. **"On resume, display Welcome screen"** (`ScreenSaverIsSecure` = 1): when the screen saver starts, also start the lock screen,
   stacked just below the screen saver's window and without its grabs. When the screen saver exits, the lock screen grabs. This way
   the desktop never shows in between, and the screen saver still gets the mouse movement that ends it.
3. **Lid and suspend**: listen for logind's `Lock` signal on the session (D-Bus) and lock. Check that the lid's suspend on the
   EeePC locks before sleeping, so the Welcome screen is there on waking.
4. **Log Off dialog → Switch User**: only with Fast User Switching shown, and its API is not documented. Leave it out for now.

## Phase 4: programs that hold the screen saver off (only if needed)

- Windows sends `WM_SYSCOMMAND` `SC_SCREENSAVE` to the foreground window, and programs that do not pass it to `DefWindowProc` stop
  the screen saver. Programs also use `SetThreadExecutionState(ES_DISPLAY_REQUIRED)`.
- Check first whether this matters for what will be shown (Windows Media Player 9 playing a video, games). If it does: a small Wine
  helper sends `SC_SCREENSAVE` to the foreground window before the supervisor starts the screen saver, a Wine patch makes
  `DefWindowProc` answer it, and `SetThreadExecutionState` resets the X idle counter as dinput's activity notice does.

## Tests

- `tests/`: idle-watch's protocol with a fake X (or Xvfb in the container: `xdotool` input resets `IDLETIME`), the supervisor's
  state changes (idle → screen saver → active → lock), and the lock window above other windows in the WM's tests, with `wm_parity.py`.
- On the EeePC: wakeups at idle with `tools/eeepc` before and after (no increase allowed), the time from Win+L until the lock screen
  shows, and a screenshot of each screen saver and of the lock screen.

## Changes on the EeePC

- The extraction and prefix update in phase 0 happen through `setup.sh` in the checkout, inside `.local/`.
- `/etc/pam.d/win4wine-lock` and possibly the Xorg options: outside the checkout, so ask first and log them in
  `tools/eeepc/LOG.md` (what, why, result, undo).

## Docs to update with each phase

- `docs/architecture.md`: idle-watch, the lock screen, the lock file in the IPC table.
- `docs/design.md`: why the lock screen is inside the session and why PAM checks the password.
- `docs/patches.md`: each new patch.
- `IDEAS.md`: point "Screensaver / Lockscreen" to this plan.
