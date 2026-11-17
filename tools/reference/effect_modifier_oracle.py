#!/usr/bin/env python3
"""Packet oracle for the attribute-modifier effects.

    python3 tools/reference/effect_modifier_oracle.py HOST PORT OUT.json CONSOLE_FIFO

Server as for combat_oracle.py (survival, difficulty normal, cheats on, its
standard input on the FIFO). Two clients stand two blocks apart; the console
gives A speed II, strength I and health boost I, then A punches B (fist:
damage 2 boosted); then weakness I, and A punches B again 1 s later; then the
effects are cleared.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_oracle import move_body, spawn  # noqa: E402
from bedrock_client import BedrockClient  # noqa: E402
from combat_oracle import attack, collect, console, item  # noqa: E402


def main():
    host, port, output, fifo = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3]), sys.argv[4]
    a, b = BedrockClient(host, port, "Alpha"), BedrockClient(host, port, "Bravo")
    clients = {"a": a, "b": b}
    steps = []
    try:
        ra, rb = spawn(a)["runtime_id"], spawn(b)["runtime_id"]
        pa, pb = (0.5, 5.62, 0.5), (2.5, 5.62, 0.5)
        a.send_packets([(0x13, move_body(ra, *pa, ground=True))])
        b.send_packets([(0x13, move_body(rb, *pb, yaw=90.0, head=90.0, ground=True))])
        collect(clients, 2)
        console(fifo, "effect Alpha speed 30 1", "effect Alpha strength 30 0", "effect Alpha health_boost 30 0")
        steps.append({"step": "effects", "packets": collect(clients, 1.5)})
        a.send_packets([attack(rb, 0, item(0), pa)])
        steps.append({"step": "strong_punch", "packets": collect(clients, 1.0)})
        console(fifo, "effect Alpha weakness 30 0")
        steps.append({"step": "weakness", "packets": collect(clients, 1.0)})
        a.send_packets([attack(rb, 0, item(0), pa)])
        steps.append({"step": "weak_punch", "packets": collect(clients, 1.0)})
        console(fifo, "effect Alpha clear")
        steps.append({"step": "clear", "packets": collect(clients, 1.5)})
    finally:
        a.raknet.close()
        b.raknet.close()
    output.write_text(json.dumps({"runtime_a": ra, "runtime_b": rb, "steps": steps}, indent=1) + "\n")


if __name__ == "__main__":
    main()
