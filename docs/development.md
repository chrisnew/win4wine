# Development

To just use LunaticOS, `scripts/setup.sh` from [INSTALL.md](../INSTALL.md) is enough. This document covers how to change and test the patches or the bridge.

## Repository layout

| Path | Contents |
|---|---|
| `resources/` | Our own icons (`.ico`, with their SVG), drawn after [XP-Style Icon Guide.md](../XP-Style%20Icon%20Guide.md) |
| `wine/` | The Wine it is based on (`upstream.env`), the patches (`patches/` and `series`), the build (`build.sh`), applying the patches (`patch-state.py`) |
| `bridge-wm/` | The X11 WM (`wm.c`, and `wm.py` that behaves the same), the Win32 proxy (`proxy.c`), the tray broker (`tray.py`), a tool for investigations (`inspect.c`) |
| `wayland/` | The Wayland session: labwc and wlroots versions (`upstream.env`), patches (`patches/`), the build (`build.sh`), labwc's configuration (`labwc/`), startup (`session.sh`) |
| `session/` | Starting and watching the session (`supervisor.py`), the Start menu of Linux apps (`linux_apps.py`), My Network Places (`network_places.py`), `win4wine-wine` for use outside the session (`wine.py`; the Wine environment is in `wineenv.py`), registering the session |
| `prefix/` | Creating the prefix (`create.py`), updating it (`stage.py`), appearance and DPI (`display.py`), registry settings (registry profiles) |
| `extract/` | Taking only the needed files from the ISO (`extract.py`), detecting the XP language (`media.py`) |
| `greeter/` | The XP Welcome screen for LightDM, the log on/log off screens, XP's cursors |
| `scripts/` | Installing everything (`setup.sh`), the development chroot (`debian.sh`), the pre-commit check |
| `tools/` | The bench session in Docker (`bench/`), driving, measuring and deploying to the EeePC (`eeepc/`), `stage-edit.py` |
| `tests/` | Integration tests, and probes of public APIs (C) that run both on Wine and on a real XP |
| `localres/`, `.local/` | ISOs, extracted files, prefixes, builds, logs, screenshots. **Not tracked by Git** |

## Git setup

```sh
git config core.hooksPath .githooks
```

The pre-commit check (`scripts/check-staged.py`) refuses files under `localres/` and `.local/`, PE files, registry hives, fonts
and binaries containing NUL bytes (even with `git add -f`). The only exceptions are PNGs in `docs/images/` and its folders
(screenshots, see [showcase.md](showcase.md)) and our own icons, `.ico` files in `resources/`.

## Building and testing in Docker

This is the usual way, and the one this fork's work is done with: the development machine needs Docker, git and nothing else
(no Wine, MinGW or Xvfb of its own). Everything builds and runs in Debian 13 containers, with the same compilers and libraries
as the EeePC, so what is built here runs there.

```sh
wine/container-build.sh                         # Wine into .local/wine-install (the first build takes long, later ones
                                                # recompile only what changed patches touch)
wine/container-build.sh --deploy HOST           # the same, then copy the install to HOST's checkout over ssh
tools/bench/bench.sh up NAME                    # a container with this checkout, the bridge built and a new prefix
tools/bench/bench.sh start NAME                 # Xvfb at 1024x600 and the whole XP session in it
tools/bench/bench.sh shot NAME OUT.png          # a screenshot; drive NAME ACTION... types, clicks and presses keys
tools/bench/bench.sh sync NAME                  # after a change: copy the checkout again and rebuild the bridge
```

- `wine/container-build.sh` makes the image `win4wine-wine-build:trixie` the first time; `tools/bench/bench.sh image` builds
  the bench's image on top of it. The bench needs `.local/wine-install` and `.local/assets` (`extract/extract.py`).
- `bench.sh --help` lists all its commands (`sh NAME < SCRIPT` runs a script in the session, `logs`, `stop`, `rm`). The bench
  is for seeing and testing a change; speed is only measured on the EeePC.
- Tests that need X but no session run in a throwaway bench container on a copy of the checkout, for example the WM's:

  ```sh
  docker run --rm -v "$PWD":/src:ro w4w-bench:trixie bash -c 'cp -r /src /tmp/w && cd /tmp/w &&
      bridge-wm/build.sh && { Xvfb :7 -screen 0 1024x600x24 & sleep 1; } && mkdir -m 700 /tmp/ipc &&
      { DISPLAY=:7 .local/bridge/wm --ipc /tmp/ipc & sleep 1; } &&
      DISPLAY=:7 XP_BRIDGE_DIR=/tmp/ipc python3 tests/wm_integration.py'
  ```

- Wine changes are made with `wine/patch-edit.py` ("Patches" below), which compiles single modules in the same container.

## Development environment in a chroot (upstream's way)

Upstream builds in a chroot instead. It is kept as upstream left it; this fork has not used it since it moved to Docker.

The host needs git, sudo and debootstrap, and at least 15 GB free. The chroot is created in `.local/debian`; `/work` inside it is this repository.

```sh
scripts/debian.sh init
scripts/debian.sh run /bin/bash
```

Inside the chroot, build like this:

```sh
cd /work
./wine/build.sh > .local/wine-build.log 2>&1    # fetch, patch and build Wine
# to watch progress: scripts/progress.py .local/wine-build.log Wine -- ./wine/build.sh
./bridge-wm/build.sh                            # .local/bridge (proxy.exe, linux-launch.exe, ...)
./tests/build.sh                                # the probes
./extract/extract.py localres/WindowsXPSP3.iso .local/assets
Xvfb :95 -screen 0 1280x800x24 -nolisten tcp > .local/xvfb.log 2>&1 &
export DISPLAY=:95 WINE=/work/.local/wine-install/bin/wine
./prefix/create.py .local/assets .local/prefix-dev --home /home/builder --wine "$WINE"
```

- `wine/build.sh` takes `WINE_SOURCE`, `WINE_BUILD`, `WINE_INSTALL` and `JOBS` for the locations and the number of parallel jobs.
  Wine is built in the new WoW64 mode (`--enable-archs=i386,x86_64`), so no i386 libraries are needed.
- After changing a patch, check it with a build **from configure, in a fresh source and build directory**. A reused build
  directory keeps old objects, and you will miss omissions (such as a header that was not registered).
- `extract.py` and `create.py` stop if the output already exists. To redo one, create it under a different name.
- `wayland/build.sh` fetches labwc, fetches wlroots and libsfdo as subprojects, patches them and links them statically
  (`LABWC_SOURCE`, `LABWC_BUILD`, `LABWC_INSTALL`, `JOBS`). The chroot needs the packages of `scripts/setup.sh --wayland`.
  To test it nested, run `wayland/session.sh LABWC --config CONFIG` on the chroot's Xvfb with
  `WLR_BACKENDS=x11 WLR_X11_OUTPUTS=2 WLR_RENDERER=pixman` (the outputs are named `X11-1` and `X11-2`).

## Patches

```sh
python3 wine/patch-state.py SOURCE wine/patches --apply   # do they all apply to an unmodified Wine 11.0?
```

The labwc and wlroots patches can be checked with the same tool (`wine/patch-state.py SOURCE wayland/patches/labwc --apply`;
for wlroots, apply `wayland/patches/wlroots` to `SOURCE/subprojects/wlroots`).

The list, order, format and the opt-in check are in [patches.md](patches.md). Never ship a change made directly in
`.local/wine-upstream`: always turn it into a patch.

To change Wine, edit the patched source and let `wine/patch-edit.py` make the patch: `mark`, edit `.local/wine-upstream`,
`make dlls/NAME/all` (compiles and links in the Debian 13 container, in seconds), then `fold NNNN` into an existing patch or
`new NNNN-dll-what.patch`, and `sync`. After `sync`, `wine/container-build.sh` compiles only what changed instead of starting
from upstream again. `wine/patch-edit.py --help` has the details.

## Tests

| Test | What it covers |
|---|---|
| `tests/wm_integration.py` | The WM alone (Alt+Tab, focus, minimizing, passing on the Windows key). Runs against either WM |
| `tests/wm_parity.py --wm CMD` | Drives a WM through frames, commands, icons, maximizing and stacking and prints its state after each step; the output of `wm.py` and `wm.c` must be identical (run each on a fresh Xvfb and `diff`) |
| `tests/alt-tab.c` | Windows for the proxy's Alt+Tab rules (modal dialog, hidden owner, tool window) in a running session; its header says which are listed |
| `tests/wine_restore.py` | A Wine window minimized and restored from Win32 is shown again (pass `WINE` and `WINEPREFIX`) |
| `tests/bridge_integration.py` | The WM, the proxy and the XP shell together |
| `tests/native_frames.py` | XP frames on Linux windows (Luna and Classic) |
| `tests/xterm_redraw.py`, `tests/wm_latency.py` | Redrawing and latency |
| `tests/wayland_compositor.py` | The Wayland session's labwc without Wine (nested, scale 1.5): X space, work area, IPC, frame mapping, stacking order, maximizing, always on top. Pass `DISPLAY` (Xvfb) and `LABWC` |
| `tests/tray_host.py` | The tray broker (StatusNotifierItem and XEmbed) |
| `tests/screen_guard.py` | When the supervisor starts the screen saver and the lock screen (`session/screen.py`), with stand-ins; no X, no Wine |
| `tests/idle_watch.py IDLE_WATCH` | `session/idle-watch.c` on an Xvfb: idle and active, `XResetScreenSaver`, DPMS, no wakeups while idle |
| `tests/logind_watch.py` | `session/logind-watch.py` with a stand-in for logind on a private bus: `Lock` and the delayed sleep |
| `tests/linux_apps.py` | The Start menu of Linux apps |
| `tests/iso_mount.py` | Mounting a disc image (`session/iso_mount.py`) with a stand-in for `udisksctl`; no Wine, no loop devices |
| `tests/network_places.py` | My Network Places: names, URLs, mDNS, SSDP and UPnP descriptions (hostile ones too), the shortcuts per device, and which files are touched; no Wine, no network |
| `tests/linuxcols.py` | The plain C part of XP Explorer's Linux columns: the permissions as `ls -l` writes them, user and group names, the paths left alone (host gcc; no Wine, no X) |
| `tests/recovery_integration.py CONFIG.json` | Switching over when the shell crashes |
| `tests/greeter_layout.py ASSETS`, `tests/cursor_theme.py ASSETS` | The Welcome screen's layout (96, 120 and 144 DPI), the cursors (32 and 64) |
| `tests/patch_state.py`, `tests/font_*.py`, `tests/workarea.py`, `tests/dpi_settings.py` | Unit tests |
| `tests/xp_language.py [ja=ASSETS] [en=ASSETS]` | The registry settings that differ between Japanese and English XP, and detecting the language of extracted files |
| `tests/stat-lx-probe.c` | `NtQueryInformationByName` (Wine patch 0160) in the session's Wine, to compare with `stat`; 32-bit and 64-bit |
| `tests/ole-clipboard-thread.c` | The OLE clipboard after the thread that used it first has ended (Wine patch 0161): `ole-clipboard-thread-test.exe` prints PASS; 32-bit and 64-bit |
| `tests/uniscribe-thread.c` | Threads making Uniscribe's font caches at the same time (Wine patch 0165): `uniscribe-thread-test.exe` prints PASS; 32-bit and 64-bit |
| `tests/clip-exit.c` | The cursor clip after a program that held it was ended or exited (Wine patch 0166): `clip-exit-test.exe` prints PASS twice; 32-bit and 64-bit |
| `tests/shreg-set-path.c` | `SHRegSetPathA` and `SHRegSetPathW` store the whole path (Wine patch 0167): `shreg-set-path-test.exe` prints PASS |
| `tests/file-icons.c` | In the session's prefix, run as `startmenu.exe` with an output file: the icon Explorer shows for each registered file type, and whether its `DefaultIcon` loads |
| `tests/screen-saver.c` | The screen saver setting, its `system.ini` mapping and `LockWorkStation`, in the session's prefix |
| `tests/*.c` | Probes of public APIs. The same program runs on Wine and on a real XP and the numbers are compared (DPI: `dpi-metrics.c`, `char-widths-dpi.c`, `caption-glyph-dpi.c`, `text-bits-dpi.c`) |

## Installing by hand

To install without `setup.sh`, build Wine, the bridge, the extracted files and the prefix as above (in Docker or the chroot), and write
`~/.config/win4wine/session.json` (the keys are in [INSTALL.md](../INSTALL.md#settings-configwin4winesessionjson)).

```sh
./session/install.sh                         # /usr/local/bin/xp-shell-session and the xsessions entry
./session/install.sh --wayland .local/labwc-install/bin/labwc   # optional: the Wayland session too (labwc from wayland/build.sh)
./greeter/install.sh /absolute/path/assets   # optional: the Welcome screen
```

The launcher scripts that `session/install.sh` installs point at this checkout, so reinstall them if you move the checkout.

## Updating an existing setup

1. Stop the session and make sure wineserver has exited.
2. Replace the Wine install.
3. Run `./prefix/stage.py --wine "$WINE" "$WINEPREFIX"`. It checks whether wine.inf changed and runs wineboot, re-registers
   XP's COM servers, applies the registry profiles and places XP's DLLs. It also runs automatically when the session starts.
4. Rebuild the bridge with `bridge-wm/build.sh` and start the session again.

To change appearance and DPI, stop the session and run `prefix/display.py --theme luna|classic --menu xp|classic --dpi N`
(`--theme` also changes the wallpaper, as XP's themes do: none for classic, Bliss for luna). N is, as in XP, 96 (Normal),
120 (Large size), or a custom 19–480. The session passes the same DPI to Linux apps (`Xft.dpi`, fontconfig, the cursor size,
and GTK's `GDK_SCALE` at 192 and above; `session/dpi.py`). The Welcome screen's DPI is set by `"dpi"` in
`/etc/win4wine/greeter.json` (in XP, too, it is a machine-wide setting).

## Importing from an existing XP hive

Install `python3-hivex` and convert read-only. Only the namespace CLSIDs and a limited set of NLS values are handled;
services, drivers, logon and product information are not carried over.

```sh
./prefix/hive-to-reg.py --software .../SOFTWARE --system .../SYSTEM --output .local/shell-hive.reg
```
