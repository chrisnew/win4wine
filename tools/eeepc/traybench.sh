#!/bin/bash
# traybench.sh BRIDGE_DIR: tray.py's CPU while geometry.tsv is renamed into its IPC directory 600 times at 60 Hz.
bridge=$1; ipc=$(mktemp -d); chmod 700 $ipc
Xvfb :98 -screen 0 800x600x24 -nolisten tcp >/dev/null 2>&1 & xvfb=$!; sleep 1
DISPLAY=:98 dbus-run-session -- python3 "$bridge/tray.py" --ipc $ipc >/dev/null 2>&1 & sleep 3
pid=$(pgrep -n -f "$bridge/tray.py --ipc $ipc")
t0=$(awk '{print $14+$15}' /proc/$pid/stat)
python3 - $ipc <<'PY'
import sys, time
from pathlib import Path
ipc = Path(sys.argv[1]); t0 = time.monotonic()
for i in range(600):
    tmp = ipc / 'geometry.tmp'; tmp.write_text(f'2800017 {i} 100 400 300 0 0 0 1024 570\n'); tmp.replace(ipc / 'geometry.tsv')
    time.sleep(max(0, t0 + (i + 1) / 60 - time.monotonic()))
PY
t1=$(awk '{print $14+$15}' /proc/$pid/stat)
echo "tray.py CPU over 10 s of drag writes: $(( (t1 - t0) * 10 )) ms ($(( (t1 - t0) * 10 / 10 ))%)"
kill $pid; sleep .5; kill $xvfb; rm -rf $ipc
