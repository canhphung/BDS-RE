#!/usr/bin/env python3
"""Copy a validated native vegetation oracle into a compact committed fixture.

Usage: export_vegetation_reference.py ORACLE_JSON OUT_JSON

The oracle (tools/re/gdb_vegetation_cases.py) constructs and calls the real
feature objects in a paused original server. This exporter validates the
provenance and copies the observations; it never computes an expected value.

Bulk block arrays are zlib-compressed and base64-encoded the same way the
terrain fixture stores its BlockVolumes; only cells that actually changed are
stored per case, and the parser below rebuilds the full picture.
"""
import base64
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

SHA = "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad"
FEATURES = None  # clay has a single feature
MINIMUM_CASES = 12


def encode_cells(cells, index_of):
    packed = bytearray()
    for x, y, z, name in cells:
        packed += struct.pack('<hhhH', x, y, z, index_of[name])
    return base64.b64encode(zlib.compress(bytes(packed), 9)).decode('ascii')


def decode_cells(text, names):
    raw = zlib.decompress(base64.b64decode(text))
    return [list(struct.unpack_from('<hhhH', raw, at)) for at in range(0, len(raw), 8)]


source, destination = Path(sys.argv[1]), Path(sys.argv[2])
raw = (source / "clay.json").read_bytes()
recorded = json.loads(raw)
cases = recorded["cases"]
if len(cases) < MINIMUM_CASES:
    raise SystemExit(f"incomplete oracle: {len(cases)} cases")

if not any(case["writes"] for case in cases):
    raise SystemExit("oracle recorded no placements")
KINDS = {"material_is_water"}
for case in cases:
    for dep in case["queries"]:
        if not {"kind", "position", "result"} <= set(dep):
            raise SystemExit("unexpected dependency shape")
        if dep["kind"] not in KINDS:
            raise SystemExit(f"unexpected dependency kind: {dep['kind']}")
        if dep["result"] not in (0, 1):
            raise SystemExit("dependency result is not boolean")

# Intern every block name once.
names = sorted({cell[3] for case in cases for cell in case["initial"]} |
               {cell[3] for case in cases for cell in case["final"]} |
               {write[3] for case in cases for write in case["writes"]})
index_of = {name: i for i, name in enumerate(names)}

# Deduplicate the initial scenes: the oracle reuses a handful of arenas.
scene_ids, scenes = {}, []
for case in cases:
    key = hashlib.sha256(json.dumps(case["initial"]).encode()).hexdigest()
    if key not in scene_ids:
        scene_ids[key] = len(scenes)
        scenes.append({"cells": encode_cells(case["initial"], index_of)})
    case["_scene"] = scene_ids[key]

compact = []
for case in cases:
    # The oracle stores `final` as a flat name list parallel to the coordinate
    # list it scanned, and `initial` as [x, y, z, name] rows in that same order.
    initial = case["initial"]
    changed = [[cell[0], cell[1], cell[2], index_of[final]]
               for cell, final in zip(initial, case["final"])
               if final != cell[3]]
    # Dependency answers are kept in call order. The host test replays them, so
    # the translated feature sees the original's own predicate results rather
    # than a survival rule this project would have to invent.
    queries = [[q["kind"], *q["position"], q["result"]] for q in case["queries"]]
    compact.append({
        "at": case["at"], "seed": case["seed"], "size": case["size"],
        "scene": case["_scene"], "result": case["result"],
        "words": case["words"],
        "writes": [[*w[:3], index_of[w[3]], w[4]] for w in case["writes"]],
        "changed": changed, "tail": case["tail"],
        "queries": queries})

fixture = {
    "binary_sha256": SHA,
    "generator": "tools/re/gdb_clay_cases.py + tools/re/export_clay_reference.py",
    "method": "Each case constructs the real ClayFeature with its native "
              "constructor, seeds a real original Random and calls the native "
              "place(). Every Random word, every dependency answer and every "
              "Feature::_placeBlock attempt are native observations.",
    "source_context": {
        "blocksource": "BlockSource(Level, Dimension, ChunkSource, true, true) "
                       "with BlockTickingQueue(Tick(1)); events suppressed",
        "scene": "a 24x24 dirt/stone/water column area, Y 2..25, reset through "
                 "the original BlockSource::setBlock with flag 0",
    },
    "encoding": "block_names interns the names; each scene is base64(zlib("
                "repeated <hhhH> x,y,z,name_index)); a case stores only the "
                "cells that changed from its scene.",
    "oracle_sha256": hashlib.sha256(raw).hexdigest(),
    "block_names": names,
    "scenes": scenes,
    "cases": compact,
}
destination.write_text(json.dumps(fixture, separators=(",", ":")) + "\n")

# Self-check: the compact form must reconstruct the observed final state.
for case, original in zip(compact, cases):
    rebuilt = {tuple(cell[:3]): cell[3] for cell in decode_cells(scenes[case["scene"]]["cells"], names)}
    for x, y, z, index in case["changed"]:
        rebuilt[(x, y, z)] = index
    observed = {(cell[0], cell[1], cell[2]): index_of[name]
                for cell, name in zip(original["initial"], original["final"])}
    if rebuilt != observed:
        raise SystemExit("compact fixture does not reproduce the native final state")

print(json.dumps({"cases": len(compact), "scenes": len(scenes),
                  "bytes": destination.stat().st_size,
                  "writes": sum(len(c["writes"]) for c in compact),
                  "words": sum(len(c["words"]) for c in compact), "out": str(destination)}))
