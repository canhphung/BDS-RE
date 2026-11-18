#!/usr/bin/env python3
"""Record the original's generated terrain (Linux 1.6.0.15, infinite overworld).

The level-Random tracer of capture_level_random.py stops on every Random call,
which starves the chunk worker threads, so this runner only loads
gdb_terrain_oracle.py. One client joins and stays until `--chunks`
FullChunkData packets arrived; the chunk packets are kept as well (they carry
the post-processed blocks and the biome ids).

    python3 tools/re/capture_terrain.py --runtime <copy of input/linux/bds> \
        --library-path <openssl lib> --preload <ipv6_shim.so> --output <dir> --seed 123
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

REFERENCE = Path(__file__).resolve().parents[1] / "reference"
sys.path.insert(0, str(REFERENCE))
from bedrock_client import BedrockClient  # noqa: E402
from movement_oracle import spawn  # noqa: E402
from capture_item_use_errors import ORIGINAL_SHA256  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--library-path", type=Path, required=True)
    parser.add_argument("--preload", type=Path)
    parser.add_argument("--gdb", default="gdb")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=123)
    parser.add_argument("--port", type=int, default=19230)
    parser.add_argument("--chunks", type=int, default=100)
    parser.add_argument("--view", type=int, default=6)
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument("--layer-oracle", action="store_true",
                        help="Stop once and call every biome layer (gdb_layer_oracle.py) instead")
    args = parser.parse_args()
    runtime, output = args.runtime.resolve(), args.output.resolve()
    if hashlib.sha256((runtime / "bedrock_server").read_bytes()).hexdigest() != ORIGINAL_SHA256:
        parser.error("unexpected original binary")
    output.mkdir(parents=True, exist_ok=False)
    run = output / "runtime"
    run.mkdir()
    for name in ("bedrock_server", "libCrypto.so", "behavior_packs", "resource_packs",
                 "definitions", "structures"):
        (run / name).symlink_to(runtime / name, target_is_directory=(runtime / name).is_dir())
    properties = {"server-name": "TerrainOracle", "level-name": "T", "level-seed": str(args.seed),
                  "server-port": str(args.port), "server-portv6": str(args.port + 1),
                  "online-mode": "false", "gamemode": "creative", "difficulty": "peaceful",
                  "view-distance": str(args.view), "tick-distance": "4", "max-threads": "2"}
    (run / "server.properties").write_text("".join(f"{k}={v}\n" for k, v in properties.items()))
    env = os.environ.copy()
    env["TERRAIN_ORACLE_OUT"] = str(output / "terrain.jsonl")
    # VanillaBiomes::m* globals (address, name) for the biome registry dump.
    vanilla = output / "vanilla_biomes.txt"
    symbols = subprocess.run(["nm", "-C", str(runtime / "bedrock_server")], capture_output=True, text=True).stdout
    vanilla.write_text("".join(f"{line.split()[0]} {line.split()[2]}\n" for line in symbols.splitlines()
                               if " VanillaBiomes::m" in line and line.split()[1] in "BbDd"))
    env["TERRAIN_VANILLA_BIOMES"] = str(vanilla)
    command = [args.gdb, "-nx", "-q", "-batch", "-ex", "set pagination off", "-ex", "set confirm off",
               "-ex", "set debuginfod enabled off", "-ex", "handle SIGPIPE nostop noprint pass",
               "-ex", f"set environment LD_LIBRARY_PATH={args.library_path.resolve()}:."]
    if args.preload:
        command += ["-ex", f"set environment LD_PRELOAD={args.preload.resolve()}"]
    if args.layer_oracle:
        env["LAYER_ORACLE_OUT"] = str(output / "layers.json")
        env["LAYER_ORACLE_BIN"] = str(runtime / "bedrock_server")
        command += ["-ex", "source " + str(Path(__file__).with_name("gdb_layer_oracle.py")),
                    "-ex", "run", "-ex", "python layer_dump()", "-ex", "kill", "--args", "./bedrock_server"]
    else:
        command += ["-ex", "source " + str(Path(__file__).with_name("gdb_terrain_oracle.py")),
                    "-ex", "run", "--args", "./bedrock_server"]
    process = subprocess.Popen(command, cwd=run, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, start_new_session=True)
    lines = []

    def collect():
        with (output / "gdb.log").open("w") as log:
            for line in process.stdout:
                lines.append(line.rstrip())
                log.write(line)
                log.flush()

    threading.Thread(target=collect, daemon=True).start()
    try:
        deadline = time.monotonic() + 180
        while not any("IPv4 supported" in line for line in lines):
            if process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("original did not start: " + "\n".join(lines[-20:]))
            time.sleep(.1)
        time.sleep(2)
        client = BedrockClient("127.0.0.1", args.port, "Terra")
        if args.layer_oracle:
            try:
                spawn(client)
            except Exception:  # the server stops at the layer breakpoint
                pass
            process.wait(timeout=args.timeout)
            print("layers:", (output / "layers.json").exists(), flush=True)
            return
        game = spawn(client)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            client.drain(1.0)
            if sum(1 for p in client.log if p["id"] == 0x3a) >= args.chunks:
                break
        chunks = [p for p in client.log if p["id"] == 0x3a]
        (output / "chunks.json").write_text(json.dumps(chunks) + "\n")
        (output / "metadata.json").write_text(json.dumps({
            "binary_sha256": ORIGINAL_SHA256, "properties": properties, "spawn": game.get("position"),
            "chunk_packets": len(chunks),
            "generator": "tools/re/capture_terrain.py with tools/re/gdb_terrain_oracle.py"}, indent=1) + "\n")
        print(f"chunks: {len(chunks)}", flush=True)
    finally:
        os.killpg(process.pid, 9)
        process.wait()


if __name__ == "__main__":
    main()
