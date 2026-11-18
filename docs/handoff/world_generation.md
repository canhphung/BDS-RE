# World-generation continuation

The working branch contains the native `OverworldGenerator` through the
surface/carver stages, including Mesa and Frozen Ocean. See
`reverse/reports/TERRAIN.md` and `reverse/reports/DECORATION.md` for evidence.

Completed in this continuation:

- Mesa bands, Bryce pillars and forested plateaus; Frozen Ocean temperature
  patches, icebergs and snow caps. 576 native columns and two sets of 24
  natural chunks match, including Random continuation and subsequent carving.
- A 256-high cross-column `DecorationRegion` and native ore placement.
- LakeFeature: native water/lava basin carving, boundary rejection, grass
  repair using sky light, lava stone rims, custom upper blocks and height limits.
  78 original-server cases match all placements, final cells and Random streams.
- Default and Mesa ore-decorator sequences. 64 isolated features and 16 whole
  batches match, including rare gravel and high-altitude Mesa gold.
- `LevelSettings::parseSeedString` 0x1c69910, `Util::hashCode`,
  `Util::isIntegral`, `Util::stringTrim` and `Level::createRandomSeed`, with 49
  native cases and runtime integration. A one- or two-character unusable seed
  falls back to a random draw, so `level-seed=1` does **not** mean a fixed
  world; the old "-1997048360" note in TERRAIN.md was one random sample. See
  `reverse/reports/SEED.md`.

Validation: 50/50 tests pass in GCC Debug (the 48 earlier ones plus
`seed_reference` and the seed integration).

LakeFeature is now implemented explicitly as `place_lake`; see
`reverse/reports/LAKES.md` for API dependencies and native source-context details.
It requires real material and sky-brightness readers. Neighbor events,
waterlogging, pending ticks and lighting jobs are not implemented by that
component. The native fixture has an empty extra layer and uses the native
BlockSource event-suppression mode, not Ready-chunk gameplay events.

`OverworldGenerator::postProcess` orchestration was traced to its full control
flow on 2026-11-18: chunk lock, `BlockSource(...,false,true)` + `Tick(1)` queue,
the ThreadData-`+0x41810` decoration origin, two seed derivations that each burn
two `nextInt()` draws, the Desert/DesertHills water-lake skip, both lake draw
sequences with their Y/generator/`nextInt(10)` gates, the decorator virtual
dispatch order, MonsterRoomFeature, BonusChestFeature and the final pending-tick
pass. See `reverse/reports/POSTPROCESS.md`. Nothing is registered yet: the three
missing feature translations would silently change the Random stream.

Vegetation: seven small features are translated and verified against a 70-case
native corpus (184 placements, 15304 words, 16387 dependency answers); see
`reverse/reports/VEGETATION.md`. The reed column walk `0x1b5d205..0x1b5d326`
remains, because no corpus case reaches it.

BonusChestFeature `0x1b526b0` and MonsterRoomFeature `0x1b549a0` are traced but
not translated; `reverse/reports/CHEST_ROOM.md` records their structure,
including BonusChest's own local Random copy and MonsterRoom's spawner/block
entity dependency, which the project has not built yet.

Next in Phase 3: the remaining
`OverworldDecorator::decorate` 0x1da5250 features and biome-specific overrides.
Keep `OverworldGenerator::postProcess` 0x1b38950 as the orchestration reference.
It creates a temporary BlockSource/tick queue, selects the biome at the chunk's
maximum corner and reseeds its ThreadData Random again after lakes. Implement
each real feature and validate it before advertising the whole post-process
stage. Trees, marine features and snow/ice are still pending.

Seed resolution is done: use `bds::resolve_level_seed` (`src/world/seed.cpp`)
instead of hashing text yourself.

Use `build_surface_at` for controlled native surface tests; production
`build_surfaces` calls the same dispatcher. Use `place_lake` and
`decorate_ores` explicitly;
`generate()` has not been changed to imply complete decoration. Runtime
generator/palette/biome-byte integration is the following separate workstream.

The native setup helper compiled by `gdb_ore_cases.py` only calls the original
setter to restore input geometry. Preserve exact native Random streams and
mutation order. `tools/reference/capture_item_use_distance.py` was already an
untracked user file before this work and must remain untouched.
