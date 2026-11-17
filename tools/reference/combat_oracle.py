#!/usr/bin/env python3
"""Packet oracle for eating, effects, absorption, armor and player combat.

    python3 tools/reference/combat_oracle.py HOST PORT OUT.json CONSOLE_FIFO

Run the server with gamemode=survival, difficulty=normal and allow-cheats=true,
its standard input connected to the FIFO given as the fourth argument (the
setup types give/effect console commands). Two clients join and stand two blocks apart on the flat surface:

  setup      - A: golden_apple x2, diamond_sword, spider_eye x4, bread x4;
               B: iron_helmet, iron_sword (slots 0.. in that order)
  a_fall     - A reports ActorFall 10 (7 damage, health 13)
  a_eat      - A starts using the golden apple (ItemUse click air), releases
               with "consume" after 33 ticks; regeneration and absorption run
  b_equip    - B uses the iron helmet (ItemUse click air: ArmorItem::use)
  a_hit      - A selects the sword and attacks B (ItemUseOnActor attack)
  b_sprint   - B selects its sword, starts sprinting and attacks A
  b_poison   - console: effect Bravo poison 3 0
  a_drop     - A drops one spider eye (world interaction, Q)
Everything both clients receive per step is recorded except chunks, time,
movement deltas and other players' MovePlayer packets.
"""
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_oracle import move_body, spawn  # noqa: E402
from bedrock_client import BedrockClient, uvarint  # noqa: E402
from hunger_health_oracle import fall, vi  # noqa: E402

NOISE = {0x6f, 0x0a, 0x3a, 0x79, 0x13}
GOLDEN_APPLE, DIAMOND_SWORD, SPIDER_EYE, BREAD = 322, 276, 375, 297
IRON_HELMET, IRON_SWORD = 306, 267


def item(identifier, count=1, damage=0):
    if identifier == 0:
        return vi(0)
    return vi(identifier) + vi((damage << 8) | count) + b"\0\0" + vi(0) + vi(0)


def vec3(x, y, z):
    return struct.pack("<3f", x, y, z)


def console(fifo, *lines):
    with open(fifo, "w") as f:
        for line in lines:
            f.write(line + "\n")


def equip(runtime, slot, held):
    # MobEquipmentPacket: runtime id, item, slot, selected slot, window id
    return 0x1F, uvarint(runtime) + held + bytes([slot, slot, 0])


def changes(*actions):
    # The inventory actions a client predicts with the transaction:
    # (window, slot, before, after) from container sources.
    out = uvarint(len(actions))
    for window, slot, before, after in actions:
        out += uvarint(0) + vi(window) + uvarint(slot) + before + after
    return out


def use_air(slot, held, at, *actions):
    # InventoryTransaction ItemUse (2), action click air (1)
    return 0x1E, (uvarint(2) + changes(*actions) + uvarint(1) + vi(0) + uvarint(0) + vi(0) + vi(-1) + vi(slot)
                  + held + vec3(*at) + vec3(0, 0, 0))


def release(slot, held, at, *actions):
    # InventoryTransaction ItemRelease (4), action consume (1)
    return 0x1E, uvarint(4) + changes(*actions) + uvarint(1) + vi(slot) + held + vec3(*at)


def attack(target, slot, held, at, *actions):
    # InventoryTransaction ItemUseOnActor (3), action attack (1)
    return 0x1E, (uvarint(3) + changes(*actions) + uvarint(target) + uvarint(1) + vi(slot) + held
                  + vec3(*at) + vec3(0, 0, 0))


def drop(slot, before, after, dropped):
    # Normal InventoryTransaction: inventory slot change plus a world
    # interaction (source 2, flags 0 = drop) receiving the item.
    actions = (uvarint(0) + vi(0) + uvarint(slot) + before + after
               + uvarint(2) + uvarint(0) + uvarint(0) + item(0) + dropped)
    return 0x1E, uvarint(0) + uvarint(2) + actions


def action(runtime, kind):
    return 0x24, uvarint(runtime) + vi(kind) + vi(0) + uvarint(0) + vi(0) + vi(0)


def collect(clients, seconds):
    out = {name: [] for name in clients}
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for name, client in clients.items():
            for pid, body in client.drain(0.05):
                if pid not in NOISE:
                    out[name].append({"id": pid, "body": body.hex(), "t": round(time.monotonic(), 3)})
    return out


def main():
    host, port, output, fifo = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3]), sys.argv[4]
    a, b = BedrockClient(host, port, "Alpha"), BedrockClient(host, port, "Bravo")
    clients = {"a": a, "b": b}
    steps = []
    try:
        ga = spawn(a)
        gb = spawn(b)
        ra, rb = ga["runtime_id"], gb["runtime_id"]
        pa, pb = (0.5, 5.62, 0.5), (2.5, 5.62, 0.5)
        a.send_packets([(0x13, move_body(ra, *pa, ground=True))])
        b.send_packets([(0x13, move_body(rb, *pb, yaw=90.0, head=90.0, ground=True))])
        collect(clients, 2)

        console(fifo, "give Alpha golden_apple 2", "give Alpha diamond_sword", "give Alpha spider_eye 4",
                "give Alpha bread 4", "give Bravo iron_helmet", "give Bravo iron_sword")
        steps.append({"step": "setup", "packets": collect(clients, 3)})

        a.send_packets([(0x13, move_body(ra, pa[0], pa[1] + 10, pa[2], ground=False))])
        collect(clients, 0.3)
        a.send_packets([(0x13, move_body(ra, *pa, ground=True)), (0x25, fall(ra, 10.0))])
        steps.append({"step": "a_fall", "packets": collect(clients, 1.5)})

        apple = item(GOLDEN_APPLE, 2)
        a.send_packets([use_air(0, apple, pa)])
        first = collect(clients, 1.65)
        a.send_packets([release(0, apple, pa, (0, 0, apple, item(GOLDEN_APPLE, 1)))])
        rest = collect(clients, 6)
        steps.append({"step": "a_eat", "packets": {k: first[k] + rest[k] for k in first}})

        helmet = item(IRON_HELMET)
        b.send_packets([use_air(0, helmet, pb, (0, 0, helmet, item(0)), (120, 0, item(0), helmet))])
        steps.append({"step": "b_equip", "packets": collect(clients, 1.5)})

        sword = item(DIAMOND_SWORD)
        a.send_packets([equip(ra, 1, sword)])
        collect(clients, 0.5)
        a.send_packets([attack(rb, 1, sword, pa, (0, 1, sword, item(DIAMOND_SWORD, 1, 1)))])
        steps.append({"step": "a_hit", "packets": collect(clients, 1.5)})

        iron = item(IRON_SWORD)
        b.send_packets([equip(rb, 1, iron), action(rb, 9)])
        collect(clients, 0.5)
        b.send_packets([attack(ra, 1, iron, pb, (0, 1, iron, item(IRON_SWORD, 1, 1)))])
        steps.append({"step": "b_sprint", "packets": collect(clients, 1.5)})

        console(fifo, "effect Bravo poison 3 0")
        steps.append({"step": "b_poison", "packets": collect(clients, 4)})

        a.send_packets([drop(2, item(SPIDER_EYE, 4), item(SPIDER_EYE, 3), item(SPIDER_EYE, 1))])
        steps.append({"step": "a_drop", "packets": collect(clients, 1.5)})
    finally:
        a.raknet.close()
        b.raknet.close()
    output.write_text(json.dumps({"runtime_a": ra, "runtime_b": rb, "steps": steps}, indent=1) + "\n")
    for s in steps:
        print(s["step"], {k: [hex(p["id"]) for p in v] for k, v in s["packets"].items()})


if __name__ == "__main__":
    main()
