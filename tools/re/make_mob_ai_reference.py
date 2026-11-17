#!/usr/bin/env python3
"""Build tests/reference/mob_ai_bds_1.6.0.15.json from a capture made with
--spawn-oracle --gdb-script tools/re/gdb_mob_ai_oracle.py.

    source tools/re/env.sh
    python3 tools/re/make_mob_ai_reference.py TRACE_DIR OUT.json

Per mob: its type (matched with the spawn oracle by spawn position), attach
offsets, and for each AI step (Mob::newServerAiStep) the level-Random draw index
at entry, the words the goal selector drew, LookAtActorGoal::canUse results and
the LookControl result (pitch, head yaw). A mob's steps end with the first step
in which the original's RandomStrollGoal::canUse drew 0 (the stroll starts there
and path navigation is not translated yet).
"""
import bisect
import json
import os
import sys
from pathlib import Path

trace = Path(sys.argv[1])
addrs, names = [], []
for line in open(os.environ["BDS_SYMS"]):
    p = line.split(" ", 3)
    if len(p) == 4 and p[2] in ("T", "t", "W", "w"):
        addrs.append(int(p[0], 16))
        names.append(p[3].strip())
order = sorted(range(len(addrs)), key=addrs.__getitem__)
addrs = [addrs[i] for i in order]
names = [names[i] for i in order]


def name(pc):
    return names[bisect.bisect_right(addrs, pc) - 1]


draws = [json.loads(line) for line in (trace / "draws.jsonl").read_text().splitlines()]
draws = [d for d in draws if d["event"] == "draw"]
events = json.loads((trace / "mob_ai.json").read_text())
spawns = json.loads((trace / "spawns.json").read_text())["spawns"]
metadata = json.loads((trace / "metadata.json").read_text())
TYPES = {"chicken": 0x130a, "cow": 0x130b, "pig": 0x130c, "sheep": 0x130d, "horse": 0x205317, "donkey": 0x205318}


def goal_draw(draw):
    return any("Goal::" in name(pc) or name(pc).startswith("GoalSelector::") for pc in draw["callers"])


def stroll_zero(draw):
    return any(name(pc).startswith("RandomStrollGoal::canUse") for pc in draw["callers"][:3]) and draw["value"] % 120 == 0


ai_draws = sorted(e["draw"] for e in events if e["kind"] == "ai")
mobs, done = {}, set()
current = None
for event in events:
    if event["kind"] == "ai":
        mob = event["mob"]
        if mob in done:
            current = None
            continue
        if mob not in mobs:
            kind = next((s["identifier"]["identifier"] for s in spawns
                         if s["position"] == event["pos"]), None)
            if kind not in TYPES:
                current = None
                continue
            mobs[mob] = {"type": TYPES[kind], "position": event["pos"], "steps": []}
        words, index = [], event["draw"]
        # The next mob's AI step starts at the next marker.
        limit = next((d for d in ai_draws if d > event["draw"]), len(draws))
        while index < limit and goal_draw(draws[index]):
            words.append(draws[index]["value"])
            index += 1
        current = {"tick": event["tick"], "draw": event["draw"], "position": event["pos"], "words": words,
                   "look_at": []}
        mobs[mob]["steps"].append(current)
        if any(stroll_zero(draws[i]) for i in range(event["draw"], index)):
            done.add(mob)
            current["stroll_start"] = True
    elif current is not None and event["kind"] == "look_at_can_use":
        current["look_at"].append(event["result"])
    elif current is not None and event["kind"] == "look" and "head_after" in event:
        mob = mobs.get(event["mob"])
        if mob is not None and "attach_head" not in mob:
            mob["attach_head"], mob["attach_eyes"] = event["attach3"], event["attach7"]
        current["look"] = [event["pitch_after"], event["head_after"]]
out = {"binary_sha256": metadata["binary_sha256"], "trace_sha256": metadata["trace_sha256"],
       "generator": "capture_level_random.py --spawn-oracle --gdb-script gdb_mob_ai_oracle.py; make_mob_ai_reference.py",
       "seed": int(metadata["properties"]["level-seed"]),
       "mobs": [m for m in mobs.values() if m["steps"] and "attach_head" in m]}
Path(sys.argv[2]).write_text(json.dumps(out) + "\n")
print(len(out["mobs"]), "mobs,", sum(len(m["steps"]) for m in out["mobs"]), "steps,",
      sum(len(s["words"]) for m in out["mobs"] for s in m["steps"]), "words")
