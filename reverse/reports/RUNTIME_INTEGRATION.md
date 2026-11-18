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
- Biome bytes in `FullChunkData` are still the host's flat table; the
  generator's biome data is not sent.
