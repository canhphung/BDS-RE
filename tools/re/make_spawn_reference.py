#!/usr/bin/env python3
"""Build tests/reference/spawn_cluster_bds_1.6.0.15.json from a
capture_level_random.py --spawn-oracle run.

    source tools/re/env.sh
    python3 tools/re/make_spawn_reference.py TRACE_DIR OUT.json

Per spawn event (Spawner::postProcessSpawnMobs, or Spawner::tick past its
nextInt(2000) gate) it records the level-Random draw index at entry, the chunk,
the original's nearest-player answers inside isSpawnPositionOk, the exact
level-Random words the event consumed, and the mobs it spawned.
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
events = json.loads((trace / "spawns_events.json").read_text())
spawns = json.loads((trace / "spawns.json").read_text())["spawns"]
metadata = json.loads((trace / "metadata.json").read_text())


SPAWN_PATH = ("Spawner::postProcessSpawnMobs", "Spawner::_spawnMobCluster", "Spawner::popCapAllows",
              "MobSpawnRules::getSpawnCount", "MobSpawnUtils::", "BlockLegacy::getTypeToSpawn",
              "GrassBlock::getMobToSpawn", "BlockLegacy::getMobToSpawn")


def belongs(draw, first):
    # The consecutive draws of the spawn path. The trace keeps six callers, so
    # deep draws (WeighedRandom under getTypeToSpawn) do not show the entry;
    # the run ends at the first draw from any other consumer. Spawner::tick
    # draws past its gate (+0x7d) also belong; the gate itself starts the
    # next chunk's run.
    for pc in draw["callers"]:
        n = name(pc)
        if n.startswith("Spawner::tick(BlockSource&, LevelChunk&)"):
            return pc - addrs[bisect.bisect_right(addrs, pc) - 1] != 0x7d
        if n.startswith(SPAWN_PATH) or "WeighedRandom::getRandomItem<MobSpawnerData" in n:
            return True
    return False


out = []
for i, event in enumerate(events):
    if event["kind"] not in ("post_process", "spawner_tick"):
        continue
    start = event["draw"]
    words = []
    index = start
    while index < len(draws) and belongs(draws[index], index == start):
        words.append(draws[index]["value"])
        index += 1
    # The isSpawnPositionOk answers and the entities each
    # _updateBaseTypeCount call counted (type, position), up to the next event.
    nearest, counts = [], []
    for later in events[i + 1:]:
        if later["kind"] in ("post_process", "spawner_tick"):
            break
        if later["kind"] == "nearest":
            nearest.append(later.get("distance_sqr") if later["found"] else None)
        if later["kind"] == "count":
            counts.append([[int(m["type"], 16)] + m["position"] for m in later["mobs"]])
    out.append({"kind": event["kind"], "tick": event["tick"], "draw": start,
                "chunk": [event["x"] >> 4, event["z"] >> 4], "nearest": nearest, "counts": counts,
                "words": words})
fixture = {
    "binary_sha256": metadata["binary_sha256"],
    "trace_sha256": metadata["trace_sha256"],
    "generator": "tools/re/capture_level_random.py --spawn-oracle; tools/re/make_spawn_reference.py",
    "seed": int(metadata["properties"]["level-seed"]),
    "difficulty": 2,
    "spawns": [{"tick": s["tick"], "identifier": s["identifier"]["identifier"], "position": s["position"]}
               for s in spawns],
    "events": out,
}
Path(sys.argv[2]).write_text(json.dumps(fixture, indent=1) + "\n")
print(len(out), "events,", sum(len(e["words"]) for e in out), "words,", len(spawns), "spawns")
