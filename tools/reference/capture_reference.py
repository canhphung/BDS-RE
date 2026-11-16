#!/usr/bin/env python3
"""Join a running Bedrock server and record the traffic up to spawn.

    python3 tools/reference/capture_reference.py 127.0.0.1 19140 out.json

Against the original BDS (online-mode=false) this yields reference captures:
the discovery advertisement, the handshake token, StartGame (with the block
palette), and whatever the server streams after RequestChunkRadius. Captures
contain no private keys; the throwaway client key is discarded.
"""
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bedrock_client import BedrockClient, uvarint  # noqa: E402


def main():
    host, port, output = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3])
    radius = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    client = BedrockClient(host, port)
    advertisement = client.raknet.ping()
    started = time.time()
    try:
        client.join()
        # RequestChunkRadius: zigzag varint.
        client.send_packets([(0x45, uvarint(radius << 1))])
        client.drain(8.0)
    finally:
        capture = {
            "server": f"{host}:{port}",
            "captured_at": int(started),
            "advertisement": advertisement,
            "handshake_header_alg": getattr(client, "handshake", {}).get("header", {}).get("alg"),
            "handshake_claims": sorted(getattr(client, "handshake", {}).get("claims", {}).keys()),
            "requested_chunk_radius": radius,
            "packets": client.log,
        }
        output.write_text(json.dumps(capture, indent=1) + "\n")
        client.raknet.close()
    ids = {}
    for packet in capture["packets"]:
        ids[packet["id"]] = ids.get(packet["id"], 0) + 1
    print(json.dumps({"packets": len(capture["packets"]), "by_id": {hex(k): v for k, v in sorted(ids.items())}}))


if __name__ == "__main__":
    main()
