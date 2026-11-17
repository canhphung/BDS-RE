#!/usr/bin/env python3
"""Instant health / instant damage on one client (see effect_modifier_oracle.py for the server setup).

    python3 tools/reference/instant_effect_oracle.py HOST PORT OUT.json CONSOLE_FIFO
"""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_oracle import move_body, spawn
from bedrock_client import BedrockClient
from combat_oracle import collect, console
from hunger_health_oracle import fall
host, port, out, fifo = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
a = BedrockClient(host, port, "Alpha")
ra = spawn(a)["runtime_id"]
pa = (0.5, 5.62, 0.5)
a.send_packets([(0x13, move_body(ra, *pa, ground=True))]); collect({"a": a}, 2)
a.send_packets([(0x13, move_body(ra, pa[0], pa[1] + 10, pa[2], ground=False))]); collect({"a": a}, 0.3)
a.send_packets([(0x13, move_body(ra, *pa, ground=True)), (0x25, fall(ra, 10.0))]); collect({"a": a}, 1)
steps = []
console(fifo, "effect Alpha instant_health 1 0"); steps.append({"step": "heal", "packets": collect({"a": a}, 2)})
console(fifo, "effect Alpha instant_damage 1 1"); steps.append({"step": "harm", "packets": collect({"a": a}, 2)})
a.raknet.close()
Path(out).write_text(json.dumps({"runtime_a": ra, "runtime_b": 0, "steps": steps}))
