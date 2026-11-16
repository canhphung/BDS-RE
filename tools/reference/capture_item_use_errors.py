#!/usr/bin/env python3
"""Capture ItemUse rejection responses from the original BDS 1.6.0.15.

Requires the original runtime assets, its compatible OpenSSL libraries and the
Python dependencies used by bedrock_client.py. Runs an isolated disposable world;
never opens or modifies a supplied world's files. See tools/reference/README.md.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import threading
import time

from analyze_capture import Reader, parse_start_game
from bedrock_client import BedrockClient, uvarint

ORIGINAL_SHA256 = "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad"
RESPONSE_IDS = {0x13, 0x15, 0x30, 0x31}
FACES = [(0, -1, 0), (0, 1, 0), (0, 0, -1), (0, 0, 1), (-1, 0, 0), (1, 0, 0)]


def relevant(packet, positions):
    if packet["id"] not in RESPONSE_IDS:
        return False
    if packet["id"] == 0x15:
        reader = Reader(bytes.fromhex(packet["body"]))
        return [reader.varint(), reader.uvarint(), reader.varint()] in positions
    return True


def vi(value):
    return uvarint((value << 1) ^ (value >> 31))


def item(legacy_id, count=0):
    return vi(legacy_id) + (vi(count) + bytes(4) if legacy_id else b"")


def request(action, face, slot, held):
    # ItemUse transaction, no inventory actions, target (2,64,1).
    return (b"\x02\x00" + uvarint(action) + vi(2) + uvarint(64) + vi(1)
            + vi(face) + vi(slot) + held
            + struct.pack("<ffffff", .5, 66.62, .5, .5, 1, .5))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--library-path", type=Path, required=True)
    parser.add_argument("--preload", type=Path)
    parser.add_argument("--output", type=Path, required=True,
                        help="New directory for the disposable runtime and captures")
    parser.add_argument("--port", type=int, default=19150)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    digest = hashlib.sha256((runtime / "bedrock_server").read_bytes()).hexdigest()
    if digest != ORIGINAL_SHA256:
        parser.error("runtime does not match the pinned original Linux binary")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    run = output / "runtime"
    run.mkdir()
    for name in ("bedrock_server", "libCrypto.so", "behavior_packs", "resource_packs",
                 "definitions", "structures"):
        (run / name).symlink_to(runtime / name, target_is_directory=(runtime / name).is_dir())
    properties = {
        "server-name": "ItemUseAudit", "level-name": "Audit", "level-seed": "123",
        "level-type": "FLAT", "server-port": str(args.port),
        "server-portv6": str(args.port + 1), "online-mode": "false",
        "allow-cheats": "true", "gamemode": "survival", "difficulty": "peaceful", "max-threads": "2",
        "view-distance": "8", "tick-distance": "4",
    }
    (run / "server.properties").write_text(
        "".join(f"{key}={value}\n" for key, value in properties.items()))
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = str(args.library_path.resolve()) + ":."
    if args.preload:
        env["LD_PRELOAD"] = str(args.preload.resolve())
    process = subprocess.Popen(["./bedrock_server"], cwd=run, env=env,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    lines, commands, outbound, cases = [], [], [], []
    client = None

    def collect():
        for line in process.stdout:
            lines.append(line.rstrip())

    collector = threading.Thread(target=collect, daemon=True)
    collector.start()

    def command(text):
        commands.append(text)
        process.stdin.write(text + "\n")
        process.stdin.flush()

    def send(packets):
        outbound.extend({"id": packet_id, "body": body.hex()} for packet_id, body in packets)
        client.send_packets(packets)

    try:
        deadline = time.monotonic() + 30
        while not any("IPv4 supported" in line for line in lines):
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("original server did not start: " + "\n".join(lines[-15:]))
            time.sleep(.05)
        client = BedrockClient("127.0.0.1", args.port, name="Audit")
        joined = client.join()
        game = parse_start_game(next(body for packet_id, body in joined if packet_id == 11))
        runtime_id = game["runtime_id"]
        send([(0x45, vi(5))])
        client.drain(2)
        send([(0x71, uvarint(runtime_id))])
        client.drain(.4)
        setup = ["gamerule domobspawning false",
                 "fill -4 63 -4 4 64 4 stone", "fill 1 65 0 3 66 2 air",
                 "setblock 2 64 1 grass", "tp Audit 0.5 65 0.5", "give Audit dirt 10"]
        for text in setup:
            command(text)
        client.drain(1)
        send([(0x13, uvarint(runtime_id)
               + struct.pack("<ffffff", .5, 66.62, .5, 0, 0, 0) + bytes([0, 1, 0])),
              (0x1f, uvarint(runtime_id) + item(3, 10) + bytes([0, 0, 0]))])
        client.drain(.4)
        for action in range(3):
            for mismatch in ("held_item", "selected_slot"):
                for face in range(6):
                    dx, dy, dz = FACES[face]
                    positions = [[2, 64, 1], [2, 65, 1],
                                 [2 + dx, 64 + dy, 1 + dz], [2 + dx, 65 + dy, 1 + dz]]
                    body = request(action, face, int(mismatch == "selected_slot"),
                                   item(0) if mismatch == "held_item" else item(3, 10))
                    start, console_start = len(client.log), len(lines)
                    send([(0x1e, body)])
                    # World generation may stall a tick; wait for the final
                    # hotbar correction rather than assigning late replies to
                    # the following request.
                    deadline = time.monotonic() + 5
                    while True:
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise RuntimeError(f"missing correction: {action}/{mismatch}/{face}")
                        if any(packet_id == 0x30 for packet_id, _ in
                               client.receive_packets(timeout=remaining)):
                            break
                    client.drain(.1)
                    packets = [{"id": p["id"], "body": p["body"]}
                               for p in client.log[start:] if relevant(p, positions)]
                    if [p["id"] for p in packets] != [0x15] * 4 + [0x31] * 4 + [0x30]:
                        raise RuntimeError(f"unexpected response for {action}/{mismatch}/{face}: {packets}")
                    cases.append({"name": f"action_{action}_{mismatch}_face_{face}",
                                  "request": {"id": 0x1e, "body": body.hex()},
                                  "block_update_positions": positions,
                                  "responses": packets, "console": lines[console_start:]})
        # A matching held item and slot must not trigger the rejection branch.
        start = len(client.log)
        body = request(1, 1, 0, item(3, 10))
        send([(0x1e, body)])
        client.drain(.5)
        packets = [{"id": p["id"], "body": p["body"]}
                   for p in client.log[start:] if relevant(p, [[2, 64, 1], [2, 65, 1], [2, 66, 1]])]
        if packets:
            raise RuntimeError(f"valid click-air produced unexpected correction: {packets}")
        cases.append({"name": "matching_click_air", "request": {"id": 0x1e, "body": body.hex()},
                      "block_update_positions": [[2, 64, 1], [2, 65, 1], [2, 66, 1]],
                      "responses": []})
        fixture = {
            "source_binary_sha256": digest,
            "scope": "ItemUse held-item/selected-slot rejection and correction; block-only inventory",
            "response_filter_ids": sorted(RESPONSE_IDS),
            "normalization": "Captured bodies are unmodified. Compare UpdateBlock runtime IDs by name/data in the captured StartGame palette and the tested server's advertised palette; all remaining response bytes and packet order must match. Only response_filter_ids retained; UpdateBlock additionally restricted to the listed per-case positions. Unrelated world tick packets excluded.",
            "runtime_palette": [{"runtime_id": index, "name": name, "data": data}
                                for index, (name, data) in enumerate(game["palette"])
                                if data == 0 and name in {"minecraft:air", "minecraft:stone",
                                                         "minecraft:grass", "minecraft:dirt",
                                                         "minecraft:bedrock"}],
            "properties": properties, "setup_commands": setup,
            "player": {"position": [.5, 66.62, .5], "selected_slot": 0,
                       "inventory": [[3, 10]] + [[0, 0]] * 35},
            "cases": cases,
        }
        (output / "item_use_errors.json").write_text(json.dumps(fixture, indent=2) + "\n")
        print(f"Captured {len(cases)} original ItemUse cases: {output / 'item_use_errors.json'}")
    finally:
        if client:
            client.raknet.close()
        if process.poll() is None:
            command("stop")
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        collector.join(timeout=2)
        (output / "raw_capture.json").write_text(json.dumps(
            {"source_binary_sha256": digest, "commands": commands, "outbound": outbound,
             "packets": client.log if client else [], "console": lines}, indent=1) + "\n")


if __name__ == "__main__":
    main()
