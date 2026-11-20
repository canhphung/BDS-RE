# World-generation continuation

Branch: `claude/clever-newton-bboqgm`, pushed to
`https://github.com/canhphung/BDS-RE`.

Reference binary: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`. Every
translation cites the native address; every fixture records the binary SHA-256
so a wrong binary cannot silently pass.

Status: **60/60 CTest tests pass in GCC Debug**. Nothing unverified is in the
tree: features are only committed once their corpus replays exactly.

## What is complete and verified

### Terrain and generation
- `OverworldGenerator` through surface generation and the four carvers, with
  Mesa bands, Bryce pillars, forested plateaus, Frozen Ocean temperature
  patches and icebergs. `reverse/reports/TERRAIN.md`.
- A 256-high cross-column `DecorationRegion`.
- Seed resolution: `bds::resolve_level_seed` (`src/world/seed.cpp`,
  `reverse/reports/SEED.md`), 49 native cases. A one- or two-character seed
  falls back to a random draw, so `level-seed=1` does **not** mean a fixed
  world.

### Decoration features, each with a native corpus
| Feature | Address | Corpus | Result |
|---|---|---|---|
| vegetation (7 features) | various | `vegetation_bds_1.6.0.15.json` | 70 cases, 184 placements, 15304 words |
| BonusChestFeature | `0x1b526b0` | `bonus_chest_bds_1.6.0.15.json` | 12 cases |
| ClayFeature | `0x1b557f0` | `clay_bds_1.6.0.15.json` | 14 cases, 538 placements |
| OreFeature / ore decorator | `0x1b58e40` | `decoration_ore_bds_1.6.0.15.json` | 64 isolated + 16 batches |
| LakeFeature | `0x1b5dc70` | `decoration_lake_bds_1.6.0.15.json` | 78 cases |
| TreeFeature base | `0x1b5a290` | 5 fixtures | **all 16 cases, 511 placements** |

TreeFeature is complete: `_prepareSpawn` `0x1b59e30`, `_placeTrunk` `0x1b5ac20`,
`_placeLeaf` `0x1b59d60`, the leaf volume and `place` `0x1b5a290` each replay
their corpus. `reverse/reports/TREES.md` records every rule and the four
non-obvious ones (non-replaceable cells skip rather than break; the vine pass
runs after every log and is gated by the trunk scale; a failed trunk makes the
feature succeed early; the tail writes dirt under the origin a second time).

### Runtime integration (`reverse/reports/RUNTIME_INTEGRATION.md`)
`TerrainConfig::kind` 3 selects the translated generator; `ensure_chunk` fills
the chunk from it, runs the decoration stage and applies the mutations.
`network_chunk` sends the original's 256 biome bytes (`chunk_biomes`,
`BiomeSource::fillBiomeData` `0x1dbb8c0`); all 113 captured arrays match.

Measured in a generated world: ore (gravel/coal/iron/redstone), clay,
vegetation, and trees (20 logs and 195 leaves in chunk `(-3,-3)` of seed 123).

The generator is **opt-in** via the non-documented `level-type=OVERWORLD`
because the native carvers sweep a 17x17 chunk neighbourhood per chunk (about
110 ms per chunk in Debug) and the existing integration fixtures assert the old
terrain. Default stays kind 2.

## What is NOT done

- **8 tree subclasses** (`FancyTreeFeature` `0x1b4aff0`, `SwampTreeFeature`
  `0x1b6bcb0`, `SavannaTreeFeature` `0x1b511c0`, `JungleTreeFeature` `0x1b65db0`,
  `MegaTreeFeature` `0x1b55c70`, `MegaPineTreeFeature` `0x1b4d740`,
  `MegaJungleTreeFeature` `0x1b533e0`, `RoofTreeFeature` `0x1b5b7e0`) — none
  translated. The base class they override is done, so each is a smaller job
  than the base was.
- **Marine**: `SeagrassFeature` `0x1b6d090`, `CoralFeature` `0x1b676f0`,
  `IcebergFeature` `0x1b63830`, `BlueIceFeature` `0x1b4d150` — untranslated.
  `KelpFeature` `0x1b53de0` has a full corpus
  (`kelp_bds_1.6.0.15.json`, 14 cases, 583 placements, 683 words) and a solved
  draw model but an unresolved read-count bug at query 265; see
  `reverse/reports/MARINE.md`.
- **Snow/ice covering**: there is **no `SnowFeature`**. Surface snow is
  `TopSnowBlock::buildSnowBlock 0x1e5d0d0` and the `AddSnowLayer` biome layer
  (already translated, in the chain at salt 2). The cold block names are mapped
  to palette ids 54..56.
- **`OverworldDecorator::decorate` `0x1da5250`** — the dispatcher. Traced only.
  It cannot be translated before the features above, because a stub changes the
  Random stream and silently desynchronises everything downstream.
- **MonsterRoomFeature `0x1b549a0`** — blocked on the `MobSpawnerBlockActor` /
  `BaseSpawner` surface, which the project has not built.
- **`postProcess` `0x1b38950` is a bounded slice only**: seed derivations and
  lake draws on a private Random. It does not call the decorator, MonsterRoom or
  BonusChest.
- **Decoration stage ordering is not native**: `decorate_generated_chunk` groups
  the verified features and picks deterministic sites because the real
  dispatcher is missing. It must be replaced by the dispatcher, not extended.

## Working rules this branch established

- Translate, never infer. Every constant comes from the binary; a guessed table
  was written twice and deleted twice.
- Fields like `+0x18`/`+0x1c` on feature objects are registry **type ids**, not
  `Block*`; passing a pointer where an id belongs aborts far from the mistake.
- `BlockPos::offset(x,y,z)` takes x in `edx`, y in `ecx`, z in `r8d`; the draw
  order must be written in register order, not X/Y/Z.
- Oracles must not patch feature code, RNG draws or expected results. Setup-only
  helpers may call the original's own setter to build geometry.
- A breakpoint inside a function that GDB itself invoked is not supported. Call
  the function directly as a function oracle instead (the `_placeLeaf` fix).
- Never commit a partial replay. If the corpus does not match, the code is not
  in the tree.

## Entry points for the next session

- `tools/re/capture_level_random.py --gdb-post-script <script>` runs the
  original under gdb in an isolated world. `--runtime
  input/reference-runtime/bds --library-path input/reference-runtime/openssl-local/lib
  --preload input/reference-runtime/ipv6_shim.so --gdb /usr/bin/gdb`. Each run
  takes about three minutes.
- Existing oracle scripts to copy: `gdb_tree_cases.py`, `gdb_vegetation_cases.py`,
  `gdb_clay_cases.py`, `gdb_kelp_cases.py`, `gdb_bonus_chest_cases.py`.
- Existing exporters: `export_tree_place_reference.py` and friends; they take
  the raw oracle directory and validate provenance.
- `build_surface_at` is for controlled native surface tests; production
  `build_surfaces` uses the same dispatcher.
- `tools/reference/capture_item_use_distance.py` was an untracked user file
  before this work and must remain untouched.
