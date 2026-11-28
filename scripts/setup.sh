#!/bin/bash
# Set up the XP shell session on this Debian 13 machine, from a fresh install.
#
#   scripts/setup.sh --media PATH [--greeter | --no-greeter] [--theme luna|classic]
#                    [--dpi N] [--font-smoothing standard|cleartype] [--wayland]
#                    [--wine-install DIR | --jobs N]
#
#   --media PATH   the user's Windows XP SP3 (x86) ISO or Windows directory
#   --greeter      also make LightDM show XP's Welcome screen (greeter/), with XP's
#                  cursors; LightDM becomes the display manager
#   --no-greeter   keep the display manager's own login screen (the default); XP's
#                  cursors are still installed for the session
#   --theme T      luna (XP's default, the default here, with XP's Start menu) or
#                  classic (with the classic Start menu), for a new prefix; given
#                  explicitly, it is applied to an existing prefix too
#   --dpi N        the screen's DPI as XP sets it (LogPixels): 96 (normal), 120 (large)
#                  or any value from 19 to 480; for the XP shell, the Linux
#                  applications in its session and the Welcome screen
#   --font-smoothing S
#                  XP's screen font smoothing (Display Properties, Effects): standard
#                  (XP's default) or cleartype; for the XP shell and the Linux
#                  applications in its session. Given explicitly, it is applied to an
#                  existing prefix too
#   --wayland      also build win4wine's labwc (wayland/build.sh) and register the
#                  Wayland session "XP Shell (Wine, Wayland)":
#                  per-monitor and fractional scales (session.json "outputs"),
#                  Wayland applications with the shell's frames
#   --wine-install DIR
#                  use a Wine built elsewhere with this repository's patches (the
#                  .local/wine-install of another Debian 13 machine, copied anywhere)
#                  instead of building it here
#   --jobs N       parallel jobs for the Wine build (default: the number of CPUs)
#   --no-shadowfb  on a virtual machine whose graphics card X draws into directly
#                  (QEMU's standard VGA, Hyper-V, VirtualBox's VBoxVGA/VBoxSVGA),
#                  do not make X draw in memory first (see "Virtual machines" below)
#
# Run it as the user who will log in to the XP shell (not as root); it uses sudo
# for packages and system files. It builds Wine with this repository's patches,
# extracts the XP files, creates the XP prefix and registers the "XP Shell"
# session. Steps already done are skipped, so it can be run again after a failure.
# Everything it builds stays in .local/ of this checkout.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
local=$root/.local
media=
lang=
greeter=no
jobs=$(nproc)
theme=luna
theme_given=no
dpi=
smoothing=
wayland=no
prebuilt=
shadowfb=yes
while (($#)); do
    case $1 in
        --media) media=${2:?--media needs a path}; shift 2 ;;
        --greeter) greeter=yes; shift ;;
        --no-greeter) greeter=no; shift ;;
        --jobs) jobs=${2:?--jobs needs a number}; shift 2 ;;
        --theme) theme=${2:?--theme needs luna or classic}; theme_given=yes; shift 2 ;;
        --dpi) dpi=${2:?--dpi needs a number}; shift 2 ;;
        --font-smoothing) smoothing=${2:?--font-smoothing needs standard or cleartype}; shift 2 ;;
        --wayland) wayland=yes; shift ;;
        --no-shadowfb) shadowfb=no; shift ;;
        --wine-install) prebuilt=$(realpath -e -- "${2:?--wine-install needs a directory}"); shift 2 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) printf 'Unknown option %s (see --help)\n' "$1" >&2; exit 2 ;;
    esac
done
[[ -n $media ]] || { printf 'Give the XP media with --media (see --help)\n' >&2; exit 2; }
[[ $theme == luna || $theme == classic ]] || { printf -- '--theme is luna or classic\n' >&2; exit 2; }
[[ -z $dpi || ( $dpi =~ ^[0-9]+$ && $dpi -ge 19 && $dpi -le 480 ) ]] ||
    { printf -- '--dpi is a number from 19 to 480\n' >&2; exit 2; }
[[ -z $smoothing || $smoothing == standard || $smoothing == cleartype ]] ||
    { printf -- '--font-smoothing is standard or cleartype\n' >&2; exit 2; }
[[ -z $media ]] || media=$(realpath -e -- "$media")
[[ $EUID -ne 0 ]] || { printf 'Run this as the user who will log in, not as root\n' >&2; exit 2; }
[[ $root != *[[:space:]]* ]] || { printf 'The checkout path must not contain spaces: %s\n' "$root" >&2; exit 2; }
# Debian 13 is the only system LunaticOS is made for and tested on.
command -v apt-get >/dev/null || { printf 'LunaticOS is set up on Debian 13 (trixie) only\n' >&2; exit 2; }
if ! grep -q '^VERSION_CODENAME=trixie' /etc/os-release; then
    printf 'Warning: written for Debian 13 (trixie); this is %s\n' "$(. /etc/os-release; echo "$PRETTY_NAME")" >&2
fi
step() { printf '\n=== %s\n' "$*"; }
# Japanese input (session.json "input_method": "fcitx5"), only for Japanese
# XP: the packages also start fcitx5 in the other desktops of the machine
# (its autostart entry), where its own layout (us) replaces the desktop's.
install_input_method() {
    missing=()
    for package in fcitx5 fcitx5-mozc fcitx5-frontend-gtk3 fcitx5-frontend-gtk4 \
            fcitx5-frontend-qt5 fcitx5-frontend-qt6 fcitx5-config-qt; do
        dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed' || missing+=("$package")
    done
    ((${#missing[@]} == 0)) ||
        sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
}
mkdir -p "$local"

step 'Packages'
packages=(
    # Wine build (WoW64: 64-bit Unix libraries, i686 and x86_64 MinGW for the PE modules)
    build-essential flex bison pkg-config git ca-certificates
    # Wine's translations (msgfmt): Wine's own programs and dialogs in the session's language
    gettext
    libx11-dev libxext-dev libxrender-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
    libfreetype-dev libfontconfig-dev libgnutls28-dev libgl-dev libasound2-dev libdbus-1-dev libudev-dev
    gcc-mingw-w64-i686 gcc-mingw-w64-x86-64
    # extraction, prefix creation
    cabextract p7zip-full xvfb xauth
    # the session: X, the WM and tray bridge, recovery terminal, root cursor
    xserver-xorg x11-xserver-utils x11-xkb-utils xterm python3 python3-xlib
    # the WM in C (bridge-wm/wm.c); without it the session runs wm.py
    libxcb1-dev
    python3-gi gir1.2-glib-2.0 gir1.2-gdkpixbuf-2.0 librsvg2-common
    # My Network Places: mDNS (session/network_places.py)
    avahi-daemon
    # "Mount" on .iso files (session/iso_mount.py) and USB sticks (session/removable.py),
    # without a password in the session
    udisks2 polkitd libpam-systemd
    locales
)
if [[ $wayland == yes ]]; then
    packages+=(
        # labwc and wlroots (wayland/build.sh), Xwayland, the outputs' settings
        meson ninja-build wayland-protocols libwayland-dev libxkbcommon-dev libxml2-dev
        libcairo2-dev libpango1.0-dev libglib2.0-dev libinput-dev libpixman-1-dev libpng-dev
        librsvg2-dev libdrm-dev libgbm-dev libegl-dev libgles-dev libseat-dev libdisplay-info-dev
        libliftoff-dev liblcms2-dev hwdata libxcb1-dev libxcb-composite0-dev libxcb-dri3-dev
        libxcb-errors-dev libxcb-ewmh-dev libxcb-icccm4-dev libxcb-present-dev libxcb-randr0-dev
        libxcb-render0-dev libxcb-render-util0-dev libxcb-res0-dev libxcb-shm0-dev
        libxcb-xfixes0-dev libxcb-xinput-dev xwayland wlr-randr
    )
fi
if [[ $greeter == yes ]]; then
    packages+=(lightdm lightdm-gtk-greeter gir1.2-lightdm-1 gir1.2-gtk-3.0 python3-gi-cairo)
elif [[ ! -e /etc/X11/default-display-manager ]]; then
    # No display manager yet: LightDM with its usual login screen.
    packages+=(lightdm lightdm-gtk-greeter)
fi
missing=()
for package in "${packages[@]}"; do
    dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed' || missing+=("$package")
done
if ((${#missing[@]})); then
    sudo apt-get update
    sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
else
    printf 'All installed\n'
fi
if [[ $greeter == yes && $(cat /etc/X11/default-display-manager 2>/dev/null) != /usr/sbin/lightdm ]]; then
    printf 'Making LightDM the display manager (it was %s)\n' "$(cat /etc/X11/default-display-manager 2>/dev/null || echo none)"
    echo /usr/sbin/lightdm | sudo tee /etc/X11/default-display-manager >/dev/null
    sudo systemctl enable --force lightdm.service
fi

# The Unix locale of the session and its Wine prefix, in the language of the
# XP media.
make_locale() {
    [[ $lang != ja ]] || install_input_method
    step "Locale $locale"
    if ! locale -a | grep -qix "${locale%.UTF-8}.utf8"; then
        sudo sed -i "s/^# *\\($locale UTF-8\\)/\\1/" /etc/locale.gen
        grep -q "^$locale UTF-8" /etc/locale.gen || echo "$locale UTF-8" | sudo tee -a /etc/locale.gen >/dev/null
        sudo locale-gen
    else
        printf '%s exists\n' "$locale"
    fi
}

step 'Wine'
wine=$local/wine-install/bin/wine
# The patches of this checkout; a build records those it was made with.
patches=$(python3 "$root/wine/patch-state.py" --digest "$root/wine/patches")
if [[ -n $prebuilt ]]; then
    [[ -f $prebuilt/lib/wine/x86_64-windows/win4wine-controls.dll && -x $prebuilt/bin/wine ]] ||
        { printf '%s is not a Wine install built by wine/build.sh\n' "$prebuilt" >&2; exit 1; }
    wine=$prebuilt/bin/wine
    printf 'Using %s: %s\n' "$prebuilt" "$("$wine" --version)"
    [[ $(cat "$prebuilt/win4wine-patches" 2>/dev/null) == "$patches" ]] ||
        printf 'Warning: %s was not built with the patches of this checkout\n' "$prebuilt" >&2
elif [[ -x $wine && -f $local/wine-install/lib/wine/x86_64-windows/win4wine-controls.dll &&
        $(cat "$local/wine-install/win4wine-patches" 2>/dev/null) == "$patches" ]]; then
    printf 'Built: %s\n' "$("$wine" --version)"
else
    if [[ -x $wine ]]; then
        printf 'The patches changed since the last build; rebuilding what they touch\n'
    fi
    printf 'Building Wine with %s jobs (log: .local/wine-build.log); this takes a long time\n' "$jobs"
    JOBS=$jobs python3 "$root/scripts/progress.py" "$local/wine-build.log" Wine -- "$root/wine/build.sh" ||
        { tail -20 "$local/wine-build.log" >&2; printf 'Wine build failed; see .local/wine-build.log\n' >&2; exit 1; }
    "$wine" --version
fi

step 'Bridge'
"$root/bridge-wm/build.sh"
printf 'Built .local/bridge\n'

labwc=$local/labwc-install/bin/labwc
if [[ $wayland == yes ]]; then
    step 'Wayland compositor'
    labwc_patches=$(python3 "$root/wine/patch-state.py" --digest "$root/wayland/patches/labwc" "$root/wayland/patches/wlroots")
    if [[ -x $labwc && $(cat "$local/labwc-install/win4wine-patches" 2>/dev/null) == "$labwc_patches" ]]; then
        printf 'Built: %s\n' "$("$labwc" --version)"
    else
        [[ ! -x $labwc ]] || printf 'The patches changed since the last build; rebuilding what they touch\n'
        printf 'Building labwc and wlroots (log: .local/labwc-build.log)\n'
        JOBS=$jobs python3 "$root/scripts/progress.py" "$local/labwc-build.log" labwc -- "$root/wayland/build.sh" ||
            { tail -20 "$local/labwc-build.log" >&2; printf 'labwc build failed; see .local/labwc-build.log\n' >&2; exit 1; }
        "$labwc" --version
    fi
fi

step 'XP files'
assets=$local/assets
if [[ -f $assets/manifest.json ]]; then
    printf 'Extracted already: %s\n' "$assets"
else
    # 7z needs room for the whole medium; keep it out of a small /tmp.
    mkdir -p "$local/tmp"
    TMPDIR=$local/tmp python3 "$root/extract/extract.py" "$media" "$assets"
fi
# Files that XP Explorer's additions brought, for an extraction made before them.
read -r -a added <<< "$(python3 "$root/extract/extract.py" --missing "$assets")"
if ((${#added[@]})); then
    printf 'Adding %s to the extraction\n' "${added[*]}"
    mkdir -p "$local/tmp"
    TMPDIR=$local/tmp python3 "$root/extract/extract.py" "${added[@]/#/--only=}" "$media" "$assets"
fi
lang=$(python3 "$root/extract/media.py" --warn "$assets")
case $lang in
    ja) locale=ja_JP.UTF-8; name=Japanese ;;
    de) locale=de_DE.UTF-8; name=German ;;
    *) locale=en_US.UTF-8; name=English ;;
esac
printf 'Windows XP in %s\n' "$name"
make_locale

step 'XP prefix'
prefix=$local/prefix
if [[ -f $prefix/xp-shell.json ]]; then
    printf 'Created already: %s\n' "$prefix"
else
    [[ ! -e $prefix ]] || { printf '%s exists but is incomplete; move it away and run again\n' "$prefix" >&2; exit 1; }
    xvfb-run -a -s '-screen 0 1280x800x24' python3 "$root/prefix/create.py" "$assets" "$prefix" \
        --home "$HOME" --wine "$wine" > "$local/prefix.log" 2>&1 ||
        { tail -20 "$local/prefix.log" >&2; printf 'Prefix creation failed; see .local/prefix.log\n' >&2; exit 1; }
    printf 'Created %s\n' "$prefix"
    # Until the theme is set, a later run sets it too
    touch "$prefix/.theme-pending"
fi
[[ ! -f $prefix/.theme-pending ]] || theme_given=yes
if [[ $theme_given == yes ]]; then
    WINEPREFIX=$prefix WINE=$wine xvfb-run -a -s '-screen 0 1280x800x24' \
        python3 "$root/prefix/display.py" --theme "$theme" --menu "$([[ $theme == luna ]] && echo xp || echo classic)" \
        >> "$local/prefix.log" 2>&1 ||
        { tail -20 "$local/prefix.log" >&2; printf 'Setting the theme failed; see .local/prefix.log\n' >&2; exit 1; }
    WINEPREFIX=$prefix "$(dirname "$wine")/wineserver" -w 2>/dev/null || true
    rm -f "$prefix/.theme-pending"
    printf 'Theme: %s\n' "$theme"
fi
if [[ -n $dpi ]]; then
    WINEPREFIX=$prefix WINE=$wine python3 "$root/prefix/display.py" --dpi "$dpi" >> "$local/prefix.log" 2>&1 ||
        { tail -20 "$local/prefix.log" >&2; printf 'Setting the DPI failed; see .local/prefix.log\n' >&2; exit 1; }
    WINEPREFIX=$prefix "$(dirname "$wine")/wineserver" -w 2>/dev/null || true
    printf 'DPI: %s\n' "$dpi"
fi
if [[ -n $smoothing ]]; then
    WINEPREFIX=$prefix WINE=$wine python3 "$root/prefix/display.py" --font-smoothing "$smoothing" \
        >> "$local/prefix.log" 2>&1 ||
        { tail -20 "$local/prefix.log" >&2; printf 'Setting the font smoothing failed; see .local/prefix.log\n' >&2; exit 1; }
    WINEPREFIX=$prefix "$(dirname "$wine")/wineserver" -w 2>/dev/null || true
    printf 'Font smoothing: %s\n' "$smoothing"
fi
# XP Explorer's additions (prefix/extras.py): the notification area's
# network, battery and volume icons, the folder task pane, Display
# Properties, XP's sounds, IE6, Windows Media Player, the Start menu's
# left column and Regional and Language Options, whose input languages
# start as the system's keyboard layout. Again when they change or after
# a Wine build.
if ! python3 "$root/prefix/extras.py" "$prefix" --check "$wine"; then
    printf 'Adding the XP Explorer additions (log: .local/prefix.log)\n'
    WINEPREFIX=$prefix "$(dirname "$wine")/wineserver" -k 2>/dev/null || true
    xvfb-run -a -s '-screen 0 1280x800x24' bash -c '
        python3 "$1/prefix/extras.py" "$2" --wine "$3" &&
        python3 "$1/session/linux_apps.py" "$2" --wine "$3" &&
        python3 "$1/xp-explorer/scripts/pins.py" "$2" --wine "$3"' _ "$root" "$prefix" "$wine" \
        >> "$local/prefix.log" 2>&1 ||
        { tail -20 "$local/prefix.log" >&2; printf 'The XP Explorer additions failed; see .local/prefix.log\n' >&2; exit 1; }
    WINEPREFIX=$prefix "$(dirname "$wine")/wineserver" -k 2>/dev/null || true
    WINEPREFIX=$prefix "$(dirname "$wine")/wineserver" -w 2>/dev/null || true
    printf 'Added\n'
else
    printf 'XP Explorer additions: up to date\n'
fi

step 'Session'
config=${XDG_CONFIG_HOME:-$HOME/.config}/win4wine/session.json
if [[ -f $config ]]; then
    # Kept, with the Wine and the language of the prefix (a new prefix may be
    # made with another Wine, or be in another language)
    python3 - "$config" "$locale" "$wine" <<'EOF'
import json, sys
config, locale, wine = sys.argv[1:]
settings = json.load(open(config))
changes = []
if settings.get('wine') != wine:
    settings['wine'] = wine
    changes.append(f'Wine {wine}')
if settings.get('locale') != locale:
    settings['locale'] = locale
    if locale.startswith('ja'):
        settings['input_method'] = 'fcitx5'
    else:
        settings.pop('input_method', None)
    changes.append(f'locale {locale}')
if changes:
    open(config, 'w').write(json.dumps(settings, indent=2) + '\n')
    print(f'Updated {config}: {", ".join(changes)}')
else:
    print(f'Keeping {config}')
EOF
else
    mkdir -p "$(dirname "$config")"
    python3 - "$config" "$wine" "$prefix" "$locale" <<'EOF'
import json, sys
config, wine, prefix, locale = sys.argv[1:]
settings = {'wine': wine, 'prefix': prefix, 'locale': locale}
if locale.startswith('ja'):
    settings['input_method'] = 'fcitx5'
open(config, 'w').write(json.dumps(settings, indent=2) + '\n')
EOF
    printf 'Wrote %s\n' "$config"
fi
if [[ $wayland == yes ]]; then
    "$root/session/install.sh" --wayland "$labwc"
else
    "$root/session/install.sh"
fi

step 'Virtual machines'
# Without a GPU of its own, X's modesetting driver draws straight into the
# graphics memory of these virtual cards, and every window moved is read back
# from it, which is slow (most of the X server's work while dragging a window
# in QEMU). Drawing in memory first (ShadowFB) leaves only writes to it.
# Cards whose memory is the guest's own (virtio-gpu, VMware's) need nothing.
drivers=()
for card in /sys/class/drm/card*/device/driver; do
    [[ -e $card ]] || continue
    driver=$(basename "$(readlink -f "$card")")
    case $driver in
        bochs-drm|bochs|hyperv_drm|vboxvideo|cirrus|cirrus-qemu) drivers+=("$driver") ;;
    esac
done
xorg_conf=/etc/X11/xorg.conf.d/20-win4wine-shadowfb.conf
if [[ ${#drivers[@]} -eq 0 ]]; then
    printf 'No virtual graphics card that needs it\n'
elif [[ $shadowfb == no ]]; then
    printf 'Left X as it is (--no-shadowfb)\n'
else
    sudo mkdir -p /etc/X11/xorg.conf.d
    sudo tee "$xorg_conf" >/dev/null <<EOF
# Written by win4wine's scripts/setup.sh: X draws in memory first on virtual
# graphics cards whose memory is slow to read back (moving windows). Remove
# this file to undo; it applies to every X session from the next start of X.
Section "OutputClass"
    Identifier "win4wine: shadow framebuffer on virtual graphics cards"
    MatchDriver "$(IFS='|'; echo "${drivers[*]}")"
    Driver "modesetting"
    Option "ShadowFB" "true"
EndSection
EOF
    printf 'Wrote %s (%s)\n' "$xorg_conf" "${drivers[*]}"
fi

step 'Login screen and cursors'
if [[ $greeter == yes ]]; then
    "$root/greeter/install.sh" "$assets" "$([[ $wayland == yes ]] && echo xp-shell-wayland || echo xp-shell)"
    if [[ -n $dpi ]]; then
        # XP's Welcome screen takes the machine's DPI.
        sudo python3 - "$dpi" <<'EOF'
import json, sys
path = '/etc/win4wine/greeter.json'
config = json.load(open(path))
config['dpi'] = int(sys.argv[1])
open(path, 'w').write(json.dumps(config) + '\n')
EOF
    fi
else
    # XP's cursors for the session only; LightDM, if any, keeps its own greeter.
    theme=$(mktemp -d)
    python3 "$root/greeter/cursors.py" "$assets/resources/user32.dll" "$theme/win4wine-xp"
    sudo rm -rf /usr/share/icons/win4wine-xp
    sudo cp -r "$theme/win4wine-xp" /usr/share/icons/
    sudo chmod -R a+rX /usr/share/icons/win4wine-xp
    rm -rf "$theme"
    printf 'Installed XP cursors; the display manager keeps its login screen\n'
fi

step 'Done'
if [[ $greeter == yes ]]; then
    printf 'Restart the display manager (or reboot): the XP Welcome screen logs in to the XP shell.\n'
else
    printf 'Restart the display manager (or reboot) and choose "XP Shell (Wine, X11)" at the login screen.\n'
fi
if [[ $wayland == yes ]]; then
    printf 'The Wayland session is "XP Shell (Wine, Wayland)"; outputs and scales: session.json "outputs".\n'
fi
printf 'Logs of the session: ~/.local/state/win4wine/\n'
