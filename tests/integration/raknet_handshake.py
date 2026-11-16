"""Independent byte-level RakNet 9 test client; this is not a Minecraft client."""
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import json
import zlib

MAGIC = bytes.fromhex("00ffff00fefefefefdfdfdfd12345678")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def port(family, host):
    with socket.socket(family, socket.SOCK_DGRAM) as sock:
        if family == socket.AF_INET6:
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        sock.bind((host, 0))
        return sock.getsockname()[1]


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


def triad(n):
    return n.to_bytes(3, "little")


def address(host, port):
    if ":" in host:
        return b"\x06" + struct.pack("<H", 23) + struct.pack(">H", port) + bytes(4) + socket.inet_pton(socket.AF_INET6, host) + bytes(4)
    return b"\x04" + bytes(byte ^ 255 for byte in socket.inet_aton(host)) + struct.pack(">H", port)


def skip_address(data, start=0):
    require(start < len(data) and data[start] in (4, 6), "invalid wire address")
    end = start + (29 if data[start] == 6 else 7)
    require(end <= len(data), "truncated wire address")
    return end


def frame(sequence, reliable, order, body):
    return b"\x84" + triad(sequence) + b"\x60" + struct.pack(">H", len(body) * 8) + triad(reliable) + triad(order) + b"\0" + body


def send_message(client, remote, sequence, body, part=1000):
    """Send one reliable-ordered message, split into fragments when larger than `part`.

    Each fragment takes its own datagram sequence and reliable index; all share
    the order index and split id. Returns the next free sequence number.
    """
    if len(body) <= part:
        client.sendto(frame(sequence, sequence, sequence, body), remote)
        return sequence + 1
    pieces = [body[i:i + part] for i in range(0, len(body), part)]
    order = sequence
    for index, piece in enumerate(pieces):
        client.sendto(b"\x84" + triad(sequence) + b"\x70" + struct.pack(">H", len(piece) * 8) + triad(sequence) +
                      triad(order) + b"\0" + struct.pack(">IHI", len(pieces), 7, index) + piece, remote)
        sequence += 1
    return sequence


def ack(sequence):
    return b"\xc0\0\x01\x01" + triad(sequence)


def incoming_frame(data):
    require(len(data) >= 14 and data[0] & 0xE0 == 0x80, "not a data datagram")
    require(data[4] == 0x60, "expected unsplit RELIABLE_ORDERED frame")
    bits = struct.unpack_from(">H", data, 5)[0]
    require(bits % 8 == 0 and len(data) == 14 + bits // 8, "frame size")
    return int.from_bytes(data[1:4], "little"), data[14:]


def varint(n):
    out = b""
    while n >= 0x80:
        out += bytes([n & 0x7F | 0x80])
        n >>= 7
    return out + bytes([n])


def read_varint(data, at):
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value, at


def login_batch(protocol, chain='{"chain":[]}', client="a.b.c"):
    """Python-built LoginPacket batch (zlib level 7), independent of the C++ encoder."""
    chain, client = chain.encode(), client.encode()
    inner = struct.pack("<I", len(chain)) + chain + struct.pack("<I", len(client)) + client
    packet = varint(0x01) + struct.pack(">i", protocol) + varint(len(inner)) + inner
    return b"\xfe" + zlib.compress(varint(len(packet)) + packet, 7)


def game_packets(message):
    require(message[0] == 0xFE, "game batch id")
    data = zlib.decompress(message[1:])
    packets, at = [], 0
    while at < len(data):
        length, at = read_varint(data, at)
        header, body_at = read_varint(data, at)
        packets.append((header, data[body_at:at + length]))
        at += length
    return packets


def connected_session(family, host, target_port, client_guid):
    """Minimal open/connected handshake; returns (socket, remote, next client sequence)."""
    client = socket.socket(family, socket.SOCK_DGRAM)
    client.settimeout(3)
    client.bind((host, 0))
    remote = (host, target_port)
    mtu = 1200
    client.sendto(b"\x05" + MAGIC + b"\x09" + bytes(mtu - 46), remote)
    require(client.recvfrom(2048)[0][0] == 0x06, "open reply one")
    client.sendto(b"\x07" + MAGIC + address(host, target_port) + struct.pack(">HQ", mtu, client_guid), remote)
    require(client.recvfrom(2048)[0][0] == 0x08, "open reply two")
    client.sendto(frame(0, 0, 0, b"\x09" + struct.pack(">QQB", client_guid, 1, 0)), remote)
    while True:
        data, _ = client.recvfrom(2048)
        if data[0] & 0xE0 == 0x80:
            seq, body = incoming_frame(data)
            client.sendto(ack(seq), remote)
            if body[0] == 0x10:
                break
    at = skip_address(body, 1) + 2
    for _ in range(20):
        at = skip_address(body, at)
    request_time, accepted_time = struct.unpack_from(">QQ", body, at)
    incoming = b"\x13" + address(host, target_port) + address("0.0.0.0", 0) * 20 + struct.pack(">QQ", request_time, accepted_time)
    client.sendto(frame(1, 1, 1, incoming), remote)
    return client, remote, 2


def until_disconnect(client, remote):
    """Collect reliable-ordered application messages until RakNet disconnect 0x15."""
    messages = []
    while True:
        data, _ = client.recvfrom(2048)
        if data[0] & 0xE0 != 0x80:
            continue
        seq, body = incoming_frame(data)
        client.sendto(ack(seq), remote)
        if body[0] == 0x10:
            continue  # late retransmission of the accepted reply
        if body == b"\x15":
            return messages
        if body not in messages:
            messages.append(body)


fixtures = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="bds-raknet-") as directory:
    root = Path(directory)
    has_ipv6 = ipv6_available()
    p4 = port(socket.AF_INET, "127.0.0.1")
    p6 = port(socket.AF_INET6, "::1") if has_ipv6 else 19133
    # online-mode=false: the throwaway fixture keys are not anchored in the
    # Mojang root, so only the self-signed path can succeed end to end.
    (root / "server.properties").write_text(f"level-name=Transport Test\nserver-port={p4}\nserver-portv6={p6}\n"
                                            "online-mode=false\n", encoding="utf-8")
    server = subprocess.Popen([sys.argv[1], "--root", str(root), "--tick-ms", "2"],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding="utf-8", bufsize=1)
    lines = queue.Queue()
    transcript = []

    def collect():
        for line in server.stdout:
            transcript.append(line.rstrip())
            lines.put(line)

    reader = threading.Thread(target=collect, daemon=True)
    reader.start()
    try:
        deadline = time.monotonic() + 8
        while True:
            require(time.monotonic() < deadline and server.poll() is None, f"server startup: {transcript}")
            try:
                if "console ready" in lines.get(timeout=0.1):
                    break
            except queue.Empty:
                continue
        families = [(socket.AF_INET, "127.0.0.1", p4)]
        if has_ipv6:
            families.append((socket.AF_INET6, "::1", p6))
        else:
            require(any("IPv6 not supported" in line for line in transcript), f"IPv6 fallback log: {transcript}")
        for family, host, target_port in families:
            with socket.socket(family, socket.SOCK_DGRAM) as client:
                client.settimeout(3)
                client.bind((host, 0))
                remote = (host, target_port)
                client_guid = 0x123456789ABCDEF0
                mtu = 1200
                # Reject incompatible RakNet version, independent of Bedrock protocol 282.
                client.sendto(b"\x05" + MAGIC + b"\x0a" + bytes(mtu - 46), remote)
                incompatible, _ = client.recvfrom(2048)
                require(incompatible[:18] == b"\x19\x09" + MAGIC and len(incompatible) == 26, "incompatible protocol reply")
                request1 = b"\x05" + MAGIC + b"\x09" + bytes(mtu - 46)
                client.sendto(request1, remote)
                reply1, _ = client.recvfrom(2048)
                require(len(reply1) == 28 and reply1[:17] == b"\x06" + MAGIC and reply1[25] == 0, "open reply one")
                guid = reply1[17:25]
                require(struct.unpack_from(">H", reply1, 26)[0] == mtu, "negotiated MTU")
                request2 = b"\x07" + MAGIC + address(host, target_port) + struct.pack(">HQ", mtu, client_guid)
                client.sendto(request2, remote)
                reply2, _ = client.recvfrom(2048)
                require(reply2[:25] == b"\x08" + MAGIC + guid, "open reply two GUID")
                end = skip_address(reply2, 25)
                require(reply2[25:end] == address(host, client.getsockname()[1]), "observed client address")
                require(reply2[end:] == struct.pack(">H", mtu) + b"\0", "open reply two MTU/security")
                # Duplicate request2 is idempotent, not a new connection.
                client.sendto(request2, remote)
                require(client.recvfrom(2048)[0] == reply2, "duplicate open request")
                request_time = 1234567
                connection_request = frame(0, 0, 0, b"\x09" + struct.pack(">QQB", client_guid, request_time, 0))
                client.sendto(connection_request, remote)

                def data_packet(expected):
                    while True:
                        data, _ = client.recvfrom(2048)
                        if data[0] & 0xE0 == 0x80:
                            offset = 14 if data[4] == 0x60 else 7
                            require(len(data) > offset, "truncated connected payload")
                            if data[offset] == expected:
                                return data
                            # More than one accepted retransmission may already be queued
                            # on a slow CI host when our ACK reaches the server.
                            require(data[offset] == 0x10 and expected != 0x10,
                                    "unexpected connected control packet")
                            client.sendto(ack(int.from_bytes(data[1:4], "little")), remote)

                accepted = data_packet(0x10)
                seq, body = incoming_frame(accepted)
                require(body[0] == 0x10, "connection accepted ID")
                at = skip_address(body, 1)
                require(body[1:at] == address(host, client.getsockname()[1]), "accepted client endpoint")
                require(body[at:at+2] == bytes(2), "accepted system index")
                at += 2
                for _ in range(20):
                    at = skip_address(body, at)
                require(at + 16 == len(body), "expected exactly 20 internal addresses")
                echoed, accepted_time = struct.unpack_from(">QQ", body, at)
                require(echoed == request_time, "request timestamp")
                # Simulate packet/ACK loss: server must retransmit the accepted message.
                retransmitted = data_packet(0x10)
                require(retransmitted == accepted, "reliable retransmission")
                client.sendto(ack(seq), remote)
                incoming = b"\x13" + address(host, target_port) + address("0.0.0.0", 0) * 20 + struct.pack(">QQ", request_time, accepted_time)
                client.sendto(frame(1, 1, 1, incoming), remote)
                # Connected ping cannot be answered before NewIncomingConnection.
                ping = b"\x00" + struct.pack(">Q", 7654321)
                client.sendto(frame(2, 2, 2, ping), remote)
                pong = data_packet(0x03)
                # Connected pong is intentionally sent UNRELIABLE.
                require(pong[4] == 0 and len(pong) == 24, "connected pong reliability/size")
                require(pong[7] == 3 and struct.unpack_from(">Q", pong, 8)[0] == 7654321, "connected pong timestamp")
                # Unauthenticated application data must not create a fake player.
                client.sendto(frame(3, 3, 3, b"\xfe\x00"), remote)
                disconnect = data_packet(0x15)
                disconnect_seq, disconnect_body = incoming_frame(disconnect)
                require(disconnect_body == b"\x15", "application data is rejected before login exists")
                client.sendto(ack(disconnect_seq), remote)
            # Real protocol-282 batches over UDP with logins signed by an
            # independent ES384 implementation (tests/protocol/login_fixtures.json).
            client_token = fixtures["client_token"]
            scenarios = [
                ("self-signed login", 282, fixtures["self_signed_chain"], [(0x05, "authenticated as Offline")]),
                ("chain via self-signed path", 282, fixtures["chain"], [(0x05, "authenticated as Steve")]),
                ("forged leaf", 282, fixtures["cases"]["untrusted_leaf_signer"], [(0x05, "disconnectionScreen.notAuthenticated")]),
                ("old client", 281, fixtures["chain"], [(0x02, 1), (0x05, "disconnectionScreen.outdatedClient")]),
                ("new client", 283, fixtures["chain"], [(0x02, 2), (0x05, "disconnectionScreen.outdatedServer")]),
            ]
            for index, (scenario, protocol, chain, expected) in enumerate(scenarios):
                client, remote, sequence = connected_session(family, host, target_port, 0x1000 + index)
                with client:
                    # Signed logins exceed the MTU, so this also exercises split reassembly.
                    send_message(client, remote, sequence, login_batch(protocol, chain, client_token))
                    messages = until_disconnect(client, remote)
                    require(len(messages) == 1, f"{scenario}: one batch before disconnect, got {messages}")
                    packets = game_packets(messages[0])
                    require(len(packets) == len(expected), f"{scenario}: packet count {packets}")
                    for (header, body), (packet_id, value) in zip(packets, expected):
                        require(header == packet_id, f"{scenario}: packet id {header}")
                        if packet_id == 0x02:
                            require(body == struct.pack(">i", value), f"{scenario}: PlayStatus")
                        else:
                            require(body[0] == 0, f"{scenario}: disconnect screen shown")
                            length, at = read_varint(body, 1)
                            require(value in body[at:at + length].decode() and at + length == len(body),
                                    f"{scenario}: disconnect message {body!r}")
        server.stdin.write("stop\n")
        server.stdin.flush()
        server.stdin.close()
        require(server.wait(timeout=8) == 0, f"stop failed: {transcript}")
        reader.join(timeout=2)
        require(any('login authenticated: "Offline" self-signed' in line for line in transcript),
                f"self-signed login not logged: {transcript}")
        require(any("login not authenticated" in line for line in transcript), f"forged login not logged: {transcript}")
        require(any("closing connection: login protocol 281 is not 282" in line for line in transcript),
                f"version refusal not logged: {transcript}")
        print(("Independent IPv4/IPv6" if has_ipv6 else "Independent IPv4-only (no host IPv6)") + " RakNet 9 handshake, retransmission, malformed-batch rejection and signed login outcomes passed")
    finally:
        if server.poll() is None:
            server.kill()
            server.wait(timeout=5)
        if not server.stdin.closed:
            server.stdin.close()
        reader.join(timeout=2)
        server.stdout.close()
