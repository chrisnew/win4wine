#!/bin/bash
# The EeePC from this computer, over ssh. The host is $EEEPC, or the first
# line of .local/eeepc-host (not in Git).
#
#   tools/eeepc/eee.sh drive ACTION...      drive.py on its display (see drive.py --help)
#   tools/eeepc/eee.sh shot OUT.png [CROP]  screenshot of its display, as PNG here (CROP: WxH+X+Y)
#   tools/eeepc/eee.sh sample ARGS...       sample.py there (CPU and wakeups per process)
#   tools/eeepc/eee.sh insession SCRIPT [SECONDS]
#                                           run the shell script SCRIPT (a file here) inside
#                                           the logind session, through Start, Run and
#                                           linux-launch.exe, and print what it wrote.
#                                           For what needs "the active local session"
#                                           (polkit: udisksctl, loginctl); ssh is not one
#   tools/eeepc/eee.sh x 'COMMAND'          a shell command there with DISPLAY and XAUTHORITY
#                                           set, in the checkout
#
# Each call copies drive.py, shot.py and sample.py to /tmp/w4w-*.py there.
# Input from drive and insession lands in the user's live session: ask first,
# and have the user leave the machine alone meanwhile.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
env='export DISPLAY=:0 XAUTHORITY=$HOME/.Xauthority; cd ~/win4wine'

push() {
    tar -C "$root/tools/eeepc" -cf - drive.py shot.py sample.py |
        ssh "$host" 'd=$(mktemp -d) && tar -C "$d" -xf - && for f in "$d"/*.py; do mv -f "$f" /tmp/w4w-${f##*/}; done && rmdir "$d"'
}
remote() { ssh "$host" "$env; $(printf '%q ' "$@")"; }

cmd=${1:-}; shift || true
if [[ $cmd != -h && $cmd != --help && -n $cmd ]]; then
    host=${EEEPC:-$(head -n1 "$root/.local/eeepc-host" 2>/dev/null || true)}
    [[ -n $host ]] || { echo 'Set EEEPC=HOST or write the host into .local/eeepc-host' >&2; exit 2; }
fi
case $cmd in
drive) push; remote python3 /tmp/w4w-drive.py "$@" ;;
sample) push; remote python3 /tmp/w4w-sample.py "$@" ;;
shot)
    out=${1:?OUT.png}; crop=${2:-}
    push
    remote python3 /tmp/w4w-shot.py /dev/stdout | magick ppm:- ${crop:+-crop "$crop" +repage} -strip "$out"
    echo "$out ($(magick identify -format '%wx%h' "$out"))"
    ;;
insession)
    script=${1:?SCRIPT}; limit=${2:-120}
    push
    # The script's output and its status go to /tmp/w4w-run.log; the last line is "exit N".
    { printf 'exec > /tmp/w4w-run.log 2>&1\n(\n'; cat "$script"; printf '\n)\necho "exit $?"\n'; } |
        ssh "$host" 'rm -f /tmp/w4w-run.log; cat > /tmp/w4w-run.sh'
    remote python3 /tmp/w4w-drive.py chord Super_L+r sleep 2 key Home chord Shift_L+End \
        type 'C:\xp\bin\linux-launch.exe sh /tmp/w4w-run.sh' sleep 0.5 key Return
    ssh "$host" "for i in \$(seq $((limit * 2))); do tail -n1 /tmp/w4w-run.log 2>/dev/null | grep -q '^exit [0-9]*$' && break; sleep 0.5; done
        cat /tmp/w4w-run.log 2>/dev/null || echo 'the script did not start (is a session logged on?)'"
    ;;
x) ssh "$host" "$env; $*" ;;
-h|--help|'') sed -n '2,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//' ;;
*) echo "unknown command $cmd" >&2; exit 2 ;;
esac
