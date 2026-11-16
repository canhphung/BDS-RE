"""A failure inside the running loop must still save the last consistent world.

World age exhaustion is used as a deterministic in-loop failure: the snapshot
starts three ticks before UINT64_MAX, so three ticks succeed and the fourth
throws. The emergency save must record the advanced age, proving a write
happened rather than the input file merely surviving.
"""
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

MAGIC = b"BDSREW\r\n"
MAX = 2**64 - 1


def snapshot(age, blocks):
    body = MAGIC + struct.pack("<IQQ", 1, age, len(blocks))
    for x, y, z, block in blocks:
        body += struct.pack("<iHiH", x, y, z, block)
    return body + struct.pack("<I", zlib.crc32(body))


def require(condition, message):
    if not condition:
        raise AssertionError(message)


with tempfile.TemporaryDirectory(prefix="bds-failure-") as directory:
    world = Path(directory) / "failure.snapshot"
    blocks = [(-1, 64, 5, 7)]
    world.write_bytes(snapshot(MAX - 3, blocks))
    result = subprocess.run([sys.argv[1], "--network", "0", "--world", str(world), "--ticks", "10", "--tick-ms", "1"],
                            input="", capture_output=True, text=True, timeout=10)
    output = result.stdout + result.stderr
    require(result.returncode == 1, f"in-loop failure must exit nonzero: {output}")
    require("runtime failure: world age exhausted" in output, f"failure cause not logged: {output}")
    require(f"emergency save world age={MAX}" in output, f"emergency save not reported: {output}")
    require(world.read_bytes() == snapshot(MAX, blocks), "emergency save did not persist the advanced world")
    print("In-loop failure saved the last consistent world before exiting")
