---
name: eeepc
description: Work on the real target, the Asus EeePC 1015PEM: deploy a commit or a Wine build to it, log on and off, drive its XP session, take screenshots, sample CPU and wakeups, and record changes in tools/eeepc/LOG.md. Use whenever something must be checked, measured or installed on the EeePC.
---

# The EeePC

It is the user's live machine. **Ask before changing anything on it** (packages, `/etc`, the prefix, a new Wine, the
checkout) and before killing processes. Input from `drive` or `insession` lands in the user's session: ask, and have them leave
the machine alone meanwhile. Reading is fine without asking: logs, `ps`, screenshots, `sample`.

How to reach it is in `CLAUDE.local.md`. `tools/eeepc/eee.sh` takes the host from `$EEEPC` or `.local/eeepc-host` (not in
Git; write the host from `CLAUDE.local.md` there if it is missing). The machine, its numbers so far and the other tools are in
[tools/eeepc/README.md](../../../tools/eeepc/README.md).

## From here

```sh
E=tools/eeepc/eee.sh
$E shot /path/in/scratchpad/now.png            # its screen, then Read the PNG; a crop: ... now.png 300x40+724+560
$E sample 30                                   # CPU and wakeups per process over 30 s (sample.py --help)
$E drive chord Super_L+r sleep 2 type 'notepad' key Return wait Notepad
$E x 'tail -50 ~/.local/state/win4wine/xp-shell.log'
$E insession probe.sh                          # a script from here, run inside the logind session
```

- The X keyboard layout is **German**. `drive.py type` presses each character on the key and level the layout has (AltGr
  for `\`, `|`, `@`, `{}`), so `type 'C:\xp'` works. Single keys and chords: `key Return`, `chord Alt_L+F4`.
- ssh is not "the active local session" for polkit. What needs one (`udisksctl`, `loginctl lock-session`) goes through
  `insession`: it types `C:\xp\bin\linux-launch.exe sh /tmp/w4w-run.sh` into Start, Run and prints the script's output.
- `xwd -root` misses redirected windows (taskbar, Start menu, menus show only as shadows); `shot` uses `shot.py`, which does not.
- My ssh traffic lights up xptray's network icon, which makes the notification area repaint: idle numbers taken while ssh is busy
  are too high. Start `sample` and leave the link quiet.

## Deploying

1. Commit and push to `origin` (the fork).
2. On the EeePC, with the user's go-ahead: `cd ~/win4wine && git fetch fork && git checkout -B master fork/master`.
3. What else it needs:

   | Changed | Then |
   |---|---|
   | proxy, xp-explorer, xptray (bridge) | `bridge-wm/build.sh` there; reload with `pkill -x proxy.exe` **only after checking no Windows program is open** (the shell restarts and ends them all). Never `pkill -f` with a pattern that is in the ssh command line: it kills the ssh shell |
   | WM, supervisor, session scripts | the user logs off and on (the session ends when the WM exits). A WM change goes into both `wm.c` and `wm.py` (`tests/wm_parity.py`) |
   | Wine | the user logs off; here `wine/container-build.sh --deploy HOST`; then there the prefix additions as `scripts/setup.sh` runs them after a Wine build: `prefix/extras.py`, `session/linux_apps.py`, `xp-explorer/scripts/pins.py` in `xvfb-run`, then `wineserver -k` and `-w` (about 7 minutes). A session needs the user's password: ask them to log on |
   | prefix additions (`extras.py` VERSION) | as for Wine, without the build |

   A Wine install copied while a session runs waits in `.local/wine-install.incoming`. Run `--deploy` again after logging out.
4. Wait for the session with a background loop on `pgrep -f session/supervisor.py` (or `status.json` saying `xp-shell`), not
   with sleeps.

## Traces

Per-process Wine channels: `"debug": "+timestamp,fixme-all,xp-explorer.exe:+file,winedevice.exe:+mountmgr"` in
`~/.config/win4wine/session.json`. That needs a relogin, so remove it afterwards. `+server` traces the wineserver for every
process. Summarise it with `tools/eeepc/servertrace.py` and `slice.py`.

To run `wine` there with a session process's environment (from `/proc/PID/environ`), drop `WINESERVERSOCKET`,
`WINELOADERNOEXEC` and `WINEPRELOADRESERVE`, or it exits 1 without a word.

## Recording

- Every change on the machine outside the checkout (packages, kernel, `/etc`, services, the prefix, a new Wine) gets an entry in
  [tools/eeepc/LOG.md](../../../tools/eeepc/LOG.md) in the same session: what, why, result, undo. Its head says how.
- Measurements go into `tools/eeepc/README.md` (or the feature's plan) with the date, the repo commit and how they were taken.
- A feature that is now on the EeePC gets its picture taken there (`screenshots` skill).
- `IDEAS.md` and `LOG.md` often hold the user's own uncommitted notes. Commit only your lines with
  `tools/stage-edit.py FILE OLD NEW`, which stages one edit and leaves the rest unstaged.
