#!/bin/bash
# wmbench.sh WM_PY [STEPS] [HZ]: profile a WM on Xvfb while a fake frame is dragged.
set -e
wm=$1; steps=${2:-600}; hz=${3:-60}
ipc=/tmp/w4w-ipc; rm -rf $ipc; mkdir -m 700 $ipc
Xvfb :97 -screen 0 1024x600x24 -nolisten tcp >/dev/null 2>&1 & xvfb=$!
sleep 1
DISPLAY=:97 python3 ${PROFILE:+-m cProfile -o /tmp/w4w-wm.prof} "$wm" --ipc $ipc & wmpid=$!
sleep 2
t0=$(awk '{print $14+$15}' /proc/$(pgrep -f -n "python3.*$wm --ipc")/stat)
DISPLAY=:97 python3 /tmp/w4w-wmbench.py $ipc $steps $hz
t1=$(awk '{print $14+$15}' /proc/$(pgrep -f -n "python3.*$wm --ipc")/stat)
echo "WM CPU during the drag: $(( (t1 - t0) * 10 )) ms ($(( (t1 - t0) * 10000 / steps )) us per step)"
kill $wmpid; wait $wmpid 2>/dev/null || true
kill $xvfb; wait $xvfb 2>/dev/null || true
[ -n "$PROFILE" ] && python3 -c "
import pstats; p = pstats.Stats('/tmp/w4w-wm.prof'); p.sort_stats('tottime').print_stats(18)" | sed -n '/ncalls/,$p' | cut -c1-150
