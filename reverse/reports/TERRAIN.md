# Overworld terrain generation (BDS 1.6.0.15, Linux)

Translation of `OverworldGenerator` and the biome layers, verified against the
original's own data. Work in progress, in phases:

1. noise, biome layers, height field, surfaces, bedrock;
2. LargeCaveFeature / CanyonFeature carvers and their Underwater subclasses;
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

## Height field and surfaces (src/world/overworld.cpp)

| Original | BDS-RE |
|---|---|
| OverworldGenerator ctor 0x1b36090: one Random(seed) for minLimit(16), maxLimit(16), main(8), surface simplex(4), scale(10), depth(16), forest(8); biome weights 10 / sqrt(dx² + dz² + 0.2) | `OverworldGenerator` |
| getHeights 0x1b373d0: 5 x 17 x 5 density from depth (200), main (8.555, 4.278, 8.555), min (684.412, 855.515, 684.412) / 256 and max (684.412) / 512 noises | `OverworldGenerator::heights` |
| prepareHeights 0x1b36ac0: raw biomes at (x * 4 - 2, z * 4 - 2), trilinear interpolation into stone / water below 63 / air | `prepare_heights` |
| loadChunk 0x1b38610: Random seeded with x * 0x14609048 + z * 0x7ebe2d5 | `generate` |
| buildSurfaces 0x1b37f80: surface noise at scale 0.125 (freq 1, amp 0.5), Biome::buildSurfaceAt per column (x outer) | `build_surfaces` |
| OverworldBiome::buildSurfaceAt 0x1db5c10, _placeBedrock 0x1db5b00; Taiga 0x1da0ea0, ExtremeHills 0x1d8edc0, Savanna 0x1dc5490, Swamp 0x1da20b0, Mutated 0x1dba960 | `default_surface` and the class cases |

Findings: the biome blend of getHeights is 3 x 3 (`range_incl(-1, 1)`) with the
inner weights of the 5 x 5 table; the min-limit noise is divided by 256 and
uses a y scale of 855.515; bedrock fills y 0 up to *and including*
`nextInt(1, 5)`; the swamp builder (decorator noise `PerlinSimplexNoise(2345,
1)`) stops at the first block whose material is air, so with air above the
surface it never places its water. The subclasses write the biome's top and
filler before the default builder reads them.

`terrain_reference` regenerates 24 chunks and compares the BlockVolume after
prepareHeights and after buildSurfaces with the original's: all 2 x 786,432
cells match (`generate_surface`).

## Carvers (src/world/caves.cpp)

loadChunk 0x1b38610 (unless generator +0x48 is set) calls, on the same
BlockVolume and loadChunk Random, `LargeCaveFeature::apply` for the
UnderwaterLargeCaveFeature (+0x9c0), `CanyonFeature::apply` for the
UnderwaterCanyonFeature (+0x9c8), then LargeCaveFeature (+0x9a8) and
CanyonFeature (+0x9b0). The subclasses only override `carve` (vtable +0x10)
and `isDiggable`.

| Original | BDS-RE |
|---|---|
| LargeCaveFeature::apply 0x1b2e7c0 / CanyonFeature::apply 0x1b497b0: setSeed(level seed), a = nextInt() / 2 * 2 + 1, b likewise, for x, z in chunk ± 8 setSeed((x * a + z * b) ^ seed) | `Carver::apply` |
| LargeCaveFeature::addFeature 0x1b2e920, addRoom 0x1b2ebd0, addTunnel 0x1b2ecc0 | `cave_add_feature`, `cave_add_room`, `cave_add_tunnel` |
| CanyonFeature::addFeature 0x1b4aa80 (vtable +0x18), addTunnel 0x1b49ad0 | `canyon_add_feature`, `canyon_add_tunnel` |
| LargeCaveFeature::carve 0x1b2fbd0, detectWater 0x1b2fa30, isDiggable 0x1b2f850, _thinSand 0x1b2f730 | `cave_carve`, `detect_water`, `diggable`, `thin_sand` |
| CanyonFeature::carve 0x1b4a560, detectWater 0x1b49930 (same code as the cave's) | `canyon_carve` |
| UnderwaterLargeCaveFeature::carve 0x1b43550, isDiggable 0x1b43290 | `underwater_cave_carve`, `underwater_diggable` |
| UnderwaterCanyonFeature::carve 0x1b429f0 | `underwater_canyon_carve` |
| BiomeSource::getBiome(BlockPos) (vtable +0x38): zoomed layer, 1 x 1 area | `biome_at` |

Findings (all kept as the original does them):

- `Random::nextGaussianFloat` 0x1b30420 is `nextFloat() - nextFloat()`, not a
  Gaussian.
- Every tunnel draws from its own `Random(random.nextInt())`, and `carve`
  receives that local Random (the underwater carvers draw magma/obsidian from
  it).
- carve's y loop starts at y1 - 1 while the block index starts at
  `index(x, y1, z)`: the cell tested and written at y is the block at y + 1,
  "above" is y + 2, and the grass fix-up writes to y.
- Land carvers return false at their first diggable block when the origin's
  biome has the Ocean type (`VanillaBiomeTypes::Ocean`, the type of biome 0);
  the underwater ones return false when it does not. LargeCave and Canyon
  carve nothing when water is on the shell of the box (detectWater).
- The canyon's height adds 15 when `0.2 >= (float)nextInt()`, so only for a
  draw of 0; its vertical scale is 4 (40 with doubled thickness when
  CanyonFeature +0x8 is set: true for CanyonFeature, false for the
  underwater one).
- The underwater carvers set water by looking at the neighbours of
  `ChunkBlockPos(unsigned char)` 0x1b3b9d0 built from the low byte of the
  volume index, which sets x, z and y all to that byte. Only bytes below 16
  have in-bounds neighbours, so almost every cell becomes still water.
- Underwater cave: y == 9 gives magma (nextFloat < 0.25) or obsidian, y < 9
  still lava, y < 62 only. Underwater canyon: y < 10 gives magma (< 0.0625),
  stone (< 0.25) or obsidian.

`terrain_reference` runs the carvers after buildSurfaces and compares with the
original's volume after the carvers. The fixtures hold 24 chunks of seed 123
(21 of them carved, 17,318 changed cells) and 25 ocean chunks with
underwater carving (`terrain_ocean_bds_1.6.0.15.json`); every chunk of the
six captures (2,514 chunks: five runs of seed 123 and one of server
`level-seed=1`) matches after the carvers. Note: the original hashed
`level-seed=1` into RandomSeed -1997048360 (level.dat), which
`make_terrain_reference.py` now reads instead of server.properties.

## Post-processing (src/world/gen_region.cpp, src/world/chunk_post_process.cpp)

Oracle: `capture_terrain.py --postprocess` loads
`tools/re/gdb_postprocess_oracle.py`, which records in time order every
chunk load, each postProcess call (with the state and heightmap of its 3 x 3
area at the start), the Biome::decorate vcall, and every
LevelChunk::setBlock / setExtraBlock call (all block writes go through
them). It also dumps the BlockLegacy registry (tests/reference/
blocks_bds_1.6.0.15.json, generated into src/world/block_data.cpp by
`tools/re/make_block_table.py`), including the results of the no-argument
virtual canContainLiquid evaluated by the original.
`tools/re/make_postprocess_reference.py` keeps, per call, the earlier writes
into its area and its own writes; `postprocess_reference` rebuilds the area
from the generator, replays the earlier writes and compares post_process's
write sequence with the original's.

| Original | BDS-RE |
|---|---|
| postProcess 0x1b38950: Random (ThreadData +0x41810) seeded like the carvers from the chunk position; biome at LevelChunk::getMax | `run_post_process` |
| water lake (not Desert/DesertHills, nextInt(4) == 0, nextInt(3, 12) / nextInt(128) / nextInt(3, 12)); lava lake (nextInt(8) == 0, y = nextInt(nextInt(120) + 8), only y >= 60, above 63 one in 10) | `run_post_process` |
| LakeFeature::place 0x1b5dc70, _check 0x1b5e980, Feature::_placeBlock 0x1b6ebd0 (setBlock flags 3) | `LakeFeature` |
| second reseed 0x1b38ea5, 8 dungeon attempts (origin + nextInt(16) + 8, nextInt(128)) | `run_post_process` |
| MonsterRoomFeature::place 0x1b549a0, _getFacingDataId 0x1b55340, _getRandomEntityId 0x1b55590 | `MonsterRoomFeature` |
| BlockSource::setBlock 0x1c59bb0, LevelChunk::setBlock 0x1de2560 (extra layer), setExtraBlock 0x1de2980 | `GenRegion::set_block` |
| BlockSource::getHeightmap 0x1c5ad20, getAboveTopSolidBlock 0x1c5ab40 / LevelChunk 0x1de8120, isTopSolid 0x1de8070, recomputeHeightMap 0x1de9520 | `GenRegion` |

Findings: postProcess reseeds the same Random again after the lakes, so the
dungeons do not depend on the lake draws. The lake blob centre is
`((16, 8, 16) - size - (2, 4, 2) + (1, 2, 1) + size / 2)` with each component
then multiplied by its own nextFloat (Java scales only the free range); lava
lakes below y 60 are skipped. The dungeon walls are written from py + 3 down
to py - 1, so its ceiling is only tested; chest offsets draw z before x and
each chest draws its loot seed (`loot_tables/chests/simple_dungeon.json`).
LevelChunk::_lightingCallbacks (heightmap updates) and the BlockSource
events (neighbour updates) only run for chunks in state 8, so during
post-processing the heightmap stays as loadChunk computed it. The water lakes
recorded place no grass rim: the sky brightness of the not yet lit chunks is
taken as 0 (INFERRED).

All 192 recorded calls (seed 123) match up to Biome::decorate; the committed
fixture keeps 7 of them (plain chunks, dungeons, water lakes).

## Not yet translated

MesaBiome / OceanFrozenBiome surfaces (the default builder is used for them),
Biome::decorate and the end of postProcess (snow/ice, _fixWaterAlongEdges,
seasons, pending ticks), the BlockSource events for loaded chunks, and the
structures (their postProcess calls sit between the lakes and the dungeons);
the server still uses the old independent terrain.
