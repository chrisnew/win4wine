#!/bin/bash
# The whole XP session on this computer: Debian 13 in Docker, Xvfb at the
# EeePC's 1024x600, this checkout's Wine (.local/wine-install) and XP files
# (.local/assets). For seeing and testing a change before it goes to the
# EeePC; it is not a measure of speed (this CPU is many times the Atom's).
#
#   tools/bench/bench.sh image              build the image (once; needs the image
#                                           of wine/container-build.sh)
#   tools/bench/bench.sh up NAME            a container with a copy of this checkout,
#                                           the bridge built and a new prefix (5-10 min)
#   tools/bench/bench.sh sync NAME          copy the checkout's files again and rebuild
#                                           the bridge (the session must be stopped;
#                                           prefix additions: "prefix NAME")
#   tools/bench/bench.sh prefix NAME        run the prefix additions again (extras.py,
#                                           linux_apps.py, pins.py), as setup.sh does
#   tools/bench/bench.sh start NAME [DEBUG] Xvfb and the session; waits for the XP shell.
#                                           DEBUG goes to session.json's "debug"
#   tools/bench/bench.sh stop NAME          end the session and the wineserver
#   tools/bench/bench.sh sh NAME < SCRIPT   run SCRIPT as the user, in the checkout, with
#                                           DISPLAY, P (prefix) and W (wine) set
#   tools/bench/bench.sh drive NAME ACTION... tools/eeepc/drive.py on the bench's display
#   tools/bench/bench.sh shot NAME OUT.png [CROP]  screenshot (CROP: WxH+X+Y)
#   tools/bench/bench.sh logs NAME          the session's logs
#   tools/bench/bench.sh rm NAME            remove the container
#
# Inside: user tester (password "Secret ^1", for the lock screen), checkout
# /home/tester/win4wine, display :5, logs in ~/.local/state/win4wine/.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
image=w4w-bench:trixie
home=/home/tester
checkout=$home/win4wine

usage() { sed -n '2,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit "${1:-2}"; }
cmd=${1:-}; [[ -n $cmd ]] || usage
shift
[[ $cmd == image || $cmd == -h || $cmd == --help ]] || { name=${1:?a container name}; shift; }

# A script for the user inside, with the session's environment.
as_user() {
    { printf '%s\n' "set -e; cd $checkout" \
        "export HOME=$home USER=tester LANG=en_US.UTF-8 XDG_RUNTIME_DIR=$home/run DISPLAY=:5" \
        "P=$checkout/.local/prefix; W=$checkout/.local/wine-install/bin/wine; S=$checkout/.local/wine-install/bin/wineserver"
      cat; } | docker exec -i -u tester "$name" bash -s
}

copy_checkout() {
    tar -C "$root" --exclude=./.local --exclude=./localres --exclude=./.git -cf - . |
        docker exec -i "$name" tar -C "$checkout" -xf -
    docker exec "$name" chown -R tester:tester "$checkout"
}

case $cmd in
image)
    docker image inspect win4wine-wine-build:trixie >/dev/null 2>&1 ||
        { echo 'No image win4wine-wine-build:trixie: run wine/container-build.sh once' >&2; exit 1; }
    docker build -q -t "$image" "$root/tools/bench" >/dev/null
    echo "built $image"
    ;;
up)
    [[ -x $root/.local/wine-install/bin/wine && -d $root/.local/assets ]] ||
        { echo 'Needs .local/wine-install (wine/container-build.sh) and .local/assets (extract/extract.py)' >&2; exit 1; }
    docker image inspect "$image" >/dev/null 2>&1 || "$0" image
    device=(); [[ -e /dev/ntsync ]] && device=(--device /dev/ntsync)
    docker run -d --init --name "$name" --hostname "$name" --shm-size=256m "${device[@]}" \
        -v "$root/.local/wine-install:/opt/wine:ro" -v "$root/.local/assets:/opt/assets:ro" \
        "$image" sleep infinity >/dev/null
    docker exec "$name" sh -c "mkdir -p $checkout/.local && ln -s /opt/wine $checkout/.local/wine-install &&
        ln -s /opt/assets $checkout/.local/assets && install -d -m 700 -o tester -g tester $home/run"
    copy_checkout
    docker exec "$name" install -m 644 "$checkout/greeter/win4wine-lock.pam" /etc/pam.d/win4wine-lock
    as_user <<'EOF'
bridge-wm/build.sh > ~/bridge-build.log 2>&1 || { tail -20 ~/bridge-build.log; exit 1; }
mkdir -p ~/.config/win4wine
printf '{"wine": "%s", "prefix": "%s", "locale": "en_US.UTF-8"}\n' "$W" "$P" > ~/.config/win4wine/session.json
xv() { xvfb-run -a -s '-screen 0 1024x600x24' "$@"; }
{
    xv python3 prefix/create.py .local/assets "$P" --home "$HOME" --wine "$W"
    WINEPREFIX=$P WINE=$W xv python3 prefix/display.py --theme luna --menu xp
    WINEPREFIX=$P $S -w || true
    xv bash -ec 'python3 prefix/extras.py "$1" --wine "$2"; python3 session/linux_apps.py "$1" --wine "$2"
        python3 xp-explorer/scripts/pins.py "$1" --wine "$2"' _ "$P" "$W"
    WINEPREFIX=$P $S -k || true
    WINEPREFIX=$P $S -w || true
} > ~/prefix.log 2>&1 || { tail -30 ~/prefix.log; exit 1; }
echo "prefix ready: $P"
EOF
    ;;
sync)
    copy_checkout
    as_user <<'EOF'
bridge-wm/build.sh > ~/bridge-build.log 2>&1 || { tail -20 ~/bridge-build.log; exit 1; }
echo synced
EOF
    ;;
prefix)
    as_user <<'EOF'
WINEPREFIX=$P $S -k 2>/dev/null || true
xvfb-run -a -s '-screen 0 1024x600x24' bash -ec "python3 prefix/extras.py $P --wine $W; python3 session/linux_apps.py $P --wine $W;
    python3 xp-explorer/scripts/pins.py $P --wine $W" > ~/prefix.log 2>&1 || { tail -30 ~/prefix.log; exit 1; }
WINEPREFIX=$P $S -w || true
echo 'prefix additions done'
EOF
    ;;
start)
    debug=${1:-}
    as_user <<EOF
python3 - '$debug' <<'PY'
import json, os, sys
path = os.path.expanduser('~/.config/win4wine/session.json')
config = json.load(open(path))
config.pop('debug', None)
if sys.argv[1]: config['debug'] = sys.argv[1]
json.dump(config, open(path, 'w'))
PY
rm -f ~/.local/state/win4wine/status.json
xdpyinfo >/dev/null 2>&1 || { rm -f /tmp/.X5-lock /tmp/.X11-unix/X5; setsid Xvfb :5 -screen 0 1024x600x24 -nolisten tcp > ~/xvfb.log 2>&1 < /dev/null & }
for i in \$(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
EOF
    docker exec -d -u tester -w "$checkout" -e HOME=$home -e USER=tester -e LANG=en_US.UTF-8 \
        -e XDG_RUNTIME_DIR=$home/run -e DISPLAY=:5 "$name" \
        sh -c 'exec dbus-run-session -- python3 session/supervisor.py > ~/supervisor.log 2>&1'
    as_user <<'EOF'
for i in $(seq 180); do
    grep -q '"state": "xp-shell"' ~/.local/state/win4wine/status.json 2>/dev/null && { echo "XP shell up after ${i} s"; exit 0; }
    sleep 1
done
echo 'no XP shell after 180 s'; tail -20 ~/supervisor.log; exit 1
EOF
    ;;
stop)
    as_user <<'EOF'
pkill -TERM -f '[s]ession/supervisor.py' || true
for i in $(seq 20); do pgrep -f '[s]ession/supervisor.py' >/dev/null || break; sleep 0.5; done
WINEPREFIX=$P $S -k 2>/dev/null || true
WINEPREFIX=$P timeout 30 $S -w 2>/dev/null || true
# dbus-run-session's daemons stay behind; each holds inotify instances, and after
# some restarts the supervisor gets none (errno 24).
pkill -x -u tester dbus-daemon || true
echo stopped
EOF
    ;;
sh)
    as_user
    ;;
drive)
    printf 'python3 tools/eeepc/drive.py %s\n' "$(printf '%q ' "$@")" | as_user
    ;;
shot)
    out=${1:?OUT.png}; crop=${2:-}
    echo 'python3 tools/eeepc/shot.py /dev/stdout' | as_user |
        magick ppm:- ${crop:+-crop "$crop" +repage} -strip "$out"
    echo "$out ($(magick identify -format '%wx%h' "$out"))"
    ;;
logs)
    echo 'for f in ~/supervisor.log ~/.local/state/win4wine/*.log; do echo "== $f"; tail -40 "$f"; done' | as_user
    ;;
rm)
    docker rm -f "$name" >/dev/null && echo "removed $name"
    ;;
-h|--help) usage 0 ;;
*) usage ;;
esac
