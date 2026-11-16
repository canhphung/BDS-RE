#!/usr/bin/env python3
"""Minimal protocol-282 Bedrock client for reference captures and differential tests.

Independent of the C++ server: RakNet 9 (with ACKs, split reassembly and
connected ping replies), zlib batches, a self-signed ES384 login, the
encryption handshake (ECDH P-384, AES-256-CFB8, SHA-256 checksums) and the
resource pack exchange. Requires the Python `cryptography` package.

It works against the original BDS 1.6.0.15 (online-mode=false) and against
BDS-RE, so the same script can capture reference traffic and compare.
"""
import base64
import hashlib
import json
import os
import socket
import struct
import time
import uuid
import zlib

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms

try:
    from cryptography.hazmat.decrepit.ciphers.modes import CFB8
except ImportError:  # older cryptography releases
    from cryptography.hazmat.primitives.ciphers.modes import CFB8

MAGIC = bytes.fromhex("00ffff00fefefefefdfdfdfd12345678")
PROTOCOL = 282


class ProtocolFailure(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ProtocolFailure(message)


# --- encodings -------------------------------------------------------------

def uvarint(n):
    out = b""
    while n >= 0x80:
        out += bytes([n & 0x7F | 0x80])
        n >>= 7
    return out + bytes([n])


def read_uvarint(data, at):
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value, at


def triad(n):
    return (n & 0xFFFFFF).to_bytes(3, "little")


def address(host, port):
    return b"\x04" + bytes(b ^ 0xFF for b in socket.inet_aton(host)) + struct.pack(">H", port)


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def b64url_decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


# --- login -----------------------------------------------------------------

def public_key_text(key):
    der = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return base64.b64encode(der).decode()


def sign_jwt(key, payload):
    header = {"alg": "ES384", "x5u": public_key_text(key)}
    signing_input = b64url(json.dumps(header, separators=(",", ":")).encode()) + "." + \
        b64url(json.dumps(payload, separators=(",", ":")).encode())
    r, s = decode_dss_signature(key.sign(signing_input.encode(), ec.ECDSA(hashes.SHA384())))
    return signing_input + "." + b64url(r.to_bytes(48, "big") + s.to_bytes(48, "big"))


def self_signed_login(key, name, server_address):
    """Self-signed chain plus client data with the fields the original reads
    (ConnectionRequest getters) and a 64x64 RGBA skin (16384 bytes)."""
    now = int(time.time())
    identity = str(uuid.uuid4())
    chain_token = sign_jwt(key, {"extraData": {"displayName": name, "identity": identity, "XUID": ""},
                                 "identityPublicKey": public_key_text(key), "nbf": now - 60, "exp": now + 86400})
    client_data = {
        "CapeData": "", "ClientRandomId": int.from_bytes(os.urandom(7), "big"), "CurrentInputMode": 1,
        "DefaultInputMode": 1, "DeviceId": str(uuid.uuid4()), "DeviceModel": "BDS-RE reference client",
        "DeviceOS": 7, "GameVersion": "1.6.0", "GuiScale": 0, "LanguageCode": "en_US",
        "PlatformOfflineId": "", "PlatformOnlineId": "", "PremiumSkin": False, "SelfSignedId": str(uuid.uuid4()),
        "ServerAddress": server_address, "SkinData": base64.b64encode(bytes([0x7F, 0x7F, 0x7F, 0xFF]) * 4096).decode(),
        "SkinGeometry": "", "SkinGeometryName": "geometry.humanoid.custom", "SkinId": "Standard_Custom",
        "ThirdPartyName": name, "UIProfile": 0,
    }
    return json.dumps({"chain": [chain_token]}), sign_jwt(key, client_data), identity


def login_body(protocol, chain, client_token):
    chain, client_token = chain.encode(), client_token.encode()
    inner = struct.pack("<I", len(chain)) + chain + struct.pack("<I", len(client_token)) + client_token
    return struct.pack(">i", protocol) + uvarint(len(inner)) + inner


# --- batches and encryption -----------------------------------------------

def compress_packets(packets):
    data = b"".join(uvarint(len(uvarint(pid) + body)) + uvarint(pid) + body for pid, body in packets)
    return zlib.compress(data, 7)


def decompress_packets(payload):
    data = zlib.decompress(payload)
    packets, at = [], 0
    while at < len(data):
        length, at = read_uvarint(data, at)
        header, body_at = read_uvarint(data, at)
        packets.append((header & 0x3FF, data[body_at:at + length]))
        at += length
    return packets


class Session:
    """EncryptedNetworkPeer as reconstructed: AES-256-CFB8, IV = key[:16],
    checksum = SHA-256(le64 counter + payload + key)[:8]."""

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
        require(checksum == self.checksum(self.received, payload), "server packet checksum mismatch")
        self.received += 1
        return payload


# --- RakNet ----------------------------------------------------------------

class RakNetClient:
    def __init__(self, host, port, mtu=1400, timeout=5.0):
        self.remote = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1" if host == "127.0.0.1" else "0.0.0.0", 0))
        self.sock.settimeout(timeout)
        self.mtu, self.guid = mtu, int.from_bytes(os.urandom(8), "big") & 0x7FFFFFFFFFFFFFFF
        self.sequence = self.reliable = self.order = 0
        self.split_id = 0
        self.splits = {}
        self.inbox = []
        self.seen_reliable = set()
        self.expected_order = {}
        self.waiting_order = {}

    def close(self):
        try:
            self.send(b"\x15")
        except OSError:
            pass
        self.sock.close()

    def ping(self):
        self.sock.sendto(b"\x01" + struct.pack(">Q", int(time.time() * 1000)) + MAGIC + struct.pack(">Q", self.guid), self.remote)
        data, _ = self.sock.recvfrom(4096)
        require(data[0] == 0x1C, "unconnected pong")
        length = struct.unpack_from(">H", data, 33)[0]
        return data[35:35 + length].decode()

    def connect(self):
        self.sock.sendto(b"\x05" + MAGIC + bytes([9]) + bytes(self.mtu - 46), self.remote)
        reply, _ = self.sock.recvfrom(4096)
        require(reply[0] == 0x06, f"open connection reply 1: {reply[:1].hex()}")
        self.mtu = struct.unpack_from(">H", reply, 26)[0]
        self.sock.sendto(b"\x07" + MAGIC + address(*self.remote) + struct.pack(">HQ", self.mtu, self.guid), self.remote)
        reply, _ = self.sock.recvfrom(4096)
        require(reply[0] == 0x08, f"open connection reply 2: {reply[:1].hex()}")
        self.send(b"\x09" + struct.pack(">QQB", self.guid, int(time.time() * 1000), 0))
        accepted = self.receive_message(lambda m: m[0] == 0x10)
        at = 1 + 7 + 2
        for _ in range(20):
            at += 7 if accepted[at] == 4 else 29
        request_time, accepted_time = struct.unpack_from(">QQ", accepted, at)
        incoming = b"\x13" + address(*self.remote) + address("0.0.0.0", 0) * 20 + struct.pack(">QQ", request_time, accepted_time)
        self.send(incoming)

    def send(self, body):
        """Reliable-ordered message, split when larger than the frame budget."""
        part = self.mtu - 60
        pieces = [body[i:i + part] for i in range(0, len(body), part)] or [b""]
        split_id = self.split_id
        self.split_id = (self.split_id + 1) & 0xFFFF
        for index, piece in enumerate(pieces):
            if len(pieces) == 1:
                frame = b"\x60" + struct.pack(">H", len(piece) * 8) + triad(self.reliable) + triad(self.order) + b"\0"
            else:
                frame = (b"\x70" + struct.pack(">H", len(piece) * 8) + triad(self.reliable) + triad(self.order) + b"\0" +
                         struct.pack(">IHI", len(pieces), split_id, index))
            self.sock.sendto(b"\x84" + triad(self.sequence) + frame + piece, self.remote)
            self.sequence += 1
            self.reliable += 1
        self.order += 1

    def _handle_datagram(self, data):
        header = data[0]
        if header & 0x80 == 0:
            return
        if header & 0x40 or header & 0x20:  # ACK / NACK: no retransmission state kept here
            return
        sequence = int.from_bytes(data[1:4], "little")
        self.sock.sendto(b"\xc0\x00\x01\x01" + triad(sequence), self.remote)
        at = 4
        while at < len(data):
            flags = data[at]
            reliability, split = flags >> 5, bool(flags & 0x10)
            bits = struct.unpack_from(">H", data, at + 1)[0]
            at += 3
            reliable_index = None
            if reliability in (2, 3, 4, 6, 7):
                reliable_index = int.from_bytes(data[at:at + 3], "little")
                at += 3
            if reliability in (1, 4):
                at += 3
            order_info = None
            if reliability in (1, 3, 4, 7):
                order_info = (int.from_bytes(data[at:at + 3], "little"), data[at + 3])
                at += 4
            split_info = None
            if split:
                split_info = struct.unpack_from(">IHI", data, at)
                at += 10
            length = (bits + 7) // 8
            body = data[at:at + length]
            at += length
            if reliable_index is not None:
                if reliable_index in self.seen_reliable:
                    continue
                self.seen_reliable.add(reliable_index)
            if split_info:
                count, sid, index = split_info
                parts = self.splits.setdefault(sid, {})
                parts[index] = body
                if len(parts) < count:
                    continue
                body = b"".join(parts[i] for i in range(count))
                del self.splits[sid]
            if reliability in (3, 7):
                order, channel = order_info
                expected = self.expected_order.get(channel, 0)
                distance = (order - expected) & 0xffffff
                if distance >= 0x800000:
                    continue
                require(distance < 1024, "ordered receive window exceeded")
                waiting = self.waiting_order.setdefault(channel, {})
                waiting[order] = body
                while expected in waiting:
                    self._dispatch(waiting.pop(expected))
                    expected = (expected + 1) & 0xffffff
                self.expected_order[channel] = expected
            else:
                self._dispatch(body)

    def _dispatch(self, body):
        if body[:1] == b"\x00":  # connected ping -> pong
            self.send(b"\x03" + body[1:9] + struct.pack(">Q", int(time.time() * 1000)))
        else:
            self.inbox.append(body)

    def receive_message(self, predicate=lambda m: True, timeout=10.0):
        deadline = time.monotonic() + timeout
        while True:
            for i, message in enumerate(self.inbox):
                if predicate(message):
                    return self.inbox.pop(i)
            remaining = deadline - time.monotonic()
            require(remaining > 0, "timed out waiting for a RakNet message")
            self.sock.settimeout(remaining)
            try:
                data, _ = self.sock.recvfrom(65535)
            except socket.timeout:
                continue
            self._handle_datagram(data)


class BedrockClient:
    """Drives login through StartGame and records every game packet."""

    def __init__(self, host, port, name="Reference", key=None):
        self.raknet = RakNetClient(host, port)
        self.name, self.session, self.log = name, None, []
        self.key = key or ec.generate_private_key(ec.SECP384R1())

    def send_packets(self, packets):
        payload = compress_packets(packets)
        if self.session:
            payload = self.session.seal(payload)
        self.raknet.send(b"\xfe" + payload)

    def receive_packets(self, timeout=10.0):
        message = self.raknet.receive_message(lambda m: m[0] in (0xFE, 0x15), timeout)
        if message[0] == 0x15:
            raise ProtocolFailure("server closed the RakNet connection")
        payload = message[1:]
        if self.session:
            payload = self.session.open(payload)
        packets = decompress_packets(payload)
        for pid, body in packets:
            self.log.append({"id": pid, "encrypted": self.session is not None, "body": body.hex()})
        return packets

    def expect(self, wanted, timeout=10.0):
        """Collect packets until one with id `wanted` arrives; return all collected."""
        collected = []
        while True:
            packets = self.receive_packets(timeout)
            collected.extend(packets)
            if any(pid == wanted for pid, _ in packets):
                return collected
            for pid, body in packets:
                if pid == 0x05:
                    raise ProtocolFailure(f"disconnected: {body!r}")

    def join(self, chain=None, client_token=None):
        """Login through StartGame. `chain`/`client_token` override the
        generated self-signed login; `self.key` must then be the leaf key."""
        host, port = self.raknet.remote
        self.raknet.connect()
        if chain is None:
            chain, client_token, _ = self_signed_login(self.key, self.name, f"{host}:{port}")
        self.send_packets([(0x01, login_body(PROTOCOL, chain, client_token))])
        packets = self.expect(0x03)
        body = dict(packets)[0x03]
        length, at = read_uvarint(body, 0)
        token = body[at:at + length].decode()
        head, payload, signature = token.split(".")
        header, claims = json.loads(b64url_decode(head)), json.loads(b64url_decode(payload))
        server_key = serialization.load_der_public_key(base64.b64decode(header["x5u"]))
        raw = b64url_decode(signature)
        server_key.verify(encode_dss_signature(int.from_bytes(raw[:48], "big"), int.from_bytes(raw[48:], "big")),
                          f"{head}.{payload}".encode(), ec.ECDSA(hashes.SHA384()))
        salt = base64.b64decode(claims["salt"])
        self.session = Session(hashlib.sha256(salt + self.key.exchange(ec.ECDH(), server_key)).digest())
        self.handshake = {"header": header, "claims": claims}
        self.send_packets([(0x04, b"")])
        self.expect(0x06)  # ResourcePacksInfo after PlayStatus
        self.send_packets([(0x08, b"\x03\x00\x00")])
        self.expect(0x07)  # ResourcePackStack
        self.send_packets([(0x08, b"\x04\x00\x00")])
        return self.expect(0x0B, timeout=30.0)  # StartGame

    def drain(self, seconds):
        """Record whatever the server sends for `seconds`."""
        end = time.monotonic() + seconds
        collected = []
        while time.monotonic() < end:
            try:
                collected.extend(self.receive_packets(timeout=max(0.05, end - time.monotonic())))
            except ProtocolFailure as error:
                if "timed out" in str(error):
                    break
                raise
        return collected
