#!/usr/bin/env python3
"""Join original GrassBlock oracle runs without regenerating expected results.

Usage: export_grass_tick_reference.py OUT.json DAY_RUN NIGHT_RUN
"""
import hashlib
import json
from pathlib import Path
import sys


def main():
    sources, cases = [], []
    binary = None
    for directory in map(Path, sys.argv[2:]):
        raw = (directory / "grass_ticks.json").read_bytes()
        original = json.loads(raw)
        metadata = json.loads((directory / "metadata.json").read_text())
        if len(original["cases"]) != 80 or original["min_brightness"] != 4 or original["max_brightness"] != 15:
            raise ValueError("unexpected original grass oracle shape/constants")
        if binary is not None and original["binary_sha256"] != binary:
            raise ValueError("mixed original binaries")
        binary = original["binary_sha256"]
        source = len(sources)
        sources.append({"oracle_sha256": hashlib.sha256(raw).hexdigest(),
                        "source_trace_sha256": metadata["trace_sha256"],
                        "generator_override": metadata["generator_override"],
                        "scene_setup_commands": metadata["commands"]})
        for case in original["cases"]:
            cases.append({"source": source, **case})
    if len(cases) != 160:
        raise ValueError("expected two complete 80-case runs")
    if sum("time set 18000" in source["scene_setup_commands"] for source in sources) != 1:
        raise ValueError("expected one daylight run and one time-set-18000 run")
    fixture = {"binary_sha256": binary,
               "generator": "tools/re/gdb_grass_ticks.py + export_grass_tick_reference.py",
               "scope": "Original server-side GrassBlock::tick dependency calls, mutation requests and raw RNG words. Live day/night light/material values recorded without replacement. Host lighting/chunk scheduler not tested.",
               "min_brightness": 4, "max_brightness": 15, "sources": sources, "cases": cases}
    Path(sys.argv[1]).write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"Exported {len(cases)} original GrassBlock cases")


if __name__ == "__main__":
    main()
