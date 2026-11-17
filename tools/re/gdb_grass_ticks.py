"""Run the original GrassBlock::tick, preserving its dependency calls and RNG.

Sourced by capture_level_random.py --grass-oracle at its final Level::tick stop.
Requires the scene setup commands documented with the resulting fixture. Uses a
separate original Random object per seed; no game algorithm/results are patched.
"""
import json
import os
import struct
import gdb

# `state` is the live probe state from gdb_level_random_sequence.py.
probe = state  # noqa: F821
current = None


def r(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xffffffffffffffff


def mem(address, count):
    return bytes(gdb.selected_inferior().read_memory(address, count))


def q(address):
    return struct.unpack("<Q", mem(address, 8))[0]


air_legacy, dirt_legacy, path_legacy = q(0x34a9378), q(0x34a7770), q(0x34a7db0)
grass_block, dirt_block, air_block = q(0x34a85a0), q(0x34a85a8), q(0x34a84f0)
grass_legacy = q(grass_block + 0x10)


def block_info(block):
    legacy = q(block + 0x10)
    kind = ("air" if legacy == air_legacy else "dirt" if legacy == dirt_legacy
            else "grass_path" if legacy == path_legacy else "grass" if legacy == grass_legacy else "other")
    return {"kind": kind, "solid": bool(mem(legacy + 0xaa, 1)[0] & 1)}


class Query(gdb.Breakpoint):
    def __init__(self, address, kind, position_offset, value_offset=0):
        super().__init__("*" + hex(address), internal=True)
        self.kind, self.position_offset, self.value_offset = kind, position_offset, value_offset

    def stop(self):
        if current is None:
            return False
        frame = r("rbp")
        position = list(struct.unpack("<iii", mem(frame + self.position_offset, 12)))
        if self.kind == "brightness":
            value = mem(frame + self.value_offset, 1)[0]
        elif self.kind == "block":
            value = block_info(r("rax"))
        elif self.kind == "material":
            value = {"liquid": bool(mem(r("rax") + 8, 1)[0] & 1),
                     "translucency": struct.unpack("<f", mem(r("rax") + 12, 4))[0]}
        else:
            value = struct.unpack("<i", struct.pack("<I", r("eax") & 0xffffffff))[0]
        current["calls"].append({"kind": self.kind, "position": position, "value": value})
        return False


class SetBlock(gdb.Breakpoint):
    def stop(self):
        if current is None or q(r("rsp")) not in (0x1e717b8, 0x1e71ab5):
            return False
        current["calls"].append({"kind": "set_block",
                                 "position": list(struct.unpack("<iii", mem(r("rsi"), 12))),
                                 "value": block_info(r("rdx"))["kind"], "flags": r("ecx")})
        return False


Query(0x1e71706, "brightness", -0x30, -0x38)
Query(0x1e7172e, "block", -0x30)
Query(0x1e71762, "material", -0x30)
Query(0x1e71895, "block", -0x58)
Query(0x1e7192d, "block", -0x58)
Query(0x1e71977, "variant", -0x58)
Query(0x1e719b2, "block", -0x98)
Query(0x1e719e7, "material", -0xb0)
Query(0x1e71a35, "brightness", -0xc0, -0xc8)
SetBlock("*0x1c59b20", internal=True)

player_vector = q(probe["level"] + 0x50)
region = q(q(player_vector) + 0xc98)
buffer = int(gdb.parse_and_eval("(char*)malloc(8192)"))
random_address = buffer
position_address = buffer + 4096
probe["oracle_random"] = random_address


def position(at):
    gdb.selected_inferior().write_memory(position_address, struct.pack("<iii", *at))


def set_block(at, block):
    position(at)
    gdb.parse_and_eval(f"((unsigned char(*)(void*,void*,void*,int,void*))0x1c59b20)"
                      f"({region},{position_address},{block},3,0)")


position((8, 3, 9))
coarse_dirt = int(gdb.parse_and_eval(
    f"((void*(*)(void*,void*))0x1c58ad0)({region},{position_address})"))
cases = []
scenes = [("bright", (0, 3, 0), 3, dirt_block), ("covered", (8, 3, 0), 3, dirt_block),
          ("descent", (0, 7, 8), 3, dirt_block), ("coarse_dirt", (8, 3, 8), 3, coarse_dirt),
          ("grass_path", (12, 3, 0), 3, dirt_block)]
for scene, at, ground, soil in scenes:
    for seed in range(16):
        for dx in range(-1, 2):
            for dz in range(-1, 2):
                set_block((at[0] + dx, ground, at[2] + dz), soil)
        set_block(at, grass_block)
        gdb.parse_and_eval(f"((void(*)(void*,unsigned))0x187e380)({random_address},{seed})")
        position(at)
        current = {"scene": scene, "position": list(at), "seed": seed, "calls": [], "words": []}
        probe["oracle_words"] = current["words"]
        gdb.parse_and_eval(f"((void(*)(void*,void*,void*,void*))0x1e716a0)"
                          f"({grass_legacy},{region},{position_address},{random_address})")
        cases.append(current)
        current = None
        probe["oracle_words"] = None

fixture = {"binary_sha256": "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
           "generator": "tools/re/gdb_grass_ticks.py: original GrassBlock::tick calls in a paused flat world",
           "min_brightness": mem(0x3498560, 1)[0], "max_brightness": mem(0x3497680, 1)[0],
           "scope": "Server-side GrassBlock::tick dependency calls, mutations and raw RNG words; original live lighting/material values are recorded, not replaced.",
           "cases": cases}
with open(os.environ["GRASS_ORACLE_OUT"], "w") as output:
    json.dump(fixture, output, indent=1)
    output.write("\n")
print("Original GrassBlock::tick oracle:", len(cases), "cases")
