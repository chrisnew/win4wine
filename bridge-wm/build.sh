#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/.local/bridge"
compile() {
    local target=$1
    shift
    local temporary
    temporary=$(mktemp "$target.XXXXXX")
    if "${MINGW_CC:-i686-w64-mingw32-gcc}" -std=c11 -Wall -Wextra -Werror -O2 -municode \
        "$@" -o "$temporary"; then
        chmod 755 "$temporary"
        mv -f -- "$temporary" "$target"
    else
        rm -f -- "$temporary"
        return 1
    fi
}
# The X11 WM (wm.c), a native program. Without gcc and libxcb's headers the
# session runs wm.py instead. A new inode: the running WM keeps its own.
if command -v gcc >/dev/null && [ -f /usr/include/xcb/xcb.h ]; then
    temporary=$(mktemp "$root/.local/bridge/wm.XXXXXX")
    if gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o "$temporary" "$root/bridge-wm/wm.c" -lxcb -lm; then
        chmod 755 "$temporary"
        mv -f -- "$temporary" "$root/.local/bridge/wm"
    else
        rm -f -- "$temporary"
        exit 1
    fi
else
    printf 'No gcc or libxcb headers (libxcb1-dev): the session will run wm.py\n' >&2
fi
# The screen savers' idle time (session/idle-watch.c), a native program too.
if command -v gcc >/dev/null && [ -f /usr/include/X11/extensions/sync.h ]; then
    temporary=$(mktemp "$root/.local/bridge/idle-watch.XXXXXX")
    if gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o "$temporary" "$root/session/idle-watch.c" -lXext -lX11; then
        chmod 755 "$temporary"
        mv -f -- "$temporary" "$root/.local/bridge/idle-watch"
    else
        rm -f -- "$temporary"
        exit 1
    fi
else
    printf 'No gcc or libXext headers (libxext-dev): the session will have no screen saver\n' >&2
fi
# Sessions may still map these PE files. Publish new inodes without truncating
# an executable that another Wine process is using.
compile "$root/.local/bridge/proxy.exe" "$root/bridge-wm/proxy.c" -luser32 -lgdi32 -lshell32 -luxtheme
compile "$root/.local/bridge/inspect.exe" "$root/bridge-wm/inspect.c" -luser32 -lgdi32 -lole32 -lshell32 -luuid
compile "$root/.local/bridge/menu-settings.exe" "$root/prefix/menu-settings.c" -lshell32
compile "$root/.local/bridge/theme-settings.exe" "$root/prefix/theme-settings.c" -luxtheme
compile "$root/.local/bridge/wallpaper-convert.exe" -municode "$root/prefix/wallpaper-convert.c" -lole32 -luuid -lwindowscodecs
# Windows 7's shell prefix (prefix/win7.py).
compile "$root/.local/bridge/theme-fonts.exe" "$root/prefix/theme-fonts.c" -luxtheme
compile "$root/.local/bridge/user-profile.exe" -municode "$root/prefix/user-profile.c" -lole32 -lshell32 -luuid
# Linux applications in the XP Start menu (session/linux_apps.py).
compile "$root/.local/bridge/linux-launch.exe" "$root/session/linux-launch.c" -mwindows -luser32
compile "$root/.local/bridge/linux-links.exe" "$root/session/linux-links.c" -lole32 -lshell32 -luuid
# XP's logon and log off sounds (session/supervisor.py).
compile "$root/.local/bridge/playsound.exe" "$root/session/playsound.c" -mwindows -lwinmm -lshell32
# The session's keyboard layout follows XP's input languages (session/keyboard.py).
compile "$root/.local/bridge/keyboard-watch.exe" "$root/session/keyboard-watch.c" -mwindows -lshell32 -ladvapi32
# XP Explorer's helpers (xp-explorer/helpers), for the session too: the
# notification area's network, battery and volume icons, the Start menu's
# most used programs, and the stand-in iexplore.exe that opens the Linux browser.
helpers=$root/xp-explorer/helpers
i686-w64-mingw32-windres -I "$helpers" "$helpers/xptray.rc" -O coff -o "$root/.local/bridge/xptray.res"
compile "$root/.local/bridge/xptray.exe" -std=gnu11 "$helpers/xptray.c" "$root/.local/bridge/xptray.res" \
    -mwindows -lcomctl32 -lgdi32 -luser32 -lkernel32
rm -f "$root/.local/bridge/xptray.res"
compile "$root/.local/bridge/startmenu.exe" -std=gnu11 "$helpers/startmenu.c" -lshell32 -lole32 -luuid -ladvapi32 -luser32
compile "$root/.local/bridge/iexplore.exe" -std=gnu11 "$helpers/iexplore.c" -mwindows -lshell32
# XP Explorer's Linux columns, an in-process column handler.
compile "$root/.local/bridge/linuxcols.dll" -std=gnu11 -shared "$helpers/linuxcols.c" "$helpers/linuxcols.def" \
    -Wl,--kill-at -static-libgcc -lole32 -loleaut32 -ladvapi32 -luuid
