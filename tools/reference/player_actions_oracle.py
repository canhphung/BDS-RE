#!/usr/bin/env python3
"""Packet oracle for movement-related PlayerActions and entity visibility.

    python3 tools/reference/player_actions_oracle.py HOST PORT OUT.json

Two reference clients join next to each other. Client A sends PlayerAction
start/stop sneak (11/12), start/stop sprint (9/10) and jump (8), then walks
out of B's view (beyond the client ticking radius) and back. For every step
the script records what A and what B receive (packet id and body), skipping
the world's own MoveActorDelta/SetTime noise. Runs unchanged against the
original BDS and BDS-RE.
"""
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_oracle import move_body, spawn  # noqa: E402
from bedrock_client import uvarint  # noqa: E402


def vi(value):
    return uvarint((value << 1) ^ (value >> 31))


def action(runtime, kind):
    return uvarint(runtime) + vi(kind) + vi(0) + uvarint(0) + vi(0) + vi(0)


NOISE = {0x6f, 0x0a, 0x3a, 0x79, 0x28}  # MoveActorDelta, SetTime, chunks, chunk radius, motion


def _uvarint(data, at=0):
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value, at


def player_metadata(body):
    """The metadata list of an AddPlayer body (bytes after the held item)."""
    at = 16
    for _ in range(2):  # name, third-party name
        n, at = _uvarint(body, at)
        at += n
    _, at = _uvarint(body, at)  # platform
    _, at = _uvarint(body, at)  # unique id
    _, at = _uvarint(body, at)  # runtime id
    n, at = _uvarint(body, at)  # platform chat id
    at += n + 36  # position, motion, rotation
    held, at = _uvarint(body, at)
    if held:
        _, at = _uvarint(body, at)
        n = body[at] | body[at + 1] << 8
        at += 2 + n
        for _ in range(2):
            _, at = _uvarint(body, at)
    start = at
    count, at = _uvarint(body, at)
    for _ in range(count):
        _, at = _uvarint(body, at)
        kind, at = _uvarint(body, at)
        if kind == 0:
            at += 1
        elif kind == 1:
            at += 2
        elif kind in (2, 7):
            _, at = _uvarint(body, at)
        elif kind == 3:
            at += 4
        elif kind == 4:
            n, at = _uvarint(body, at)
            at += n
        elif kind == 6:
            for _ in range(3):
                _, at = _uvarint(body, at)
        elif kind == 8:
            at += 12
    return body[start:at]


def about(packets, runtime, name):
    """Packets concerning player A, normalised for comparison between servers:
    actor data, movement attribute, relayed PlayerAction/MovePlayer, armor and
    held item exactly; RemoveActor and AddPlayer by meaning (unique ids and
    profile fields differ between servers; the AddPlayer metadata is compared
    with the name tag replaced)."""
    out = []
    for p in packets:
        body = bytes.fromhex(p["body"])
        first, _ = _uvarint(body)
        kind = p["id"]
        if kind in (0x27, 0x24, 0x20, 0x1f) and first == runtime:
            out.append([kind, body.hex()])
        elif kind == 0x13 and first == runtime:
            out.append([kind, "relay"])  # bodies depend on the spawn point; see movement_reference
        elif kind == 0x1d and first == runtime and b"minecraft:movement" in body:
            out.append([kind, body.hex()])
        elif kind == 0x0e and ((first >> 1) ^ -(first & 1)) & 0xFFFFFFFF == runtime:
            out.append([kind, "remove"])
        elif kind == 0x0c and name.encode() in body:
            out.append([kind, player_metadata(body).replace(name.encode(), b"NAME").hex()])
    return out


def collect(client, seconds):
    return [{"id": pid, "body": body.hex()} for pid, body in client.drain(seconds) if pid not in NOISE]


def main():
    host, port, output = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3])
    from bedrock_client import BedrockClient  # noqa: E402
    a, b = BedrockClient(host, port, "ActorA"), BedrockClient(host, port, "WatcherB")
    steps = []
    try:
        game_a = spawn(a)
        spawn(b)
        a.drain(2)
        b.drain(2)
        rid = game_a["runtime_id"]
        x, y, z = game_a["position"]
        sequence = [("sneak_start", [(0x24, action(rid, 11))]), ("sneak_stop", [(0x24, action(rid, 12))]),
                    ("sprint_start", [(0x24, action(rid, 9))]), ("sprint_stop", [(0x24, action(rid, 10))]),
                    ("jump", [(0x24, action(rid, 8))]),
                    ("leave_view", [(0x13, move_body(rid, x + 160.5, y + 40, z))]),
                    ("return", [(0x13, move_body(rid, x + 1.5, y, z))])]
        for name, packets in sequence:
            a.send_packets(packets)
            steps.append({"step": name, "to_actor": collect(a, 1.5), "to_watcher": collect(b, 1.5)})
    finally:
        a.raknet.close()
        b.raknet.close()
    for s in steps:
        s["about_actor"] = {"to_actor": about(s["to_actor"], rid, "ActorA"),
                            "to_watcher": about(s["to_watcher"], rid, "ActorA")}
    output.write_text(json.dumps({"runtime_id": rid, "steps": steps}, indent=1) + "\n")
    for s in steps:
        print(s["step"], [hex(p["id"]) for p in s["to_actor"]], [hex(p["id"]) for p in s["to_watcher"]])


if __name__ == "__main__":
    main()
