#!/usr/bin/env python3
"""join_players.py PORT Name1,Name2 SECONDS - spawn clients one after another and keep them connected."""
import sys, time
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "reference"))
from movement_oracle import spawn
from bedrock_client import BedrockClient
port = int(sys.argv[1]); names = sys.argv[2].split(","); hold = float(sys.argv[3])
cs = []
for n in names:
    c = BedrockClient("127.0.0.1", port, n); g = spawn(c); cs.append(c); print(n, g["runtime_id"], flush=True)
    c.drain(0.5)
end = time.monotonic() + hold
while time.monotonic() < end:
    for c in cs: c.drain(0.05)
for c in cs: c.raknet.close()
