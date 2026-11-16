"""Exercise the real executable: console, storage, lists, backup, IPv4/IPv6 discovery."""
import json
from pathlib import Path
import queue
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time


def require(value, message):
    if not value:
        raise AssertionError(message)


def available_port(family, host):
    with socket.socket(family, socket.SOCK_DGRAM) as s:
        if family == socket.AF_INET6:
            s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        s.bind((host, 0))
        return s.getsockname()[1]


def ipv6_available():
    """True when the host can bind IPv6 loopback; the server must cope either way."""
    if not socket.has_ipv6:
        return False
    try:
        with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as s:
            s.bind(("::1", 0))
        return True
    except OSError:
        return False


with tempfile.TemporaryDirectory(prefix="bds-console-") as directory:
    root = Path(directory)
    port = available_port(socket.AF_INET, "127.0.0.1")
    has_ipv6 = ipv6_available()
    # Without IPv6 any configured port is fine: the server must log and continue.
    port6 = available_port(socket.AF_INET6, "::1") if has_ipv6 else 19133
    properties = (f"server-name=Integration Server\nlevel-name=World With Spaces\n"
                  f"server-port={port}\nserver-portv6={port6}\nwhite-list=false\nallow-cheats=false\n")
    (root / "server.properties").write_text(properties, encoding="utf-8")
    (root / "opsfile.json").write_text('[{"xuid":"123"}]', encoding="utf-8")
    process = subprocess.Popen([sys.argv[1], "--root", str(root), "--tick-ms", "2"],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, encoding="utf-8", bufsize=1)
    output = queue.Queue()
    transcript = []

    def read_output():
        for line in process.stdout:
            transcript.append(line.rstrip())
            output.put(line.rstrip())

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()

    def wait_for(fragment):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            try:
                line = output.get(timeout=0.1)
                if fragment in line:
                    return line
            except queue.Empty:
                if process.poll() is not None:
                    break
        raise AssertionError(f"missing {fragment!r}: {transcript}")

    def command(text):
        process.stdin.write(text + "\n")
        process.stdin.flush()

    try:
        wait_for(f"IPv4 supported, port: {port}")
        wait_for(f"IPv6 supported, port: {port6}" if has_ipv6 else "IPv6 not supported")
        wait_for("console ready")
        command('whitelist add "Example Name"')
        wait_for("whitelist updated")
        require(json.loads((root / "whitelist.json").read_text())[0]["name"] == "Example Name", "quoted whitelist persistence")
        command("whitelist on")
        wait_for("whitelist on (runtime only)")
        command("ops list")
        wait_for("operators entries=1")
        command("changesetting difficulty hard")
        wait_for("setting changed: difficulty")
        command("changesetting allow-cheats true")
        wait_for("setting changed: allow-cheats")
        magic = bytes.fromhex("00ffff00fefefefefdfdfdfd12345678")
        ping = b"\x01" + struct.pack(">Q", 0x0102030405060708) + magic + struct.pack(">Q", 99)
        endpoints = [(socket.AF_INET, ("127.0.0.1", port))]
        if has_ipv6:
            endpoints.append((socket.AF_INET6, ("::1", port6)))
        for family, address in endpoints:
            with socket.socket(family, socket.SOCK_DGRAM) as client:
                client.settimeout(2)
                client.sendto(ping, address)
                pong, _ = client.recvfrom(2048)
                require(pong[0] == 0x1C and pong[1:9] == ping[1:9] and pong[17:33] == magic, "discovery framing")
                length = struct.unpack_from(">H", pong, 33)[0]
                require(len(pong) == 35 + length, "advertisement length")
                fields = pong[35:].decode("utf-8").split(";")
                require(fields[:6] == ["MCPE", "Integration Server", "282", "1.6.0", "0", "10"], "advertisement fields")
                require(fields[7] == "World With Spaces", "advertised world name")
                client.sendto(b"\x01bad", address)
                client.settimeout(0.15)
                try:
                    client.recvfrom(2048)
                    raise AssertionError("malformed discovery was accepted")
                except socket.timeout:
                    pass
        command("save hold")
        wait_for("backup preparing")
        deadline = time.monotonic() + 8
        backup_file = None
        while time.monotonic() < deadline:
            command("save query")
            # Queries emit one line; allow asynchronous preparation to finish.
            line = wait_for("backup ")
            if "backup ready:" in line:
                match = re.search(r'backup ready: (".*") length=(\d+)', line)
                require(match, "backup query file/length")
                # std::quoted uses C-style backslash escaping, compatible here with JSON.
                backup_file = Path(json.loads(match.group(1)))
                backup_length = int(match.group(2))
                break
            time.sleep(0.01)
        require(backup_file and backup_file.is_file(), "backup preparation")
        before = backup_file.read_bytes()
        age = struct.unpack_from("<Q", before, 12)[0]
        require(len(before) == backup_length, "backup copy length")
        # Keep the backup held while ticks continue and verify the pinned file is immutable.
        time.sleep(0.1)
        require(backup_file.read_bytes() == before, "backup changed while held")
        command("save resume")
        wait_for("backup resumed")
        require(not backup_file.exists(), "backup not released")
        command("stop")
        process.stdin.close()
        require(process.wait(timeout=8) == 0, "console stop failed")
        reader.join(timeout=2)
        snapshot = root / "worlds" / "World With Spaces" / "bdsre-world.snapshot"
        require(struct.unpack_from("<Q", snapshot.read_bytes(), 12)[0] > age, "ticks paused during backup")
        require((root / "server.properties").read_text() == properties, "runtime command rewrote properties")
        # Restart after console stop must reload the selected world.
        result = subprocess.run([sys.argv[1], "--root", str(root), "--network", "0", "--ticks", "2", "--tick-ms", "1"],
                                input="", capture_output=True, text=True, timeout=8)
        require(result.returncode == 0, result.stdout + result.stderr)
        require("stopped ticks=2" in result.stdout, "restart tick count")
        require(json.loads((root / "whitelist.json").read_text())[0]["name"] == "Example Name", "whitelist lost on restart")
        # An original BDS world must not be silently rewritten in the internal format.
        (snapshot.parent / "db").mkdir()
        preserved = snapshot.read_bytes()
        original = subprocess.run([sys.argv[1], "--root", str(root), "--network", "0", "--ticks", "1"],
                                  input="", capture_output=True, text=True, timeout=8)
        require(original.returncode == 1 and "original Bedrock storage is not supported" in original.stdout,
                "original world was accepted by internal storage")
        require(snapshot.read_bytes() == preserved, "original world guard modified checkpoint")
        print(f"Native console, restart, whitelist/operator files, asynchronous backup and {'dual-stack' if has_ipv6 else 'IPv4-only fallback'} discovery passed")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        if not process.stdin.closed:
            process.stdin.close()
        reader.join(timeout=2)
        process.stdout.close()
