#!/usr/bin/env python3
"""Parse a reference capture with the reconstructed protocol-282 layouts.

    python3 tools/reference/analyze_capture.py capture.json [palette_out.json]

Parsing StartGame to exactly its last byte is itself a differential check of
the reconstructed StartGame/LevelSettings layout. The block palette is
written out for the server to load.
"""
import json
import struct
import sys


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, n):
        if self.at + n > len(self.data):
            raise ValueError(f"truncated at {self.at} (+{n}) of {len(self.data)}")
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

    def varint64(self):
        return self.varint()

    def string(self):
        return self.take(self.uvarint()).decode()

    def boolean(self):
        value = self.take(1)[0]
        if value > 1:
            raise ValueError(f"bad bool {value} at {self.at - 1}")
        return value == 1

    def f32(self):
        return struct.unpack("<f", self.take(4))[0]

    def u64(self):
        return struct.unpack("<Q", self.take(8))[0]

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]

    def i16(self):
        return struct.unpack("<h", self.take(2))[0]


def game_rules(r):
    rules = []
    for _ in range(r.uvarint()):
        name, kind = r.string(), r.uvarint()
        value = r.boolean() if kind == 1 else r.varint() if kind == 2 else r.f32() if kind == 3 else None
        if value is None:
            raise ValueError(f"unknown game rule type {kind}")
        rules.append((name, kind, value))
    return rules


def parse_start_game(body):
    r = Reader(body)
    g = {"unique_id": r.varint64(), "runtime_id": r.uvarint(), "player_game_type": r.varint(),
         "position": [r.f32(), r.f32(), r.f32()], "rotation": [r.f32(), r.f32()]}
    s = g["settings"] = {}
    for key in ("seed", "dimension", "generator", "game_type", "difficulty"):
        s[key] = r.varint()
    s["spawn"] = [r.varint(), r.uvarint(), r.varint()]
    s["achievements_disabled"] = r.boolean()
    s["time"] = r.varint()
    s["education_world"], s["education_features"] = r.boolean(), r.boolean()
    s["rain_level"], s["lightning_level"] = r.f32(), r.f32()
    for key in ("multiplayer", "lan_broadcast", "xbl_broadcast", "commands_enabled", "texture_packs_required"):
        s[key] = r.boolean()
    s["game_rules"] = game_rules(r)
    s["bonus_chest"], s["start_with_map"], s["constant_false"] = r.boolean(), r.boolean(), r.boolean()
    s["default_player_permission"], s["xbl_broadcast_mode"] = r.varint(), r.varint()
    s["server_chunk_tick_range"] = r.i32()
    s["platform_broadcast"], s["platform_broadcast_mode"] = r.boolean(), r.varint()
    for key in ("xbl_broadcast_intent", "locked_behavior_pack", "locked_resource_pack", "from_locked_template"):
        s[key] = r.boolean()
    g["level_id"], g["level_name"], g["premium_template"] = r.string(), r.string(), r.string()
    g["trial"], g["current_tick"], g["enchantment_seed"] = r.boolean(), r.u64(), r.varint()
    g["palette"] = [[r.string(), r.i16()] for _ in range(r.uvarint())]
    g["multiplayer_correlation_id"] = r.string()
    if r.at != len(body):
        raise ValueError(f"StartGame has {len(body) - r.at} unparsed bytes")
    return g


def main():
    capture = json.load(open(sys.argv[1]))
    start = next(p for p in capture["packets"] if p["id"] == 0x0B)
    game = parse_start_game(bytes.fromhex(start["body"]))
    summary = {k: v for k, v in game.items() if k != "palette"}
    summary["palette_entries"] = len(game["palette"])
    summary["palette_head"] = game["palette"][:8]
    print(json.dumps(summary, indent=1))
    if len(sys.argv) > 2:
        with open(sys.argv[2], "w") as out:
            json.dump({"source": "StartGamePacket from the original BDS 1.6.0.15 (protocol 282), captured with "
                                 "tools/reference/capture_reference.py; order = runtime id",
                       "entries": game["palette"]}, out, indent=0)
            out.write("\n")


if __name__ == "__main__":
    main()
