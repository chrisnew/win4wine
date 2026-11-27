# The XP session on Xvfb, for tools/bench/bench.sh. Built on the image of
# wine/container-build.sh, so it has the same Debian 13 libraries as the Wine
# install it runs, and MinGW for the bridge.
FROM win4wine-wine-build:trixie
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        xvfb xauth xdotool x11-utils x11-apps x11-xserver-utils x11-xkb-utils xterm \
        python3-xlib python3-gi python3-gi-cairo python3-cairo \
        gir1.2-glib-2.0 gir1.2-gtk-3.0 gir1.2-pango-1.0 gir1.2-gdkpixbuf-2.0 librsvg2-common \
        libxcb1-dev cabextract p7zip-full locales dbus procps libgl1-mesa-dri mesa-utils \
    && sed -i 's/^# *\(en_US.UTF-8 UTF-8\)/\1/; s/^# *\(ja_JP.UTF-8 UTF-8\)/\1/' /etc/locale.gen && locale-gen \
    && useradd -m -s /bin/bash tester && echo 'tester:Secret ^1' | chpasswd \
    && rm -rf /var/lib/apt/lists/*
