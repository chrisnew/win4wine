---
name: bench
description: Run the whole XP session on this development machine (Docker, Xvfb at 1024x600) to try a change, drive it with XTest and take screenshots, before it goes to the EeePC. Use whenever a change to the session, the bridge, the prefix, xp-explorer or Wine should be seen working, or a screenshot is needed and the EeePC is not to be touched.
---

# The bench: the XP session in Docker

`tools/bench/bench.sh` runs the real session (supervisor, WM, proxy, XP Explorer) in a Debian 13 container on Xvfb at the
EeePC's 1024x600, with this checkout's Wine (`.local/wine-install`, mounted read-only) and XP files (`.local/assets`). It is for
seeing a change work. It is **not** a measure of speed: this CPU is many times the Atom's, so timing bugs may not show, and
numbers must come from the EeePC (`eeepc` skill).

## Start

```sh
tools/bench/bench.sh up w4w-bench       # once: image, container, bridge, prefix (about a minute; the image the first time)
tools/bench/bench.sh start w4w-bench    # Xvfb :5 and the session; prints "XP shell up after N s"
tools/bench/bench.sh shot w4w-bench /path/in/scratchpad/desk.png   # then Read the PNG to look at it
```

`up` needs `.local/wine-install` (`wine/container-build.sh`) and `.local/assets`. `docker ps -a` shows containers left from
earlier sessions: reuse one with `start` rather than making another.

## After changing something

| Changed | Do |
|---|---|
| session, bridge, xp-explorer sources, prefix scripts | `stop`, `sync` (copies the checkout, rebuilds the bridge), `start` |
| prefix additions (`prefix/extras.py`, `linux_apps.py`, `pins.py`) | `stop`, `sync`, `prefix`, `start` |
| Wine | `stop`, `wine/container-build.sh` (the bench sees the new install at once), `start`; the session stages the prefix |
| `prefix/create.py` or a clean state wanted | `rm`, then `up` |

`start NAME '+timestamp,warn+seh'` sets session.json's `"debug"` (Wine channels; `+server` traces the wineserver). `logs NAME`
prints the ends of the session's logs.

## Driving it

`bench.sh drive NAME ACTION...` runs `tools/eeepc/drive.py` (`--help` lists the actions) on the bench's display:

```sh
B=tools/bench/bench.sh
$B drive w4w-bench chord Super_L+r wait Run 10 type 'notepad' key Return wait Notepad 20
$B drive w4w-bench rclick 560 470 sleep 1.5          # then shot, Read, pick the next coordinates
$B drive w4w-bench windows                            # titles of the viewable windows
```

- Look before you click: take a shot, read the coordinates off it, click, take another. Windows cascade, so positions
  change from run to run.
- `wait TITLE` returns when a window with that title maps. That is before it takes keyboard input, and a prompt can carry the
  same title (Notepad's "create a new file?"): add a `sleep 1`, and check with a shot.
- Programs from a script: `bench.sh sh NAME <<'EOF'` with `nohup python3 session/wine.py 'C:\xp\xp-explorer.exe' 'Z:\usr\bin'
  > /tmp/open.log 2>&1 < /dev/null &`. Quote the heredoc (`'EOF'`) so backslashes survive. `$P` is the prefix and `$W` the
  wine binary in there.
- Programs started with `session/wine.py` from outside lack the supervisor's `WIN4WINE_*_REQUEST` variables (log off, lock).
  Start those through the session (Start, Run).
- A fresh session hides bugs of the long-running shell. Close and reopen folder windows before concluding "works here".

## Win32 probes

The image has MinGW. To build the probes and run one in the session:

```sh
tools/bench/bench.sh sh w4w-bench <<'EOF'
WIN4WINE_PROBES=/tmp/probes tests/build.sh
python3 session/wine.py 'Z:\tmp\probes\alt-tab-test.exe'
EOF
```

A probe that needs no session can run headless: `WINEPREFIX=/tmp/pfx $W x.exe` in `bench.sh sh`. Name it `startmenu.exe` to
get XP's native shell32 (AppDefaults) and call XP's shell APIs directly.

## Other devices on a network

Extra containers on a user-defined Docker network see each other's multicast (rootless too): `docker network create w4w-net`,
then `docker network connect w4w-net w4w-bench` and a peer started from the same image with `--network w4w-net`. mDNS needs
`avahi-daemon` in each (`dbus-daemon --system --fork`, then `avahi-daemon --daemonize --no-drop-root --no-chroot` with
`rlimit-nproc` removed from its configuration). Publish with `avahi-publish`.

## Gotchas

- Rootless Docker: the container's user cannot read this machine's scratchpad. Pipe scripts in (`bench.sh sh`), and get files
  out with `docker exec NAME cat FILE > here`.
- The host's `rm` asks first (an alias): use `command rm -f` in scripts that run unattended.
- `tests/linux_apps.py --wine` needs the `ja_JP.UTF-8` locale, which the image has.
- The lock screen's password for user `tester` is `Secret ^1`.
- When done for the day, `stop` the session. `rm` the container only when its state is no longer needed.
