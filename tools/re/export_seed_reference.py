#!/usr/bin/env python3
"""Copy a validated LevelSettings::parseSeedString oracle into a fixture.

Usage: export_seed_reference.py ORACLE_JSON OUT_JSON

The oracle (tools/re/gdb_seed_parse.py) calls the original function in a
paused original server. This exporter validates the recorded provenance and
copies the observations; it never computes an expected value.
"""
import hashlib
import json
import sys
from pathlib import Path

SHA = "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad"
MINIMUM_CASES = 40

source, destination = Path(sys.argv[1]), Path(sys.argv[2])
raw = source.read_bytes()
recorded = json.loads(raw)
if recorded["binary_sha256"] != SHA:
    raise SystemExit("unexpected original binary")
if recorded["address"] != "0x1c69910":
    raise SystemExit("unexpected function address")
if len(recorded["cases"]) < MINIMUM_CASES:
    raise SystemExit("incomplete oracle")
for case in recorded["cases"]:
    if set(case) != {"input", "default", "signed", "unsigned", "note"}:
        raise SystemExit("unexpected case shape")
    if case["signed"] & 0xFFFFFFFF != case["unsigned"]:
        raise SystemExit("signed/unsigned values disagree")

fixture = {
    "binary_sha256": SHA,
    "function": recorded["function"],
    "address": recorded["address"],
    "generator": "tools/re/gdb_seed_parse.py + tools/re/export_seed_reference.py",
    "method": recorded["method"],
    "helper": recorded["helper"],
    "oracle_sha256": hashlib.sha256(raw).hexdigest(),
    "cases": recorded["cases"],
}
destination.write_text(json.dumps(fixture, indent=1) + "\n")
print(json.dumps({"cases": len(fixture["cases"]), "out": str(destination)}))
