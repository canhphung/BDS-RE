#!/usr/bin/env python3
"""Trace dropped items in the original BDS 1.6.0.15 under gdb.

    python3 tools/reference/trace_item_actor.py --runtime input/linux/bds \
        --library-path <openssl-1.0>/lib --preload <ipv6_shim.so> \
        --output input/reference/items-new [--scenario merge]

Default scenario: a survival player breaks grass at (2,64,1) in a prepared
FLAT world and steps into the hole to pick the dirt up. "merge": two
adjacent drops merge, then the hole is refilled over them. gdb_item_actor.py
logs every ItemActor::normalTick entry; packets go to packets.json. The
committed references tests/reference/item_actor*_bds_1.6.0.15.json were
extracted from such runs.
"""
import argparse, json, os, struct, subprocess, sys, threading, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_capture import parse_start_game
from bedrock_client import BedrockClient, uvarint
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--runtime", type=Path, required=True)
parser.add_argument("--library-path", type=Path, required=True)
parser.add_argument("--preload", type=Path)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--port", type=int, default=19180)
parser.add_argument("--scenario", choices=["pickup", "merge"], default="pickup")
args = parser.parse_args()
OUT = args.output.resolve(); PORT = args.port; RUNTIME = args.runtime.resolve()
HERE = Path(__file__).resolve().parent
EYE = (0.5, 66.62, 0.5)
def vi(v): return uvarint((v << 1) ^ (v >> 31))
def brk(x, y, z): return b"\x02\x00" + uvarint(2) + vi(x) + uvarint(y) + vi(z) + vi(1) + vi(0) + vi(0) + struct.pack("<6f", *EYE, 0, 0, 0)
def move(rid, p, ground=True): return uvarint(rid) + struct.pack("<6f", *p, 0, 0, 0) + bytes([0, int(ground), 0])
OUT.mkdir(parents=True)
run = OUT / "runtime"; run.mkdir()
for n in ("bedrock_server", "libCrypto.so", "behavior_packs", "resource_packs", "definitions", "structures"):
    (run / n).symlink_to(RUNTIME / n, target_is_directory=(RUNTIME / n).is_dir())
props = {"server-name": "Trace", "level-name": "T", "level-seed": "123", "level-type": "FLAT", "server-port": str(PORT),
         "server-portv6": str(PORT + 1), "online-mode": "false", "allow-cheats": "true", "gamemode": "survival",
         "difficulty": "peaceful", "view-distance": "8"}
(run / "server.properties").write_text("".join(f"{k}={v}\n" for k, v in props.items()))
env = dict(os.environ, LD_LIBRARY_PATH=str(args.library_path.resolve()) + ":.", TRACE_OUT=str(OUT / "gdb.log"))
preload = ["-ex", f"set environment LD_PRELOAD={args.preload.resolve()}"] if args.preload else []
gdbcmd = ["gdb", "-q", "-batch", "-ex", "set pagination off", "-ex", "handle SIGPIPE nostop noprint pass",
          *preload, "-ex", "break main", "-ex", "run",
          "-ex", f"source {HERE / 'gdb_item_actor.py'}", "-ex", "continue", "--args", "./bedrock_server"]
p = subprocess.Popen(gdbcmd, cwd=run, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
lines = []
threading.Thread(target=lambda: [lines.append(l.rstrip()) for l in p.stdout], daemon=True).start()
def cmd(t): p.stdin.write(t + "\n"); p.stdin.flush()
log = []
try:
    t0 = time.monotonic()
    while not any("IPv4 supported" in l for l in lines):
        if p.poll() is not None or time.monotonic() - t0 > 120: raise RuntimeError("\n".join(lines[-30:]))
        time.sleep(.1)
    c = BedrockClient("127.0.0.1", PORT, name="Tracer")
    joined = c.join()
    rid = parse_start_game(next(b for i, b in joined if i == 11))["runtime_id"]
    c.send_packets([(0x45, vi(5))]); c.drain(3)
    c.send_packets([(0x71, uvarint(rid))]); c.drain(.5)
    for t in ["gamerule domobspawning false", "fill -2 63 -2 14 64 3 stone", "fill -2 65 -2 14 70 3 air",
              "tp Tracer 0.5 65 0.5", "setblock 2 64 1 grass", "setblock 5 64 1 dirt"]:
        cmd(t)
    c.drain(3)
    c.send_packets([(0x13, move(rid, EYE))]); c.drain(1)
    mark = {"break": len(c.log)}
    if args.scenario == "merge":
        cmd("setblock 6 64 1 dirt"); c.drain(1)
        mark = {"break": len(c.log)}
        c.send_packets([(0x1e, brk(5, 64, 1)), (0x1e, brk(6, 64, 1))]); c.drain(3)
        cmd("say refill"); cmd("fill 5 64 1 6 64 1 stone"); c.drain(3)
        mark["walk"] = len(c.log); mark["end"] = len(c.log)
    else:
        c.send_packets([(0x1e, brk(2, 64, 1))]); c.drain(4)
        mark["walk"] = len(c.log)
        c.send_packets([(0x13, move(rid, (2.5, 65.62, 1.5)))]); c.drain(3)
        mark["end"] = len(c.log)
    json.dump({"marks": mark, "packets": c.log}, open(OUT / "packets.json", "w"))
    c.raknet.close()
finally:
    try: cmd("stop"); p.wait(timeout=60)
    except Exception: p.kill()
    (OUT / "console.log").write_text("\n".join(lines))
