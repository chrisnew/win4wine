#!/bin/bash
# Build Wine for a Debian 13 machine on another computer, in a Debian 13
# container (Docker), and optionally copy it to that machine.
#
#   wine/container-build.sh [--jobs N] [--deploy HOST[:CHECKOUT]]
#
# The container runs wine/build.sh on this checkout, so the build, its source
# and its install are this checkout's .local/wine-{upstream,build,install},
# made with Debian 13's compilers and libraries: the install runs on any
# Debian 13 x86_64 machine that has setup.sh's packages (setup.sh
# --wine-install, or --deploy). Builds after the first one recompile only what
# changed patches touch.
#
#   --jobs N       parallel compile jobs (default: the number of CPUs here)
#   --deploy HOST[:CHECKOUT]
#                  copy the install over ssh to HOST's checkout (default
#                  ~/win4wine) as .local/wine-install, where setup.sh and the
#                  session find it. The old install is kept as
#                  .local/wine-install-old. While HOST runs a session from that
#                  install the copy waits in .local/wine-install.incoming; run
#                  --deploy again after logging out there to swap it in.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
image=win4wine-wine-build:trixie
jobs=$(nproc)
deploy=
while (($#)); do
    case $1 in
        --jobs) jobs=${2:?--jobs needs a number}; shift 2 ;;
        --deploy) deploy=${2:?--deploy needs a host}; shift 2 ;;
        -h|--help) sed -n '2,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

# The Wine build packages of scripts/setup.sh (Debian): keep these in step
# with it, so that configure finds the same libraries as a build there.
# The kernel headers come from trixie-backports: Debian 13's (6.12) have a
# linux/ntsync.h without NTSYNC_IOC_EVENT_READ, and Wine then builds without
# ntsync. The install still runs on 6.12, where /dev/ntsync is missing and
# Wine keeps to the wineserver's synchronization.
echo '>>> Preparing the Debian 13 build image'
docker build -q -t "$image" - <<'EOF' >/dev/null
FROM debian:trixie
RUN echo 'deb http://deb.debian.org/debian trixie-backports main' > /etc/apt/sources.list.d/backports.list \
    && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential flex bison pkg-config git ca-certificates gettext \
        libx11-dev libxext-dev libxrender-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev \
        libfreetype-dev libfontconfig-dev libgnutls28-dev libgl-dev libasound2-dev libdbus-1-dev libudev-dev \
        gcc-mingw-w64-i686 gcc-mingw-w64-x86-64 \
        python3 \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends -t trixie-backports linux-libc-dev \
    && grep -q NTSYNC_IOC_EVENT_READ /usr/include/linux/ntsync.h \
    && rm -rf /var/lib/apt/lists/*
EOF

# The checkout at the same path inside, so that the paths in the build
# (compile_commands.json, debug information) are this computer's.
# Files it writes belong to this user: rootless Docker maps the container's
# root to this user; a daemon running as root needs this user's IDs.
user=()
docker info --format '{{.SecurityOptions}}' | grep -q rootless || user=(--user "$(id -u):$(id -g)")
mkdir -p "$root/.local"
docker run --rm --init \
    "${user[@]}" -e HOME=/tmp -e JOBS="$jobs" \
    -v "$root:$root" -w "$root" \
    "$image" wine/build.sh 2>&1 | tee "$root/.local/wine-build.log"

[[ -n $deploy ]] || exit 0

install=$root/.local/wine-install
host=${deploy%%:*}
checkout=win4wine
[[ $deploy != *:* ]] || checkout=${deploy#*:}
patches=$(cat "$install/win4wine-patches")
# The remote side, given the checkout and this install's patch digest: swap
# a staged copy in (exit 0), or say why not (exit 3: none staged, 4: in use).
swap='
set -eu
cd "$1/.local"
[ -f wine-install.incoming/win4wine-patches ] &&
    [ "$(cat wine-install.incoming/win4wine-patches)" = "$2" ] || exit 3
for pid in $(pgrep -x wineserver || true); do
    case $(readlink "/proc/$pid/exe" 2>/dev/null) in
        "$PWD"/wine-install/*) exit 4 ;;
    esac
done
rm -rf wine-install-old
[ ! -d wine-install ] || mv wine-install wine-install-old
mv wine-install.incoming wine-install
'
remote_patches=$(ssh "$host" "cd $checkout && python3 wine/patch-state.py --digest wine/patches")
[[ $remote_patches == "$patches" ]] ||
    printf 'Note: %s:%s has another patch series (another commit); its setup.sh will want to rebuild\n' "$host" "$checkout" >&2
status=0
ssh "$host" sh -s -- "$checkout" "$patches" <<<"$swap" || status=$?
if ((status == 3)); then
    echo ">>> Copying the install to $host:$checkout/.local/wine-install.incoming"
    ssh "$host" "rm -rf $checkout/.local/wine-install.incoming && mkdir -p $checkout/.local/wine-install.incoming"
    tar -C "$install" -cf - . | zstd -q -T0 -3 |
        ssh "$host" "zstd -q -d | tar -C $checkout/.local/wine-install.incoming -xf -"
    status=0
    ssh "$host" sh -s -- "$checkout" "$patches" <<<"$swap" || status=$?
fi
case $status in
    0) echo ">>> $host now has this Wine in $checkout/.local/wine-install (the previous one: wine-install-old)" ;;
    4) echo ">>> $host runs a session with its current Wine; log out there, then run --deploy again to swap it in" ;;
    *) exit "$status" ;;
esac
