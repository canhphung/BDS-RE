"""Movement-related PlayerActions and entity visibility of BDS-RE against the
original BDS.

tests/reference/player_actions_bds_1.6.0.15.json was produced by
tools/reference/player_actions_oracle.py against the original server: sneak
and sprint flags (SetActorData), the sprint movement attribute, the relay
rules of PlayerAction, RemoveActor when a player leaves the client ticking
area and AddPlayer (with metadata) plus MobArmorEquipment when it returns.
The same oracle must observe the same packets about the acting player.
"""
import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
reference = json.loads((root / "tests/reference/player_actions_bds_1.6.0.15.json").read_text())


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


with tempfile.TemporaryDirectory(prefix="bds-actions-") as directory:
    work = Path(directory)
    port = free_port()
    (work / "server.properties").write_text(f"server-port={port}\nonline-mode=false\nlevel-name=Actions\nview-distance=8\n")
    server = subprocess.Popen([sys.argv[1], "--root", str(work)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
    try:
        deadline = time.monotonic() + 10
        for line in server.stdout:
            if "console ready" in line or time.monotonic() > deadline:
                break
        output = work / "observed.json"
        subprocess.run([sys.executable, str(root / "tools/reference/player_actions_oracle.py"), "127.0.0.1", str(port),
                        str(output)], check=True, timeout=150)
        observed = json.loads(output.read_text())
        for expected, got in zip(reference["steps"], observed["steps"], strict=True):
            assert expected["step"] == got["step"]
            for who in ("to_actor", "to_watcher"):
                assert got["about_actor"][who] == expected[who], (
                    f"{expected['step']} {who}: expected {expected[who]} got {got['about_actor'][who]}")
        print("PlayerAction flags/attributes/relay and entity visibility match the original BDS observations")
    finally:
        server.stdin.write("stop\n")
        server.stdin.flush()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
