#!/usr/bin/env python3
"""Run a bounded original-server RNG trace in a new disposable directory.

Unlike the interactive helpers this owns only its child process: it never
pkills other servers or removes an existing world. Requires gdb with Python.
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
from bedrock_client import BedrockClient, ProtocolFailure  # noqa: E402
from movement_oracle import spawn  # noqa: E402
from capture_item_use_errors import ORIGINAL_SHA256  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--library-path", type=Path, required=True)
    parser.add_argument("--preload", type=Path)
    parser.add_argument("--gdb", default="gdb")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ticks", type=int, default=200)
    parser.add_argument("--players", default="A")
    parser.add_argument("--port", type=int, default=19220)
    parser.add_argument("--seed", type=int, default=123)
    parser.add_argument("--generator", choices=("original", "flat"), default="flat")
    parser.add_argument("--block-samples", action="store_true")
    parser.add_argument("--command", action="append", default=[],
                        help="Original console command to execute after every client has spawned")
    parser.add_argument("--grass-oracle", action="store_true",
                        help="After tracing, execute the original GrassBlock::tick oracle in the paused world")
    parser.add_argument("--lighting-oracle", action="store_true")
    parser.add_argument("--lighting-cases", type=int, default=128)
    parser.add_argument("--tick-order", action="store_true")
    parser.add_argument("--gdb-script", action="append", default=[], type=Path,
                        help="Extra gdb Python oracle sourced after the sequence probe (may use `state`)")
    parser.add_argument("--gdb-post-script", action="append", default=[], type=Path,
                        help="Native function oracle sourced at the final paused tick")
    parser.add_argument("--scenario", help="Client scenario from tools/re/scenarios.py run by the first player")
    parser.add_argument("--packet-log", action="store_true",
                        help="Write every packet each client received to packets_<name>.json")
    parser.add_argument("--spawn-oracle", action="store_true",
                        help="Also dump biome spawn lists and every Spawner::spawnMob call")
    parser.add_argument("--drop-items", action="store_true",
                        help="Give/drop one dirt per spawned client for chunk/item order traces")
    args = parser.parse_args()
    if not 1 <= args.lighting_cases <= 4096:
        parser.error("lighting-cases must be 1..4096")
    if args.grass_oracle and args.lighting_oracle:
        parser.error("grass and lighting function oracles need separate scene runs")
    names = args.players.split(",")
    if not 1 <= args.ticks <= 10000 or not 1 <= len(names) <= 10:
        parser.error("ticks must be 1..10000 and players 1..10")
    runtime, output = args.runtime.resolve(), args.output.resolve()
    if hashlib.sha256((runtime / "bedrock_server").read_bytes()).hexdigest() != ORIGINAL_SHA256:
        parser.error("unexpected original binary")
    output.mkdir(parents=True, exist_ok=False)
    run = output / "runtime"
    run.mkdir()
    for name in ("bedrock_server", "libCrypto.so", "behavior_packs", "resource_packs",
                 "definitions", "structures"):
        (run / name).symlink_to(runtime / name, target_is_directory=(runtime / name).is_dir())
    properties = {"server-name": "RandomOracle", "level-name": "O", "level-seed": str(args.seed),
                  "server-port": str(args.port), "server-portv6": str(args.port + 1),
                  "online-mode": "false", "gamemode": "survival", "difficulty": "normal",
                  "view-distance": "8", "tick-distance": "4", "allow-cheats": "true", "max-threads": "2"}
    (run / "server.properties").write_text("".join(f"{key}={value}\n" for key, value in properties.items()))
    env = os.environ.copy()
    env.update(TRACE_OUT=str(output / "draws.jsonl"), TRACE_TICKS=str(args.ticks),
               TRACE_PLAYERS=str(len(names)), TRACE_GENERATOR=args.generator)
    env["TRACE_BLOCK_SAMPLES"] = str(int(args.block_samples))
    env["TRACE_TICK_ORDER"] = str(int(args.tick_order))
    env["LIGHTING_CASES"] = str(args.lighting_cases)
    env["LIGHTING_ORACLE_OUT"] = str(output / "lighting.json")
    env["GRASS_ORACLE_OUT"] = str(output / "grass_ticks.json")
    env["SPAWN_ORACLE_OUT"] = str(output / "spawns.json")
    command = [args.gdb, "-nx", "-q", "-batch", "-ex", "set pagination off", "-ex", "set confirm off",
               "-ex", "set debuginfod enabled off", "-ex", "handle SIGPIPE nostop noprint pass",
               "-ex", f"set environment LD_LIBRARY_PATH={args.library_path.resolve()}:."]
    if args.preload:
        command += ["-ex", f"set environment LD_PRELOAD={args.preload.resolve()}"]
    command += ["-ex", "break main", "-ex", "run", "-ex",
                "source " + str(Path(__file__).with_name("gdb_level_random_sequence.py"))]
    if args.spawn_oracle:
        command += ["-ex", "source " + str(Path(__file__).with_name("gdb_spawn_oracle.py"))]
    for script in args.gdb_script:
        command += ["-ex", "source " + str(script.resolve())]
    env["ORACLE_OUT_DIR"] = str(output)
    command += ["-ex", "continue"]
    if args.grass_oracle:
        command += ["-ex", "source " + str(Path(__file__).with_name("gdb_grass_ticks.py"))]
    if args.lighting_oracle:
        command += ["-ex", "source " + str(Path(__file__).with_name("gdb_lighting_oracle.py"))]
    for script in args.gdb_post_script:
        command += ["-ex", "source " + str(script.resolve())]
    command += ["-ex", "kill", "--args", "./bedrock_server"]
    process = subprocess.Popen(command, cwd=run, env=env, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               start_new_session=True)
    lines, errors, clients, threads, spawned, dropped = [], [], [], [], [], []
    commands_ready = threading.Event()
    setup_commands = list(args.command)
    if args.drop_items:
        setup_commands += [f"give {name} dirt 1" for name in names]
    command_lock = threading.Lock()
    stopping = threading.Event()

    def collect():
        with (output / "gdb.log").open("w") as log:
            for line in process.stdout:
                lines.append(line.rstrip())
                log.write(line)
                log.flush()

    reader = threading.Thread(target=collect, daemon=True)
    reader.start()

    def join(name):
        client = BedrockClient("127.0.0.1", args.port, name)
        clients.append(client)
        try:
            game = spawn(client)
            print(f"spawned {name}: generator={game['settings']['generator']} position={game['position']}", flush=True)
            with command_lock:
                spawned.append(name)
                if len(spawned) == len(names):
                    for text in setup_commands:
                        process.stdin.write(text + "\n")
                    process.stdin.flush()
                    commands_ready.set()
            sent_drop = False
            if args.scenario and name == names[0]:
                commands_ready.wait(60)
                from scenarios import SCENARIOS
                scenario_log = []
                SCENARIOS[args.scenario](client, game, scenario_log)
                (output / "scenario.json").write_text(json.dumps(scenario_log) + "\n")
            while not stopping.is_set() and process.poll() is None:
                client.drain(.2)
                if args.drop_items and commands_ready.is_set() and not sent_drop:
                    from analyze_capture import Reader
                    from combat_oracle import drop, item
                    for packet in reversed(client.log):
                        if packet["id"] != 0x31:
                            continue
                        data = Reader(bytes.fromhex(packet["body"]))
                        if data.uvarint() != 0 or data.uvarint() == 0:
                            continue
                        if data.varint() == 3 and data.varint() == 1:
                            request = drop(0, item(3), item(0), item(3))
                            client.send_packets([request])
                            dropped.append({"player": name, "id": request[0], "body": request[1].hex()})
                            sent_drop = True
                        break
        except (ProtocolFailure, OSError) as error:
            if process.poll() is None and not stopping.is_set():
                errors.append(f"{name}: {error}")

    try:
        deadline = time.monotonic() + 120
        while not any("IPv4 supported" in line for line in lines):
            if process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("original did not start: " + "\n".join(lines[-20:]))
            time.sleep(.05)
        for name in names:
            thread = threading.Thread(target=join, args=(name,), daemon=True)
            threads.append(thread)
            thread.start()
        deadline = time.monotonic() + max(120, args.ticks * 2)
        while process.poll() is None:
            if errors or time.monotonic() >= deadline:
                raise RuntimeError(f"trace did not finish: {errors}")
            time.sleep(.1)
        if any("Error occurred in Python" in line or "Python Exception" in line for line in lines):
            raise RuntimeError("native GDB oracle failed: " + "\n".join(lines[-20:]))
        records = [json.loads(line) for line in (output / "draws.jsonl").read_text().splitlines()]
        if not records or records[-1]["event"] != "complete" or process.returncode:
            raise RuntimeError("incomplete trace: " + "\n".join(lines[-15:]))
        if args.drop_items and len(dropped) != len(names):
            raise RuntimeError("not all original clients dropped their items")
        if args.grass_oracle:
            oracle = output / "grass_ticks.json"
            if not oracle.exists() or len(json.loads(oracle.read_text())["cases"]) != 80:
                raise RuntimeError("grass oracle failed: " + "\n".join(lines[-20:]))
        if args.packet_log:
            for client in clients:
                (output / f"packets_{client.name}.json").write_text(json.dumps(client.log) + "\n")
        metadata = {"binary_sha256": ORIGINAL_SHA256, "properties": properties,
                    "generator_override": args.generator, "players": names,
                    "commands": setup_commands, "item_drop_requests": dropped,
                    "tick_order": args.tick_order,
                    "trace_sha256": hashlib.sha256((output / "draws.jsonl").read_bytes()).hexdigest(),
                    "completion": records[-1]}
        (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        if args.grass_oracle:
            fixture = json.loads(oracle.read_text())
            fixture.update(scene_setup_commands=args.command,
                           generator_override=args.generator,
                           source_trace_sha256=metadata["trace_sha256"])
            oracle.write_text(json.dumps(fixture, indent=1) + "\n")
        if args.lighting_oracle:
            oracle = output / "lighting.json"
            fixture = json.loads(oracle.read_text())
            if len(fixture["block_ticks"]) != args.lighting_cases:
                raise RuntimeError("incomplete original lighting oracle")
            fixture.update(scene_setup_commands=setup_commands, source_trace_sha256=metadata["trace_sha256"],
                           generator_override=args.generator)
            oracle.write_text(json.dumps(fixture, indent=1) + "\n")
        print(json.dumps(metadata["completion"]))
    finally:
        stopping.set()
        if process.poll() is None:
            # Only our debugger/process group, created above, is terminated.
            import signal
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        for client in clients:
            client.raknet.close()
        for thread in threads:
            thread.join(timeout=1)
        reader.join(timeout=2)


if __name__ == "__main__":
    main()
