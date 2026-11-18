"""Dump the original's generated chunk volumes (Linux 1.6.0.15, overworld).

Sourced through capture_level_random.py --generator original --gdb-script.
OverworldGenerator::loadChunk 0x1b38610 builds a BlockVolume over
ThreadData +0x1800 (32768 `Block const*`, width 16, height 128, depth 16,
index (x * 16 + z) * 128 + y, BlockVolume::index 0x1b30380). Three
non-stopping breakpoints read it:

- 0x1b38788: after prepareHeights (vtable +0xa0) -> stage "heights"
- 0x1b387a1: after buildSurfaces                  -> stage "surface"
- 0x1b388c7: after LargeCaveFeature/CanyonFeature -> stage "carved"

Each Block is named by its BlockLegacy (Block +0x10) raw name std::string
(BlockLegacy +0x28) and its data value (Block +0x8, uint16). The output
(TERRAIN_ORACLE_OUT, default ORACLE_OUT_DIR/terrain.jsonl) holds one line per
stage and chunk: chunk x/z, a palette of "name:data" and the zlib-compressed
little-endian uint16 palette indices in BlockVolume order.
"""
import base64
import json
import os
import struct
import zlib
import gdb

OUT = os.environ.get("TERRAIN_ORACLE_OUT",
                     os.path.join(os.environ.get("ORACLE_OUT_DIR", "."), "terrain.jsonl"))
names = {}


def r(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xffffffffffffffff


def mem(address, count):
    return bytes(gdb.selected_inferior().read_memory(address, count))


def q(address):
    return struct.unpack("<Q", mem(address, 8))[0]


def string(address):
    pointer, length = struct.unpack("<QQ", mem(address, 16))
    return mem(pointer, length).decode() if length else ""


def block_name(block):
    if block not in names:
        legacy = q(block + 0x10)
        data = struct.unpack("<H", mem(block + 8, 2))[0]
        names[block] = "%s:%d" % (string(legacy + 0x28), data)
    return names[block]


class Stage(gdb.Breakpoint):
    def __init__(self, address, stage):
        super().__init__("*" + hex(address), internal=True)
        self.stage = stage

    def stop(self):
        frame = r("rbp")
        x, z = struct.unpack("<2i", mem(q(frame - 0x20), 8))
        data = q(frame - 0x28) + 0x1800
        pointers = struct.unpack("<32768Q", mem(data, 32768 * 8))
        palette, index, cells = [], {}, []
        for p in pointers:
            key = block_name(p)
            if key not in index:
                index[key] = len(palette)
                palette.append(key)
            cells.append(index[key])
        blob = zlib.compress(struct.pack("<32768H", *cells), 9)
        with open(OUT, "a") as f:
            f.write(json.dumps({"stage": self.stage, "x": x, "z": z, "palette": palette,
                                "cells": base64.b64encode(blob).decode()}) + "\n")
        return False


Stage(0x1b38788, "heights")
Stage(0x1b387a1, "surface")
Stage(0x1b388c7, "carved")


# Biome registry (Biome::mBiomes 0x348b670, vector<unique_ptr<Biome>> by id):
# Biome +0x8 name, +0x34 temperature, +0x38 downfall, +0x4c depth, +0x50 scale,
# +0x80 BiomeType*, +0x88 id; MutatedBiome (vtable 0x31e8590) +0xc0 parent.
# Dumped once, at the first prepareHeights, with the VanillaBiomes globals.
VANILLA = os.environ.get("TERRAIN_VANILLA_BIOMES")
dumped = []


def dump_biomes():
    begin, end = struct.unpack("<QQ", mem(0x348b670, 16))
    globals_ = {}
    if VANILLA and os.path.exists(VANILLA):
        for line in open(VANILLA):
            address, name = line.split()
            globals_[q(int(address, 16))] = name.split("::m", 1)[1]
    out = []
    for slot in range(begin, end, 8):
        biome = q(slot)
        if not biome:
            out.append(None)
            continue
        temperature, downfall = struct.unpack("<2f", mem(biome + 0x34, 8))
        depth, scale = struct.unpack("<2f", mem(biome + 0x4c, 8))
        vtable = q(biome)
        entry = {"id": struct.unpack("<i", mem(biome + 0x88, 4))[0], "name": string(biome + 8),
                 "global": globals_.get(biome), "temperature": temperature, "downfall": downfall,
                 "depth": depth, "scale": scale, "type": hex(q(biome + 0x80)), "vtable": hex(vtable),
                 "raw": mem(biome, 0xd0).hex()}
        if vtable == 0x31e8590 + 0x10:
            entry["parent"] = struct.unpack("<i", mem(q(biome + 0xc0) + 0x88, 4))[0]
        out.append(entry)
    with open(OUT.replace(".jsonl", "_biomes.json"), "w") as f:
        json.dump(out, f, indent=1)


class RawBiomes(gdb.Breakpoint):
    # prepareHeights 0x1b36b47: after BiomeSource::fillRawBiomeData(-0x9e0, x*4-2, z*4-2).
    def stop(self):
        if not dumped:
            dumped.append(1)
            dump_biomes()
        frame = r("rbp")
        x, z = struct.unpack("<i", mem(frame - 0x14, 4))[0], struct.unpack("<i", mem(frame - 0x18, 4))[0]
        ids = [struct.unpack("<i", mem(q(frame - 0x9e0 + 8 * i) + 0x88, 4))[0] for i in range(100)]
        with open(OUT, "a") as f:
            f.write(json.dumps({"stage": "raw_biomes", "x": x, "z": z, "ids": ids}) + "\n")
        return False


RawBiomes("*0x1b36b47", internal=True)
