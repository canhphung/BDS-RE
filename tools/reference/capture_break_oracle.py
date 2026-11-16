#!/usr/bin/env python3
"""Capture block-break (ItemUse action 2) responses from the original BDS 1.6.0.15.

    python3 tools/reference/capture_break_oracle.py --runtime input/linux/bds \
        --library-path <openssl-1.0>/lib --preload <ipv6_shim.so> --output input/reference/break-new

Same isolation as capture_item_use_errors.py: a fresh FLAT world in a new
directory, set up through console commands. A survival player at
(0.5, 66.62, 0.5) with an empty hand sends break requests without any
StartBreak/PlayerAction first (each case is preceded by a separate StartBreak/
AbortBreak pair on the same block, recording the LevelEvent 3600 crack rate):

- grass at (2,64,1)       ordinary block, distance 3.06
- dirt at (3,64,1)        ordinary block
- bedrock at (4,64,1)     unbreakable in survival
- stone at (12,64,1)      Actor::distanceTo(Vec3(BlockPos)) = 11.80 <= 12.5
- stone at (13,64,1)      distance 12.78 > 12.5

Every packet the server sends in the following window is recorded.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import threading
import time
from pathlib import Path

from analyze_capture import parse_start_game
from bedrock_client import BedrockClient, uvarint

ORIGINAL_SHA256 = "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad"
EYE = (0.5, 66.62, 0.5)


def vi(value):
    return uvarint((value << 1) ^ (value >> 31))


def break_request(x, y, z, face=1):
    # InventoryTransaction type 2 (ItemUse), no inventory actions, action 2 (break),
    # empty held item in slot 0, from-position = player eye, click position 0.
    return (b"\x02\x00" + uvarint(2) + vi(x) + uvarint(y) + vi(z) + vi(face) + vi(0) + vi(0)
            + struct.pack("<ffffff", *EYE, 0, 0, 0))


def action(rid, kind, x, y, z, face=1):
    return uvarint(rid) + vi(kind) + vi(x) + uvarint(y) + vi(z) + vi(face)


CASES = [("grass", (2, 64, 1), "setblock 2 64 1 grass"),
         ("dirt", (3, 64, 1), "setblock 3 64 1 dirt"),
         ("bedrock", (4, 64, 1), "setblock 4 64 1 bedrock"),
         ("stone_in_reach", (12, 64, 1), "setblock 12 64 1 stone"),
         ("stone_out_of_reach", (13, 64, 1), "setblock 13 64 1 stone")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--library-path", type=Path, required=True)
    parser.add_argument("--preload", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=19170)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    if hashlib.sha256((runtime / "bedrock_server").read_bytes()).hexdigest() != ORIGINAL_SHA256:
        parser.error("runtime does not match the pinned original Linux binary")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    run = output / "runtime"
    run.mkdir()
    for name in ("bedrock_server", "libCrypto.so", "behavior_packs", "resource_packs", "definitions", "structures"):
        (run / name).symlink_to(runtime / name, target_is_directory=(runtime / name).is_dir())
    properties = {"server-name": "BreakAudit", "level-name": "Break", "level-seed": "123", "level-type": "FLAT",
                  "server-port": str(args.port), "server-portv6": str(args.port + 1), "online-mode": "false",
                  "allow-cheats": "true", "gamemode": "survival", "difficulty": "peaceful", "view-distance": "8"}
    (run / "server.properties").write_text("".join(f"{k}={v}\n" for k, v in properties.items()))
    env = dict(os.environ, LD_LIBRARY_PATH=str(args.library_path.resolve()) + ":.")
    if args.preload:
        env["LD_PRELOAD"] = str(args.preload.resolve())
    process = subprocess.Popen(["./bedrock_server"], cwd=run, env=env, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    lines, results = [], []
    threading.Thread(target=lambda: [lines.append(l.rstrip()) for l in process.stdout], daemon=True).start()

    def command(text):
        process.stdin.write(text + "\n")
        process.stdin.flush()

    client = None
    try:
        deadline = time.monotonic() + 30
        while not any("IPv4 supported" in line for line in lines):
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("original server did not start: " + "\n".join(lines[-15:]))
            time.sleep(.05)
        client = BedrockClient("127.0.0.1", args.port, name="Breaker")
        joined = client.join()
        game = parse_start_game(next(body for packet_id, body in joined if packet_id == 11))
        rid = game["runtime_id"]
        client.send_packets([(0x45, vi(5))])
        client.drain(2)
        client.send_packets([(0x71, uvarint(rid))])
        client.drain(.4)
        for text in ["gamerule domobspawning false", "gamerule dotiledrops true",
                     "fill -2 63 -2 14 64 3 stone", "fill -2 65 -2 14 70 3 air", "tp Breaker 0.5 65 0.5"]:
            command(text)
        for _, _, setup in CASES:
            command(setup)
        client.drain(1.5)
        client.send_packets([(0x13, uvarint(rid) + struct.pack("<ffffff", *EYE, 0, 0, 0) + bytes([0, 1, 0]))])
        client.drain(.5)
        for name, (x, y, z), _ in CASES:
            start = len(client.log)
            client.send_packets([(0x24, action(rid, 0, x, y, z))])
            client.drain(.6)
            client.send_packets([(0x24, action(rid, 1, x, y, z))])
            client.drain(.6)
            crack = [{"id": p["id"], "body": p["body"]} for p in client.log[start:] if p["id"] == 0x19]
            start = len(client.log)
            client.send_packets([(0x1e, break_request(x, y, z))])
            client.drain(1.5)
            results.append({"case": name, "block": [x, y, z], "distance_to_corner":
                            round(((EYE[0] - x) ** 2 + (EYE[1] - y) ** 2 + (EYE[2] - z) ** 2) ** .5, 3),
                            "start_abort_level_events": crack, "responses": [{"id": p["id"], "body": p["body"]} for p in client.log[start:]]})
    finally:
        if client:
            client.raknet.close()
        try:
            command("stop")
            process.wait(timeout=20)
        except Exception:
            process.kill()
    (output / "break_oracle.json").write_text(json.dumps({"binary_sha256": ORIGINAL_SHA256, "eye": EYE,
                                                          "cases": results}, indent=1) + "\n")
    for result in results:
        print(result["case"], result["distance_to_corner"], [hex(p["id"]) for p in result["responses"]])


if __name__ == "__main__":
    main()
