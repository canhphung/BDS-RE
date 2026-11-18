# OverworldGenerator runtime integration — step 5, first slice

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

## What is wired

`World` gained terrain kind 3: `World::ensure_chunk` fills a column from
`OverworldGenerator::generate`, the translated `loadChunk` `0x1b38610`
(`prepareHeights` → `buildSurfaces` → the four carvers). The generator's
128-high `TerrainVolume` is copied into the host chunk through
`internal_id`, which maps the generator's `TerrainBlock` names onto this
server's internal block ids (`air` 0, `stone` 1, `grass` 2, `dirt` 3,
`bedrock` 4, `water`/`lava` to their liquid ids). Unknown names fall back to
air rather than inventing an id.

`TerrainConfig::kind` is now `0` explicit legacy columns, `1` flat, `2` the
independent rolling terrain and `3` the translated generator. The snapshot
loader accepts kind 3, so a world created this way survives a restart.

## Why it is opt-in

`DedicatedServer::run` selects the generator from `level-type`:

| `level-type` | kind | generator |
|---|---:|---|
| `FLAT` | 1 | flat layers |
| `DEFAULT` / anything else | 2 | independent rolling terrain |
| `OVERWORLD` | 3 | **translated `OverworldGenerator`** |

`OVERWORLD` is not a documented vanilla value; it exists so the translated
generator can be exercised without changing the default world, because the
existing integration fixtures (`raknet_handshake`, `movement_reference`,
`player_actions_reference`) assert properties of the old terrain.

The translated generator is also much slower. Measured in a Debug build:

| Stage | ms per chunk |
|---|---:|
| `prepareHeights` | 6.0 |
| `+ buildSurfaces` | 8.5 |
| `+ carve` | **147.3** |

The carvers sweep a 17x17 chunk neighbourhood per chunk, which is what the
native code does; the original hides the cost on its chunk worker threads.
BDS-RE generates on the simulation thread, so 139 chunks took long enough to
time out `raknet_handshake` when kind 3 was the default. Making it opt-in
keeps the default fast and still lets the translated path be tested.

A generator instance is cached per seed in `src/world/terrain.cpp`: building
its noise objects per chunk cost more than the generation itself.

## Test

`world_generator` checks the integration rather than the generator (which
`terrain_reference` already covers):

- a kind-3 World fills a chunk whose cells follow `OverworldGenerator::generate`
  for the same seed;
- generation is deterministic across two independent Worlds;
- an edit placed with `set_block` survives later `ensure_chunk` calls.

## postProcess runtime slice

`src/world/post_process.cpp` adds `post_process_chunk`, called from
`ensure_chunk` for kind 3. It is a **bounded** slice of
`OverworldGenerator::postProcess` `0x1b38950`: it reproduces

- the first seed derivation `0x1b38b5e` (seed a private Random with the level
  seed, draw two `nextInt()`, make them odd, mix with the origin, xor the
  level seed — the same arithmetic `Carver::apply` uses, in unsigned 32-bit);
- the water-lake selection draws `0x1b38c54..0x1b38d51`: `nextInt(4)` gate,
  `nextInt(3,12)`, `nextInt(128)`, `nextInt(3,12)`, plus the Desert /
  DesertHills and generator-flag skips;
- the lava-lake selection `0x1b38d5b..0x1b38e9b`, which runs only when the
  water lake did not place: `nextInt(8)` gate, `nextInt(3,12)`,
  `nextInt(0x78)+8`, `nextInt(w)`, `nextInt(3,12)`, and the `0x3c`/`0x40`
  gates with `nextInt(10)`;
- the second derivation `0x1b38ea5`.

Every draw comes from the slice's **private** Random, so the level Random is
untouched — the level-Random acceptance trace in
`tests/reference/world_ticks_bds_1.6.0.15.json` depends on that, and
`post_process` asserts it directly.

The slice deliberately does **not** call the lake features (they need a
material and sky-brightness reader this layer does not supply), the decorator
dispatch, `MonsterRoomFeature` or `BonusChestFeature`. The translated ore, clay
and vegetation features therefore still have no caller. `post_process` reports
the region's mutations so a later wiring can apply them.

## Decoration actually reaches the world

`src/world/decoration_stage.cpp` adds `decorate_generated_chunk`: it builds a
`DecorationRegion` from the generated `TerrainVolume` (plus empty neighbours, so
a cross-column write is reported and the owning chunk regenerates it
deterministically) and runs the features that are **verified and need no
missing infrastructure**:

| Native | Translation |
|---|---|
| `OverworldDecorator::decorateOres` `0x1da6de0` | `decorate_ores` (100 attempts) |
| `ClayFeature::place` `0x1b55840` | `place_clay` |
| `TallGrassFeature` `0x1b66570`, `FlowerFeature` `0x1b5ef70`, `ReedsFeature` `0x1b5d070` | the corresponding `place_*` |

`ensure_chunk` applies the reported mutations that fall inside the chunk. The
host palette grew by the decoration blocks (`gravel`, the six ores, `clay`,
`sand`, and stone variants 1/3/5) appended so every existing internal id keeps
its meaning, and `internal_id` maps them.

Measured on chunk (0,0) with seed 123: **153 gravel, 69 coal ore, 9 iron ore,
3 redstone ore**, plus the stone variants. So translated decoration finally
appears in a generated world.

**This is not the native order.** The real sequence is the
`OverworldDecorator::decorate` body (`0x1da5250`), which the project has not
translated because it dispatches into trees, plants, marine and snow features
that are missing ([DECORATE.md](DECORATE.md)). The stage groups the verified
features and says so in the file; it must be replaced by the real dispatcher,
not extended.

## Still pending in step 5

- `postProcess` and the decoration stages are **not** called from the runtime.
  The translated features (ore, lake, clay, vegetation, bonus chest, tree base)
  have no caller yet; wiring them needs the `OverworldDecorator` dispatcher and
  the chunk post-processing state machine, which are tracked in
  [DECORATE.md](DECORATE.md) and [POSTPROCESS.md](POSTPROCESS.md).
- The host palette only knows the seven original ids plus the liquid depths.
  The generated terrain currently only produces air, stone, grass, dirt,
  bedrock, water and lava, so the palette gap does not surface yet, but
  decoration will need sand, gravel, clay, logs, leaves and the ores.
- Biome bytes in `FullChunkData` **are now the original's** for a kind-3
  world: `network_chunk` calls `chunk_biomes` (`BiomeSource::fillBiomeData`
  `0x1dbb8c0` through the VoronoiZoom layer) instead of a constant Plains, and
  `chunk_biome_bytes` checks **all 113 captured chunk biome arrays** from
  `terrain_bds_1.6.0.15.json`, 24 of which contain more than one biome.
  Explicit and independent worlds still send Plains, which is what they are.
