"""Client scenarios for capture_level_random.py --scenario (first player only).

mob_combat: the player stands at (0.5, 5.62, 0.5) on the flat surface; the
setup commands summon a chicken at (2, 4, 0) and a cow at (-2, 4, 0). The
player punches each (empty hand, InventoryTransaction ItemUseOnActor attack)
once a second until its RemoveActor arrives or ten hits were sent.
"""
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "reference"))
from analyze_capture import Reader  # noqa: E402
from bedrock_client import uvarint  # noqa: E402
from combat_oracle import attack, item  # noqa: E402
from movement_oracle import move_body  # noqa: E402

EYE = (0.5, 5.62, 0.5)


def find_actor(client, actor_type, near, timeout=20.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        client.drain(0.1)
        for packet in client.log:
            if packet["id"] != 0x0D:
                continue
            r = Reader(bytes.fromhex(packet["body"]))
            r.varint()
            runtime = r.uvarint()
            if r.uvarint() != actor_type:
                continue
            x, y, z = r.f32(), r.f32(), r.f32()
            if abs(x - near[0]) < 1.5 and abs(z - near[2]) < 1.5:
                return runtime
    return None


def removed(client, runtime, unique_hint=None):
    for packet in client.log:
        if packet["id"] == 0x0E:
            return True if unique_hint is None else None
    return False


def mob_combat(client, game, log):
    runtime = game["runtime_id"]
    client.send_packets([(0x13, move_body(runtime, *EYE, ground=True))])
    for actor_type, near in ((0x130A, (2.0, 4.0, 0.0)), (0x130B, (-2.0, 4.0, 0.0))):
        target = find_actor(client, actor_type, near)
        log.append({"event": "target", "type": actor_type, "runtime": target})
        if target is None:
            continue
        for hit in range(10):
            before = len(client.log)
            client.send_packets([attack(target, 0, item(0), EYE)])
            log.append({"event": "attack", "target": target, "hit": hit, "log_index": before})
            deadline = time.monotonic() + 1.0
            gone = False
            while time.monotonic() < deadline:
                client.drain(0.1)
                for packet in client.log[before:]:
                    if packet["id"] == 0x0E:
                        gone = True
            if gone:
                break
        client.drain(2.0)


SCENARIOS = {"mob_combat": mob_combat}
