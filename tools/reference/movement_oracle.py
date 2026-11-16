#!/usr/bin/env python3
"""Packet oracle for MovePlayer handling; runs unchanged against any server.

    python3 tools/reference/movement_oracle.py HOST PORT OUT.json

Two reference clients join. Client A sends MovePlayer packets that a
validating server would reject (a 100-block jump, Y above 512, a teleport
with riding id and teleport fields). The script records what client B
receives for A's runtime id and whether A is sent any correction. Running it
against the original BDS and against BDS-RE gives comparable JSON.
"""
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_capture import parse_start_game  # noqa: E402
from bedrock_client import BedrockClient, ProtocolFailure, uvarint  # noqa: E402


def move_body(runtime, x, y, z, pitch=0.0, yaw=0.0, head=0.0, mode=0, ground=True, riding=0, extra=(0, 0)):
    body = uvarint(runtime) + struct.pack("<6f", x, y, z, pitch, yaw, head) + bytes([mode, int(ground)]) + uvarint(riding)
    if mode == 2:
        body += struct.pack("<ii", *extra)
    return body


def runtime_of(body):
    value = shift = at = 0
    while True:
        byte = body[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value


def spawn(client):
    client.join()
    game = parse_start_game(bytes.fromhex(next(p for p in client.log if p["id"] == 0x0B)["body"]))
    client.send_packets([(0x45, uvarint(4 << 1))])
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            if any(pid == 0x02 and body == b"\0\0\0\3" for pid, body in client.receive_packets(timeout=1)):
                break
        except ProtocolFailure as error:
            if "timed out" not in str(error):
                raise
    client.send_packets([(0x71, uvarint(game["runtime_id"]))])  # SetLocalPlayerAsInitialized
    return game


def moves_for(client, runtime, seconds):
    seen = []
    for pid, body in client.drain(seconds):
        if pid == 0x13 and runtime_of(body) == runtime:
            seen.append(body.hex())
    return seen


def main():
    host, port, output = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3])
    a, b = BedrockClient(host, port, "MoverA"), BedrockClient(host, port, "WatcherB")
    try:
        game_a = spawn(a)
        spawn(b)
        a.drain(1.5)
        b.drain(1.5)
        x, y, z = game_a["position"]
        rid = game_a["runtime_id"]
        # All targets stay within B's view (radius 4 chunks); relays are only
        # sent to players that see the entity (Dimension::sendPacketForEntity).
        sent = [
            move_body(rid, x + 0.5, y, z),                       # ordinary step
            move_body(rid, x + 40.5, y, z, yaw=45.0),            # 40-block jump in one packet
            move_body(rid, x + 40.5, 1000.0, z, pitch=95.0),     # Y beyond the clamp, pitch beyond 90
            move_body(rid, x + 1.0, y, z, mode=2, riding=77, extra=(3, 9)),  # teleport mode with extra fields
        ]
        observed = {"sent": [body.hex() for body in sent], "relayed_to_b": [], "corrections_to_a": []}
        for body in sent:
            a.send_packets([(0x13, body)])
            observed["relayed_to_b"].extend(moves_for(b, rid, 2.0))
            observed["corrections_to_a"].extend(moves_for(a, rid, 0.3))
        observed["relayed_unchanged"] = observed["relayed_to_b"] == observed["sent"]
    finally:
        a.raknet.close()
        b.raknet.close()
    output.write_text(json.dumps(observed, indent=1) + "\n")
    print(json.dumps({k: (len(v) if isinstance(v, list) else v) for k, v in observed.items()}))


if __name__ == "__main__":
    main()
