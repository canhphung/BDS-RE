#!/usr/bin/env python3
"""Independent protocol-282 encoder for level packets (StartGame and friends).

Written from the field list recorded in reverse/evidence/network_constants.json
("start_game"), not from the C++ implementation, and used to produce golden
bytes for tests/protocol/level_tests.cpp. Run and paste the printed constants
when the layout evidence changes.
"""
import struct


def uvarint(n):
    out = b""
    while n >= 0x80:
        out += bytes([n & 0x7F | 0x80])
        n >>= 7
    return out + bytes([n])


def varint(n):  # zigzag, 32-bit
    return uvarint(((n << 1) ^ (n >> 31)) & 0xFFFFFFFF)


def varint64(n):  # zigzag, 64-bit
    return uvarint(((n << 1) ^ (n >> 63)) & 0xFFFFFFFFFFFFFFFF)


def string(s):
    b = s.encode()
    return uvarint(len(b)) + b


def boolean(v):
    return b"\x01" if v else b"\x00"


def f32(v):
    return struct.pack("<f", v)


def block_pos(x, y, z):
    return varint(x) + uvarint(y) + varint(z)


settings = b"".join([
    varint(-123456),           # seed
    varint(0),                 # dimension
    varint(2),                 # generator: Flat
    varint(1),                 # game type: creative
    varint(3),                 # difficulty: hard
    block_pos(-17, 70, 300),   # default spawn
    boolean(True),             # achievements disabled
    varint(6000),              # time
    boolean(False), boolean(False),  # education world, education features
    f32(0.25), f32(0.5),       # rain, lightning
    boolean(True), boolean(True), boolean(False),  # multiplayer, LAN, XBL broadcast
    boolean(True), boolean(False),  # commands enabled, texture packs required
    uvarint(3) + string("pvp") + uvarint(1) + boolean(True)            # game rules: bool,
    + string("maxcommandchainlength") + uvarint(2) + varint(65535)      # zigzag int,
    + string("x") + uvarint(3) + f32(0.5),                              # float
    boolean(False), boolean(False), boolean(False),  # bonus chest, start with map, constant false
    varint(2),                 # default player permission: operator
    varint(0),                 # XBL broadcast mode
    struct.pack("<i", 6),      # server chunk tick range
    boolean(False), varint(0), boolean(False),  # platform broadcast, mode, XBL intent
    boolean(False), boolean(False), boolean(False),  # locked behaviour/resource, locked template
])
start_game = b"".join([
    varint64(-5), uvarint(7), varint(1),
    f32(-16.5), f32(71.62), f32(300.5), f32(10.0), f32(-90.0),
    settings,
    string("level-id"), string("Bedrock level"), string(""), boolean(False),
    struct.pack("<Q", 123456789012), varint(-2),
    uvarint(2), string("minecraft:air") + struct.pack("<h", 0), string("minecraft:stone") + struct.pack("<h", -1),
    string("corr"),
])
print("start_game", start_game.hex())
print("spawn_position", (varint(0) + block_pos(-17, 70, 300) + boolean(False)).hex())
print("set_time", varint(6000).hex())
print("set_difficulty", uvarint(3).hex())
print("commands_enabled", boolean(True).hex())
print("chunk_radius_updated", varint(12).hex())
print("request_chunk_radius_minus_one", varint(-1).hex())
