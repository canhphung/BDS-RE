#!/usr/bin/env python3
"""Distill original draw/sample traces into bounded regression inputs.

Usage: export_world_tick_reference.py OUT.json TRACE_DIRECTORY...
Preserves captured values; does not calculate expected LCG positions or RNG words.
"""
import collections
import hashlib
import json
from pathlib import Path
import sys


def main():
    destination = Path(sys.argv[1])
    startup, first_tick, samples, traces = None, None, [], []
    selected = collections.Counter()
    for directory in map(Path, sys.argv[2:]):
        raw = (directory / "draws.jsonl").read_bytes()
        metadata = json.loads((directory / "metadata.json").read_text())
        records = [json.loads(line) for line in raw.splitlines()]
        if records[-1]["event"] != "complete":
            raise ValueError("incomplete trace")
        if metadata["properties"]["level-seed"] != "123":
            raise ValueError("this corpus uses seed 123")
        trace_hash = hashlib.sha256(raw).hexdigest()
        if trace_hash != metadata["trace_sha256"]:
            raise ValueError("trace checksum mismatch")
        traces.append({"sha256": trace_hash, "completion": records[-1],
                       "generator_override": metadata["generator_override"],
                       "players": metadata["players"], "commands": metadata.get("commands", [])})
        if startup is None:
            startup = [r["value"] for r in records if r["event"] == "draw" and r["tick"] == 0]
            first_tick = [r["value"] for r in records if r["event"] == "draw" and r["tick"] == 1]
            tick = next(r for r in records if r["event"] == "tick" and r["tick"] == 1)
            if len(startup) != 4492 or len(first_tick) != 112 or tick["players"]:
                raise ValueError("unexpected startup/empty first-tick shape")
        chunk_start, weather, current = None, None, None
        for r in records:
            if r["event"] == "block_tick_begin":
                chunk_start, weather = r, None
            elif r["event"] == "weather_column":
                weather = r
            elif r["event"] == "block_samples_begin":
                current = {"tick": r["tick"], "chunk": r["chunk"],
                           "trace_sha256": trace_hash, "height": r["height"],
                           "sample_count": r["count"], "initial_lcg": r["lcg"], "samples": []}
                if chunk_start and weather:
                    current.update(weather_initial_lcg=chunk_start["lcg"],
                                   weather_column=weather["column"], weather_lcg=weather["lcg"])
            elif r["event"] == "block_sample" and current:
                current["samples"].append({"position": r["position"], "lcg": r["lcg"]})
            elif r["event"] == "block_samples_end" and current:
                if len(current["samples"]) != current["sample_count"]:
                    raise ValueError("truncated block sample group")
                current["final_lcg"] = r["lcg"]
                # Prefer groups that also include the precipitation-column step.
                if "weather_initial_lcg" in current and selected[current["height"]] < 16:
                    samples.append(current)
                    selected[current["height"]] += 1
                current = None
    if set(selected) != {16, 256} or selected[16] != 16 or selected[256] != 16:
        raise ValueError(f"need 16 cases each at heights 16 and 256, got {selected}")
    fixture = {"binary_sha256": metadata["binary_sha256"],
               "generator": "tools/re/capture_level_random.py + gdb_level_random_sequence.py",
               "scope": "Startup/first empty tick RNG; LevelChunk::tickBlocks coordinate selection only. Full block ticks, spawning, AI and later Server RNG order remain unverified.",
               "seed": 123, "traces": traces, "startup_words": startup,
               "first_empty_tick_words": first_tick, "block_samples": samples}
    destination.write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"Exported {len(startup)} startup words, {len(first_tick)} tick words, {len(samples)} sample groups")


if __name__ == "__main__":
    main()
