#!/usr/bin/env python3
"""Packet oracle for attributes, hunger, health, fall damage, death and respawn.

    python3 tools/reference/hunger_health_oracle.py HOST PORT OUT.json

One survival client (run the server with difficulty=normal) stands on the
surface and:
  fall      - is moved 20 blocks up, then lands and reports ActorFall 19.8
  regen     - waits 10 s (food 20 >= 18: one health per 80 ticks)
  jump      - sends PlayerAction jump and sprint-jump
  drain     - walks 200 blocks back and forth on the ground for 100 ticks
              (0.01 exhaustion per block), draining saturation then food
  starve    - waits 12 s
  die       - falls 40 blocks (ActorFall 40)
  respawn   - sends PlayerAction respawn (7) and waits
Everything the client receives per step is recorded (MoveActorDelta, SetTime,
chunks and other entities' packets are dropped).
"""
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_oracle import move_body, spawn  # noqa: E402
from bedrock_client import BedrockClient, uvarint  # noqa: E402


def vi(value):
    return uvarint((value << 1) ^ (value >> 31))


def fall(runtime, distance, void=False):
    # ActorFallPacket: runtime id, fall distance, in void
    return uvarint(runtime) + struct.pack("<f", distance) + bytes([int(void)])


def first_varint(body):
    value = shift = at = 0
    while True:
        byte = body[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value


NOISE = {0x6f, 0x0a, 0x3a, 0x79, 0x0d, 0x20, 0x18, 0x0e, 0x28}


def collect(client, seconds, runtime):
    out = []
    for pid, body in client.drain(seconds):
        if pid in NOISE:
            continue
        if pid in (0x27, 0x1d, 0x1b, 0x13) and first_varint(body) != runtime:
            continue
        out.append({"id": pid, "body": body.hex(), "t": round(time.monotonic(), 3)})
    return out


def main():
    host, port, output = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3])
    c = BedrockClient(host, port, "Hunger")
    steps = []
    try:
        game = spawn(c)
        rid = game["runtime_id"]
        x, y, z = 0.5, 5.62, 0.5
        c.send_packets([(0x13, move_body(rid, x, y, z, ground=True))])
        steps.append({"step": "join", "packets": collect(c, 3, rid)})
        c.send_packets([(0x13, move_body(rid, x, y + 20, z, ground=False))])
        c.drain(0.5)
        c.send_packets([(0x13, move_body(rid, x, y, z, ground=True)), (0x25, fall(rid, 19.8))])
        steps.append({"step": "fall", "packets": collect(c, 2, rid)})
        steps.append({"step": "regen", "packets": collect(c, 10, rid)})
        c.send_packets([(0x24, uvarint(rid) + vi(8) + vi(0) + uvarint(0) + vi(0) + vi(0))])
        steps.append({"step": "jump", "packets": collect(c, 1, rid)})
        packets = []
        for i in range(100):
            c.send_packets([(0x13, move_body(rid, x + (200 if i % 2 == 0 else 0), y, z, ground=True))])
            packets += collect(c, 0.05, rid)
        packets += collect(c, 1, rid)
        steps.append({"step": "drain", "packets": packets})
        c.send_packets([(0x13, move_body(rid, x, y, z, ground=True))])
        steps.append({"step": "starve", "packets": collect(c, 12, rid)})
        c.send_packets([(0x13, move_body(rid, x, y + 40, z, ground=False))])
        c.drain(0.5)
        c.send_packets([(0x13, move_body(rid, x, y, z, ground=True)), (0x25, fall(rid, 40.0))])
        steps.append({"step": "die", "packets": collect(c, 3, rid)})
        c.send_packets([(0x24, uvarint(rid) + vi(7) + vi(0) + uvarint(0) + vi(0) + vi(0))])
        steps.append({"step": "respawn", "packets": collect(c, 4, rid)})
    finally:
        c.raknet.close()
    output.write_text(json.dumps({"runtime_id": rid, "steps": steps}, indent=1) + "\n")
    for s in steps:
        print(s["step"], [hex(p["id"]) for p in s["packets"]])


if __name__ == "__main__":
    main()
