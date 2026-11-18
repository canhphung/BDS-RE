# Overworld terrain generation (BDS 1.6.0.15, Linux)

Translation of `OverworldGenerator` and the biome layers, verified against the
original's own data. Work in progress, in phases:

1. noise, biome layers, height field, surfaces, bedrock (this document);
2. LargeCaveFeature / CanyonFeature carvers;
3. `OverworldGenerator::postProcess` decoration;
4. structures.

## Oracles

- `tools/re/capture_terrain.py` runs the original with only
  `tools/re/gdb_terrain_oracle.py` loaded (the level-Random tracer of
  `capture_level_random.py` stops on every Random call and starves the chunk
  workers). The oracle dumps `OverworldGenerator::loadChunk` 0x1b38610's
  BlockVolume (ThreadData +0x1800, 32768 `Block const*`, index
  `(x * 16 + z) * 128 + y`) after prepareHeights, buildSurfaces and the
  carvers, every raw 10 x 10 biome grid of prepareHeights, and the biome
  registry (`Biome::mBiomes` 0x348b670).
- `--layer-oracle` instead stops once and calls every biome layer's fillArea
  (`tools/re/gdb_layer_oracle.py`), with each layer's class, base seed and
  world seed.
- `tools/re/make_terrain_reference.py` writes
  `tests/reference/terrain_bds_1.6.0.15.json`; `tools/re/make_biome_table.py`
  writes `src/world/biome_data.cpp` from `tests/reference/biomes_bds_1.6.0.15.json`.

## Noise (src/world/noise.cpp)

| Original | BDS-RE |
|---|---|
| ImprovedNoise _init 0x1b77650, _calcValues 0x1b77950, _blendCubeCorners 0x1b77a00, _grad 0x1b78110 (uvMask table), _grad2 0x1b77e20, _noise 0x1b777c0, _readArea 0x1b78400 (2D branch for y size 1; corners cached per y cell) | `ImprovedNoise` |
| PerlinNoise ctor 0x1b73780, getValue 0x1b737d0, getRegion 0x1b738f0 / 0x1b73aa0 | `PerlinNoise` |
| SimplexNoise ctor 0x1b74960 (origin Vec3(third, second, first)), _getValue(Vec2) 0x1b74b40, _add 0x1b75c20; SQRT3/F2/G2 0x1772880.. | `SimplexNoise` |
| PerlinSimplexNoise _init 0x1b76180, getValue 0x1b762d0, getRegion 0x1b76510 | `PerlinSimplexNoise` |

All in single precision as the original.

## Biome layers (src/world/biome_layers.cpp)

`OverworldDimension::createDefaultLayers` 0x1d4f260 with every layer's
fillArea. The layer graph, salts and world seeds were checked against the
original's layer objects, and every one of the 53 layers' outputs matches the
original's for 10 x 10 and 12 x 12 areas. Findings that differ from the
obvious reading:

- `ZoomLayer::zoom(salt, layer, n)` 0x1d491e0 gives every one of the n layers
  salt + 1.
- `RegionHillsLayer` has no `init` override, so its river-noise parent chain
  (+0x40, two zooms) keeps world seed 0.
- `AddOceanTemperatureLayer` fills its parent but draws `nextRandom(100) / 100`
  thresholds (0.075, 0.4, 0.675, 0.95); `AddOceanEdgeLayer` turns warm next to
  frozen (and frozen next to warm) into ocean; `OceanMixerLayer` keeps the
  temperature for every ocean but the plain deep ocean.
- Layers called with larger areas than the original ever uses (16 x 16 below
  the shore layers) overflow the original's own buffers; the oracle results
  for those areas are not comparable.

`terrain_reference` compares 41,800 raw-biome cells (418 chunks,
BiomeSource::fillRawBiomeData 0x1dbba10) and 28,928 chunk-biome bytes (113
FullChunkData packets, fillBiomeData 0x1dbb8c0 through VoronoiZoom) with the
original: all match.

## Not yet translated

prepareHeights / getHeights are read (see the comments in the code once
added), buildSurfaces, bedrock, carvers, postProcess and structures are not
yet in BDS-RE; the server still uses the old independent terrain.
