"""MovePlayer behaviour of BDS-RE against observations of the original BDS.

tests/reference/movement_bds_1.6.0.15.json was produced by
tools/reference/movement_oracle.py against the original server: every
MovePlayer (40-block jump, Y=1000, pitch 95, teleport mode with riding id and
extra fields) was relayed to the other player byte for byte, and the mover
received no correction. The same oracle must observe the same against BDS-RE.
"""
import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
reference = json.loads((root / "tests/reference/movement_bds_1.6.0.15.json").read_text())


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


with tempfile.TemporaryDirectory(prefix="bds-movement-") as directory:
    work = Path(directory)
    port = free_port()
    (work / "server.properties").write_text(f"server-port={port}\nonline-mode=false\nlevel-name=Movement\nview-distance=8\n")
    server = subprocess.Popen([sys.argv[1], "--root", str(work)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
    try:
        deadline = time.monotonic() + 10
        for line in server.stdout:
            if "console ready" in line or time.monotonic() > deadline:
                break
        output = work / "observed.json"
        subprocess.run([sys.executable, str(root / "tools/reference/movement_oracle.py"), "127.0.0.1", str(port), str(output)],
                       check=True, timeout=120)
        observed = json.loads(output.read_text())
        assert reference["relayed_unchanged"] and not reference["corrections_to_a"], "reference fixture changed"
        assert observed["relayed_unchanged"], f"relay differs from the original: {observed}"
        assert len(observed["relayed_to_b"]) == len(reference["relayed_to_b"]), observed
        assert observed["corrections_to_a"] == [], f"BDS-RE corrected movement the original accepts: {observed}"
        print("MovePlayer relay and acceptance match the original BDS observations")
    finally:
        server.stdin.write("stop\n")
        server.stdin.flush()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
