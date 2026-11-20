#!/usr/bin/env python3
"""Copy a validated native BonusChestFeature oracle into a compact fixture.

Usage: export_bonus_chest_reference.py ORACLE_DIR OUT_JSON

The oracle (tools/re/gdb_bonus_chest_cases.py) constructs the real feature and
calls the real place() in a paused original server. This exporter validates the
provenance and copies the observations; it never computes an expected value.
"""
import base64
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

SHA = "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad"
MINIMUM_CASES = 10
KINDS = {"top_solid", "is_empty", "solid_below"}


def encode_cells(cells, index_of):
    packed = bytearray()
    for x, y, z, name in cells:
        packed += struct.pack('<hhhH', x, y, z, index_of[name])
    return base64.b64encode(zlib.compress(bytes(packed), 9)).decode('ascii')


source, destination = Path(sys.argv[1]), Path(sys.argv[2])
raw = (source / "bonus_chest.json").read_bytes()
recorded = json.loads(raw)
cases = recorded["cases"]
if len(cases) < MINIMUM_CASES:
    raise SystemExit(f"incomplete oracle: {len(cases)} cases")
if not any(case["writes"] for case in cases):
    raise SystemExit("oracle recorded no placements")
for case in cases:
    # BonusChestFeature creates its own Random copy, so the caller's stream must
    # stay untouched. This is the property the translation has to reproduce.
    if case["words"]:
        raise SystemExit("the caller's Random was consumed; expected an untouched stream")
    for query in case["queries"]:
        if set(query) < {"kind", "position", "result"}:
            raise SystemExit("unexpected query shape")
        if query["kind"] not in KINDS:
            raise SystemExit(f"unexpected query kind: {query['kind']}")

names = sorted({cell[3] for case in cases for cell in case["initial"]} |
               {cell[3] for case in cases for cell in case["final"]} |
               {write[3] for case in cases for write in case["writes"]})
index_of = {name: i for i, name in enumerate(names)}

scene_ids, scenes = {}, []
compact = []
for case in cases:
    key = hashlib.sha256(json.dumps(case["initial"]).encode()).hexdigest()
    if key not in scene_ids:
        scene_ids[key] = len(scenes)
        scenes.append({"cells": encode_cells(case["initial"], index_of)})
    changed = [[cell[0], cell[1], cell[2], index_of[final]]
               for cell, final in zip(case["initial"], case["final"])
               if final != cell[3]]
    compact.append({
        "at": case["at"], "seed": case["seed"], "scene": scene_ids[key],
        "result": case["result"],
        "local_words": case["local_words"],
        "writes": [[*w[:3], index_of[w[3]], w[4]] for w in case["writes"]],
        "queries": [[q["kind"], *q["position"], q["result"]] for q in case["queries"]],
        "changed": changed, "tail": case["tail"]})

fixture = {
    "binary_sha256": SHA,
    "function": "BonusChestFeature::place(BlockSource&, BlockPos const&, Random&)",
    "address": "0x1b526b0",
    "generator": "tools/re/gdb_bonus_chest_cases.py + tools/re/export_bonus_chest_reference.py",
    "method": "Each case constructs the real feature with its default "
              "constructor, seeds a real caller Random, and calls the native "
              "place(). Every write, every dependency answer and both Random "
              "streams are native observations. The caller's stream stays "
              "untouched because the feature seeds its own local Random.",
    "encoding": "block_names interns the names; each scene is base64(zlib("
                "repeated <hhhH> x,y,z,name_index)); a case stores only the "
                "cells that changed from its scene.",
    "oracle_sha256": hashlib.sha256(raw).hexdigest(),
    "block_names": names,
    "scenes": scenes,
    "cases": compact,
}
destination.write_text(json.dumps(fixture, separators=(",", ":")) + "\n")

# Self-check: the compact form must rebuild every observed final state.
for case, original in zip(compact, cases):
    rebuilt = {tuple(cell[:3]): cell[3] for cell in
               (struct.unpack_from('<hhhH', zlib.decompress(base64.b64decode(scenes[case["scene"]]["cells"])), at)
                for at in range(0, len(zlib.decompress(base64.b64decode(scenes[case["scene"]]["cells"]))), 8))}
    for x, y, z, index in case["changed"]:
        rebuilt[(x, y, z)] = index
    observed = {(cell[0], cell[1], cell[2]): index_of[name]
                for cell, name in zip(original["initial"], original["final"])}
    if rebuilt != observed:
        raise SystemExit("compact fixture does not reproduce the native final state")

print(json.dumps({"cases": len(compact), "scenes": len(scenes),
                  "bytes": destination.stat().st_size,
                  "writes": sum(len(c["writes"]) for c in compact),
                  "local_words": sum(len(c["local_words"]) for c in compact),
                  "queries": sum(len(c["queries"]) for c in compact),
                  "out": str(destination)}))
