#!/usr/bin/env python3
"""Build tests/reference/terrain_bds_1.6.0.15.json from a capture_terrain.py run.

Keeps, for seed 123: every raw 10x10 biome grid (BiomeSource::fillRawBiomeData
at prepareHeights), the 256 biome bytes of each FullChunkData packet
(LevelChunk biomes, index (z << 4) | x), and the BlockVolume after
prepareHeights and after buildSurfaces for a sample of chunks (palette +
zlib/base64 uint16 cells in BlockVolume order (x * 16 + z) * 128 + y).
"""
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "reference"))


def varint(data, i):
    value = shift = 0
    while True:
        b = data[i]
        i += 1
        value |= (b & 0x7f) << shift
        shift += 7
        if not b & 0x80:
            return value, i


def zigzag(v):
    return (v >> 1) ^ -(v & 1)


def chunk_biomes(body):
    # FullChunkDataPacket: ChunkPos (zigzag varints), then a string: section
    # count, sections, 512 height-map bytes, 256 biome bytes, ...
    data = bytes.fromhex(body)
    x, i = varint(data, 0)
    z, i = varint(data, i)
    length, i = varint(data, i)
    payload = data[i:i + length]
    count = payload[0]
    j = 1
    for _ in range(count):
        version, layers = payload[j], payload[j + 1]
        j += 2
        for _ in range(layers):
            header = payload[j]
            j += 1
            bits = header >> 1
            per_word = 32 // bits
            words = (4096 + per_word - 1) // per_word
            j += words * 4
            palette, j = varint(payload, j)
            palette = zigzag(palette)
            for _ in range(palette):
                _, j = varint(payload, j)
    j += 512
    return zigzag(x), zigzag(z), list(payload[j:j + 256])


def main():
    run = Path(sys.argv[1])
    out = Path(sys.argv[2])
    samples = int(sys.argv[3]) if len(sys.argv) > 3 else 24
    raw, volumes = [], {}
    for line in (run / "terrain.jsonl").read_text().splitlines():
        e = json.loads(line)
        if e["stage"] == "raw_biomes":
            raw.append({"x": e["x"], "z": e["z"], "ids": e["ids"]})
        elif e["stage"] in ("heights", "surface"):
            volumes.setdefault((e["x"], e["z"]), {})[e["stage"]] = {"palette": e["palette"], "cells": e["cells"]}
    chunks = [dict(zip(("x", "z", "biomes"), chunk_biomes(p["body"])))
              for p in json.loads((run / "chunks.json").read_text())]
    keys = sorted(k for k, v in volumes.items() if len(v) == 2)
    step = max(1, len(keys) // samples)
    sample = [{"x": k[0], "z": k[1], **volumes[k]} for k in keys[::step][:samples]]
    out.write_text(json.dumps({"binary_sha256": "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
                               "seed": int(json.loads((run / "metadata.json").read_text())["properties"]["level-seed"]),
                               "generator": "tools/re/capture_terrain.py + tools/re/make_terrain_reference.py",
                               "raw_biomes": raw, "chunk_biomes": chunks, "volumes": sample}) + "\n")
    print(f"{len(raw)} raw grids, {len(chunks)} chunks, {len(sample)} volumes -> {out}")


if __name__ == "__main__":
    main()
