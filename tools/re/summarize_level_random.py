#!/usr/bin/env python3
"""summarize_level_random.py TRACE_DIR [FIRST_TICK LAST_TICK] - per tick, runs of
consecutive level-Random draws grouped by their first non-Random caller."""
import bisect, json, os, sys
syms_path = os.environ["BDS_SYMS"]
addrs, names = [], []
for line in open(syms_path):
    p = line.split(" ", 3)
    if len(p) == 4 and p[2] in ("T", "t", "W", "w"):
        addrs.append(int(p[0], 16)); names.append(p[3].strip())
order = sorted(range(len(addrs)), key=addrs.__getitem__)
addrs = [addrs[i] for i in order]; names = [names[i] for i in order]
def name(pc):
    i = bisect.bisect_right(addrs, pc) - 1
    return names[i] if i >= 0 else hex(pc)
def consumer(callers):
    for pc in callers:
        n = name(pc)
        if not n.startswith(("Random::", "std::", "unsigned long std::")):
            return n.split("(")[0]
    return name(callers[-1]).split("(")[0]
lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
hi = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
runs = []
for line in open(os.path.join(sys.argv[1], "draws.jsonl")):
    r = json.loads(line)
    if r["event"] in ("initialized",):
        runs.append((r.get("tick"), "== initialized " + str(r.get("name", "")), 0))
    if r["event"] != "draw" or not lo <= r["tick"] <= hi:
        continue
    c = consumer(r["callers"])
    if runs and runs[-1][0] == r["tick"] and runs[-1][1] == c:
        runs[-1] = (r["tick"], c, runs[-1][2] + 1)
    else:
        runs.append((r["tick"], c, 1))
for tick, c, n in runs:
    print(tick, n, c)
