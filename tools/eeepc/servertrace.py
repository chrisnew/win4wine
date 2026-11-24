"""Summarise a wineserver +server trace: requests per thread in the last N lines, with each thread's process.
servertrace.py LOG [TAIL_LINES]"""
import re, sys
from collections import Counter, defaultdict
from pathlib import Path
lines = Path(sys.argv[1]).read_text(errors='replace').splitlines()
tail = int(sys.argv[2]) if len(sys.argv) > 2 else 60000
# Wine thread id -> unix pid/tid (the request line's id is the caller's Wine tid)
unix = {}
for l in lines:
    m = re.match(r'(\w{4}): init_(?:first_)?thread\( unix_pid=(\d+), unix_tid=(\d+)', l)
    if m: unix[m.group(1)] = (int(m.group(2)), int(m.group(3)))
def name(pid):
    try: return Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')[0].decode(errors='replace').split('\\')[-1][:30]
    except OSError: return '?'
reqs = defaultdict(Counter)
for l in lines[-tail:]:
    m = re.match(r'(\w{4}): (\w+)\(', l)
    if m and ') =' not in l[:80]:
        reqs[m.group(1)][m.group(2)] += 1
total = sum(sum(c.values()) for c in reqs.values())
print(f'{total} requests in the last {tail} lines')
for tid, c in sorted(reqs.items(), key=lambda kv: -sum(kv[1].values()))[:8]:
    pid, utid = unix.get(tid, (0, 0))
    print(f'{tid} unix {pid}/{utid} {name(pid)}: {sum(c.values())}  ' + ', '.join(f'{k} {v}' for k, v in c.most_common(8)))
