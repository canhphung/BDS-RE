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

Validation: 48/48 tests pass in Debug, Release, ASan+UBSan and Clang.

LakeFeature is now implemented explicitly as `place_lake`; see
`reverse/reports/LAKES.md` for API dependencies and native source-context details.
It requires real material and sky-brightness readers. Neighbor events,
waterlogging, pending ticks and lighting jobs are not implemented by that
component. The native fixture has an empty extra layer and uses the native
BlockSource event-suppression mode, not Ready-chunk gameplay events.

Next in Phase 3: the remaining
`OverworldDecorator::decorate` 0x1da5250 features and biome-specific overrides.
Keep `OverworldGenerator::postProcess` 0x1b38950 as the orchestration reference.
It creates a temporary BlockSource/tick queue, selects the biome at the chunk's
maximum corner and reseeds its ThreadData Random again after lakes. Implement
each real feature and validate it before advertising the whole post-process
stage. Trees, marine features and snow/ice are still pending.

Use `build_surface_at` for controlled native surface tests; production
`build_surfaces` calls the same dispatcher. Use `place_lake` and
`decorate_ores` explicitly;
`generate()` has not been changed to imply complete decoration. Runtime
generator/palette/biome-byte integration is the following separate workstream.

The native setup helper compiled by `gdb_ore_cases.py` only calls the original
setter to restore input geometry. Preserve exact native Random streams and
mutation order. `tools/reference/capture_item_use_distance.py` was already an
untracked user file before this work and must remain untouched.
