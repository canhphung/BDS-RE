#!/usr/bin/env python3
"""Distill player/chunk/item order from an original --tick-order --drop-items trace.

Usage: export_chunk_order_reference.py TRACE_DIRECTORY OUT.json
Retains actual list order, chunk owners and item age/position; no RNG is replayed
or skipped to manufacture an order.
"""
import hashlib
import json
from pathlib import Path
import sys


def main():
    directory = Path(sys.argv[1])
    raw = (directory / "draws.jsonl").read_bytes()
    records = [json.loads(line) for line in raw.splitlines()]
    metadata = json.loads((directory / "metadata.json").read_text())
    if records[-1]["event"] != "complete" or hashlib.sha256(raw).hexdigest() != metadata["trace_sha256"]:
        raise ValueError("incomplete or modified trace")
    frames = {}
    for record in records:
        frames.setdefault(record.get("tick"), []).append(record)
    eligible = [tick for tick, events in frames.items() if tick is not None
                and sum(e["event"] == "item_tick" for e in events) == 2]
    if len(eligible) < 4:
        raise ValueError("need multiple complete ticks with two items")
    chosen = list(eligible[:3])
    initial_order = [e["actor"] for e in frames[chosen[0]] if e["event"] == "player_world"]
    opposite = next((tick for tick in eligible
                     if [e["actor"] for e in frames[tick] if e["event"] == "player_world"] != initial_order), None)
    if opposite is None:
        raise ValueError("need a trace containing both player orders")
    chosen += [opposite, eligible[-1]]
    cases = []
    for tick in chosen:
        events = frames[tick]
        players = [e for e in events if e["event"] == "player_world"]
        chunks = [e for e in events if e["event"] == "chunk_tick"]
        if len(players) != 2 or len(chunks) != len({tuple(e["chunk"]) for e in chunks}):
            raise ValueError("unexpected player count or repeated original chunk tick")
        expected = []
        for e in events:
            kind = e["event"]
            if kind in ("player_world", "player_tick", "nearby", "item_tick"):
                expected.append({"event": kind, "actor": e["actor"]})
            elif kind == "chunk_tick":
                expected.append({"event": kind, "chunk": e["chunk"]})
        cases.append({"tick": tick,
                      "players": [{"actor": p["actor"], "position": p["position"]} for p in players],
                      "offsets": players[0]["offsets"],
                      "chunks": [{"position": c["chunk"], "items": c["items"]} for c in chunks],
                      "items": [{key: e[key] for key in ("actor", "owner_chunk", "position", "age")}
                                for e in events if e["event"] == "item_tick"],
                      "expected": expected})
    out = {"binary_sha256": metadata["binary_sha256"], "source_trace_sha256": metadata["trace_sha256"],
           "generator": "tools/re/gdb_level_random_sequence.py --tick-order + export_chunk_order_reference.py",
           "scope": "Per-player chunk/ordinary-item/player/nearby callback order with captured player/offset lists and chunk availability. Does not verify how later RNG produces those lists.",
           "setup_commands": metadata["commands"], "drop_requests": metadata["item_drop_requests"],
           "cases": cases}
    Path(sys.argv[2]).write_text(json.dumps(out, indent=1) + "\n")
    print("Exported", len(cases), "original chunk/item order frames")


if __name__ == "__main__":
    main()
