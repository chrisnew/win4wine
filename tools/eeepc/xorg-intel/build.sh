#!/bin/bash
# Build Debian 13's xserver-xorg-video-intel with sna-gen3-dri3.patch, in a
# container, into .local/xorg-intel/ of the checkout.
#
# Install it on the EeePC with `sudo apt install ./xserver-xorg-video-intel_*.deb`
# and restart X (log out). It needs Option "DRI" "3" in a Device section of
# /etc/X11/xorg.conf.d/. Undo: `sudo apt install --reinstall
# xserver-xorg-video-intel=<Debian's version>`. `apt-mark hold` keeps apt from
# replacing it at the next update.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$here/../../.." && pwd)
out=$root/.local/xorg-intel
mkdir -p "$out"
user=()
docker info --format '{{.SecurityOptions}}' | grep -q rootless || user=(--user "$(id -u):$(id -g)")
docker build -q -t win4wine-xorg-intel:trixie - <<'DOCKERFILE' >/dev/null
FROM debian:trixie
RUN sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends dpkg-dev devscripts quilt \
    && DEBIAN_FRONTEND=noninteractive apt-get build-dep -y xserver-xorg-video-intel \
    && mkdir /pkg && cd /pkg && apt-get source --download-only xserver-xorg-video-intel \
    && chmod -R a+rX /pkg && rm -rf /var/lib/apt/lists/*
DOCKERFILE
docker run --rm "${user[@]}" -e HOME=/tmp -v "$here:/patch:ro" -v "$out:/out" -w /tmp \
    win4wine-xorg-intel:trixie bash -euc '
        dpkg-source -x /pkg/*.dsc src >/dev/null
        cd src
        cp /patch/sna-gen3-dri3.patch debian/patches/
        echo sna-gen3-dri3.patch >> debian/patches/series
        QUILT_PATCHES=debian/patches quilt push -a -q >/dev/null
        grep -q "sna->kgem.gen >= 040 || xf86IsOptionSet" src/sna/sna_driver.c
        DEBFULLNAME=win4wine DEBEMAIL=win4wine@localhost dch --local +w4w "SNA: DRI3 on gen3 with Option \"DRI\" \"3\"" >/dev/null
        dpkg-buildpackage -b -uc -us -j"$(nproc)" >/tmp/build.log 2>&1 || { tail -40 /tmp/build.log; exit 1; }
        cp ../xserver-xorg-video-intel_*.deb /out/
        ls /out'
