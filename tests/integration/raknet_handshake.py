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
import hashlib
import base64

try:  # The encrypted-session scenario needs an independent crypto stack.
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
    try:
        from cryptography.hazmat.decrepit.ciphers.modes import CFB8
    except ImportError:
        from cryptography.hazmat.primitives.ciphers.modes import CFB8
except ImportError:
    sys.exit("raknet_handshake needs the Python 'cryptography' package (pip install cryptography)")

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


class Sender:
    """Client-side reliable-ordered sender: datagram/reliable indices advance per
    fragment, the order index once per message."""

    def __init__(self, client, remote, sequence):
        self.client, self.remote, self.sequence, self.order = client, remote, sequence, sequence

    def send(self, body, part=1000):
        pieces = [body[i:i + part] for i in range(0, len(body), part)] or [b""]
        for index, piece in enumerate(pieces):
            if len(pieces) == 1:
                header = b"\x60" + struct.pack(">H", len(piece) * 8) + triad(self.sequence) + triad(self.order) + b"\0"
            else:
                header = (b"\x70" + struct.pack(">H", len(piece) * 8) + triad(self.sequence) + triad(self.order) + b"\0" +
                          struct.pack(">IHI", len(pieces), self.order & 0xFFFF, index))
            self.client.sendto(b"\x84" + triad(self.sequence) + header + piece, self.remote)
            self.sequence += 1
        self.order += 1


class Session:
    """Reconstructed EncryptedNetworkPeer, written against Python cryptography."""

    def __init__(self, key):
        self.key, self.sent, self.received = key, 0, 0
        self.encryptor = Cipher(algorithms.AES(key), CFB8(key[:16])).encryptor()
        self.decryptor = Cipher(algorithms.AES(key), CFB8(key[:16])).decryptor()

    def checksum(self, counter, payload):
        return hashlib.sha256(counter.to_bytes(8, "little") + payload + self.key).digest()[:8]

    def seal(self, payload):
        sealed = self.encryptor.update(payload + self.checksum(self.sent, payload))
        self.sent += 1
        return sealed

    def open(self, data):
        plain = self.decryptor.update(data)
        payload, checksum = plain[:-8], plain[-8:]
        require(checksum == self.checksum(self.received, payload), "server packet checksum")
        self.received += 1
        return payload


def b64url(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def compressed_packets(packets):
    data = b"".join(varint(len(varint(header) + body)) + varint(header) + body for header, body in packets)
    return zlib.compress(data, 7)


def packets_of(payload):
    data = zlib.decompress(payload)
    packets, at = [], 0
    while at < len(data):
        length, at = read_varint(data, at)
        header, body_at = read_varint(data, at)
        packets.append((header, data[body_at:at + length]))
        at += length
    return packets


class Reader:
    """Independent protocol-282 reader for the level packets checked here."""

    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, n):
        require(self.at + n <= len(self.data), "truncated level packet")
        out = self.data[self.at:self.at + n]
        self.at += n
        return out

    def uvarint(self):
        value = shift = 0
        while True:
            byte = self.take(1)[0]
            value |= (byte & 0x7F) << shift
            shift += 7
            if byte < 0x80:
                return value

    def varint(self):
        n = self.uvarint()
        return (n >> 1) ^ -(n & 1)

    def string(self):
        return self.take(self.uvarint()).decode()

    def boolean(self):
        return self.take(1)[0] == 1

    def f32(self):
        return struct.unpack("<f", self.take(4))[0]


def parse_start_game(body):
    """Field order from StartGamePacket::write / serialize<LevelSettings>::write."""
    r = Reader(body)
    game = {"unique_id": r.varint(), "runtime_id": r.uvarint(), "game_type": r.varint(),
            "position": (r.f32(), r.f32(), r.f32()), "rotation": (r.f32(), r.f32())}
    game["seed"], game["dimension"], game["generator"] = r.varint(), r.varint(), r.varint()
    game["world_game_type"], game["difficulty"] = r.varint(), r.varint()
    game["spawn"] = (r.varint(), r.uvarint(), r.varint())
    r.boolean(); game["time"] = r.varint(); r.boolean(); r.boolean(); r.f32(); r.f32()
    r.boolean(); r.boolean(); r.boolean()
    game["commands"], game["texture_packs_required"] = r.boolean(), r.boolean()
    require(r.uvarint() == 0, "no game rules expected")
    r.boolean(); r.boolean(); r.boolean()
    game["permission"], _ = r.varint(), r.varint()
    game["tick_range"] = struct.unpack("<i", r.take(4))[0]
    r.boolean(); r.varint(); r.boolean(); r.boolean(); r.boolean(); r.boolean()
    game["level_id"], game["level_name"], _ = r.string(), r.string(), r.string()
    r.boolean()
    game["current_tick"] = struct.unpack("<Q", r.take(8))[0]
    r.varint()
    game["palette"] = [(r.string(), struct.unpack("<h", r.take(2))[0]) for _ in range(r.uvarint())]
    r.string()
    require(r.at == len(body), "StartGame fully consumed")
    return game


def next_application(client, remote, seen):
    """Next new reliable-ordered application message from the server."""
    while True:
        data, _ = client.recvfrom(2048)
        if data[0] & 0xE0 != 0x80:
            continue
        seq, body = incoming_frame(data)
        client.sendto(ack(seq), remote)
        if body[0] == 0x10 or body in seen:
            continue  # late retransmissions
        seen.append(body)
        return body


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
            client_key = ec.derive_private_key(int(fixtures["client_private_key"], 16), ec.SECP384R1())
            scenarios = [
                ("self-signed login", 282, fixtures["self_signed_chain"], "Offline"),
                ("chain via self-signed path", 282, fixtures["chain"], "Steve"),
                ("forged leaf", 282, fixtures["cases"]["untrusted_leaf_signer"], [(0x05, "disconnectionScreen.notAuthenticated")]),
                ("old client", 281, fixtures["chain"], [(0x02, 1), (0x05, "disconnectionScreen.outdatedClient")]),
                ("new client", 283, fixtures["chain"], [(0x02, 2), (0x05, "disconnectionScreen.outdatedServer")]),
            ]
            for index, (scenario, protocol, chain, expected) in enumerate(scenarios):
                client, remote, sequence = connected_session(family, host, target_port, 0x1000 + index)
                with client:
                    sender, seen = Sender(client, remote, sequence), []
                    # Signed logins exceed the MTU, so this also exercises split reassembly.
                    sender.send(login_batch(protocol, chain, client_token))
                    if isinstance(expected, str):
                        # ServerToClientHandshake arrives unencrypted.
                        packets = game_packets(next_application(client, remote, seen))
                        require(len(packets) == 1 and packets[0][0] == 0x03, f"{scenario}: ServerToClientHandshake")
                        length, at = read_varint(packets[0][1], 0)
                        token = packets[0][1][at:at + length].decode()
                        head, payload, signature = token.split(".")
                        header, claims = json.loads(b64url(head)), json.loads(b64url(payload))
                        server_key = serialization.load_der_public_key(base64.b64decode(header["x5u"]))
                        raw = b64url(signature)
                        server_key.verify(encode_dss_signature(int.from_bytes(raw[:48], "big"), int.from_bytes(raw[48:], "big")),
                                          f"{head}.{payload}".encode(), ec.ECDSA(hashes.SHA384()))
                        require(header["alg"] == "ES384", f"{scenario}: token algorithm")
                        salt = base64.b64decode(claims["salt"])
                        require(len(salt) == 16, f"{scenario}: salt length")
                        session = Session(hashlib.sha256(salt + client_key.exchange(ec.ECDH(), server_key)).digest())
                        sender.send(b"\xfe" + session.seal(compressed_packets([(0x04, b"")])))
                        def receive_packets():
                            message = next_application(client, remote, seen)
                            require(message[0] == 0xFE, f"{scenario}: encrypted batch id")
                            return packets_of(session.open(message[1:]))

                        packets = receive_packets()
                        require(packets == [(0x02, struct.pack(">i", 0)), (0x06, bytes(5))],
                                f"{scenario}: PlayStatus LoginSuccess + empty ResourcePacksInfo {packets}")
                        # ResourcePackClientResponse: HaveAllPacks (3), then Completed (4).
                        sender.send(b"\xfe" + session.seal(compressed_packets([(0x08, b"\x03\x00\x00")])))
                        require(receive_packets() == [(0x07, bytes(3))], f"{scenario}: empty ResourcePackStack")
                        sender.send(b"\xfe" + session.seal(compressed_packets([(0x08, b"\x04\x00\x00")])))
                        packets = receive_packets()
                        require([header for header, _ in packets] == [0x0B, 0x2B, 0x0A, 0x3C, 0x3B],
                                f"{scenario}: StartGame, SetSpawnPosition, SetTime, SetDifficulty, SetCommandsEnabled {packets}")
                        game = parse_start_game(packets[0][1])
                        require(game["runtime_id"] >= 1 and game["unique_id"] == game["runtime_id"], f"{scenario}: entity ids {game}")
                        require(game["level_name"] == "Transport Test" and game["generator"] == 1 and game["dimension"] == 0,
                                f"{scenario}: level settings {game}")
                        require(game["spawn"] == (0, 64, 0) and abs(game["position"][1] - 65.62) < 1e-3, f"{scenario}: spawn {game}")
                        require(game["tick_range"] == 4 and game["permission"] == 1 and game["palette"][0] == ("minecraft:air", 0),
                                f"{scenario}: defaults {game}")
                        # A movement packet before spawning is ignored; the radius request is answered.
                        sender.send(b"\xfe" + session.seal(compressed_packets([(0x13, b"\x00")])))
                        sender.send(b"\xfe" + session.seal(compressed_packets([(0x45, varint(32 << 1))])))
                        packets = receive_packets()
                        require(packets[0] == (0x46, varint(10 << 1)), f"{scenario}: ChunkRadiusUpdated {packets}")
                        require(len(packets) == 2 and packets[1][0] == 0x05 and expected.encode() in packets[1][1] and
                                b"Chunk streaming" in packets[1][1], f"{scenario}: encrypted disconnect {packets}")
                        require(next_application(client, remote, seen) == b"\x15", f"{scenario}: RakNet disconnect")
                        continue
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
        # Whitelist admission, toggled at runtime through the console: the
        # self-signed player has no XUID and no listed UUID, so it is refused in
        # plaintext before any key exchange.
        server.stdin.write("whitelist on\n")
        server.stdin.flush()
        deadline = time.monotonic() + 5
        while not any("whitelist on (runtime only)" in line for line in transcript):
            require(time.monotonic() < deadline, f"whitelist command: {transcript}")
            time.sleep(0.01)
        client, remote, sequence = connected_session(socket.AF_INET, "127.0.0.1", p4, 0x2000)
        with client:
            Sender(client, remote, sequence).send(login_batch(282, fixtures["self_signed_chain"], fixtures["client_token"]))
            messages = until_disconnect(client, remote)
            packets = game_packets(messages[0])
            require(len(messages) == 1 and len(packets) == 1 and packets[0][0] == 0x05 and
                    b"disconnectionScreen.notAllowed" in packets[0][1], f"whitelist refusal {packets}")
        server.stdin.write("stop\n")
        server.stdin.flush()
        server.stdin.close()
        require(server.wait(timeout=8) == 0, f"stop failed: {transcript}")
        reader.join(timeout=2)
        require(any('login authenticated: "Offline" self-signed' in line for line in transcript),
                f"self-signed login not logged: {transcript}")
        require(any("Player connected: Steve" in line for line in transcript), f"handshake completion not logged: {transcript}")
        require(any("login refused: Offline (disconnectionScreen.notAllowed)" in line for line in transcript),
                f"whitelist refusal not logged: {transcript}")
        require(any("login not authenticated" in line for line in transcript), f"forged login not logged: {transcript}")
        require(any("closing connection: login protocol 281 is not 282" in line for line in transcript),
                f"version refusal not logged: {transcript}")
        print(("Independent IPv4/IPv6" if has_ipv6 else "Independent IPv4-only (no host IPv6)") + " RakNet 9 handshake, retransmission, malformed-batch rejection, signed logins, the encrypted handshake, pack exchange, StartGame, chunk radius and whitelist refusal passed")
    finally:
        if server.poll() is None:
            server.kill()
            server.wait(timeout=5)
        if not server.stdin.closed:
            server.stdin.close()
        reader.join(timeout=2)
        server.stdout.close()
