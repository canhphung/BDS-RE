# World generation — session summary

Branch `claude/clever-newton-bboqgm`, pushed to
`https://github.com/canhphung/BDS-RE`.
Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

**61/61 CTest tests pass in GCC Debug** (`ctest -j1`). Nothing unverified is in
the tree.

## What the objective asked, and what actually landed

The objective had five parts. This is the honest scoreboard.

### 1. `src/world/vegetation.cpp` — 70 cases — DONE

All 70 cases replay: **184 placements, 15304 Random words, 16387 dependency
answers**. `reverse/reports/VEGETATION.md`.

### 2. BonusChestFeature + MonsterRoomFeature — HALF

- `BonusChestFeature` `0x1b526b0` translated and verified: 12 cases, 36
  placements, 12 local-Random words, 96 dependency answers, 12 chests flagged
  spawned. `reverse/reports/CHEST_ROOM.md`.
- `MonsterRoomFeature` `0x1b549a0` **not translated**. It needs
  `MobSpawnerBlockActor` / `BaseSpawner` / `_getRandomEntityId`, a surface this
  project has never built. `CHEST_ROOM.md` records its structure so the work
  does not start from zero.

### 3. `OverworldDecorator::decorate` `0x1da5250` — NOT DONE

Traced only (`reverse/reports/DECORATE.md`, size `0x18f2`, every cited address
audited). It dispatches into the tree, plant, marine and snow features; a stub
would change the Random stream, which is the exact failure every corpus in this
project exists to catch. So it stays untranslated by design.

The runtime instead has `decoration_stage.cpp`, which runs the features that
ARE verified and says plainly that it must be replaced by the real dispatcher,
not extended.

### 4. Trees, marine, snow/ice — PARTIAL

**Trees:** the whole `TreeFeature` base class is done and verified —
`_prepareSpawn` `0x1b59e30` (8 placements, 919 answers), `_placeTrunk`
`0x1b5ac20` (87 placements, 40 words), `_placeLeaf` `0x1b59d60` (30 cases),
the leaf volume (448 cells) and `place` `0x1b5a290` (511 placements). Plus
`JungleTreeFeature` `0x1b65db0` (523 placements through the subclass) wired into
the runtime. Six subclasses remain, from `FancyTreeFeature` `0x1b4aff0` to
`RoofTreeFeature` `0x1b5b7e0`.

**Marine:** none translated, but two corpora are captured and analysed:
- `kelp_bds_1.6.0.15.json` — 14 cases, 583 placements, 683 words. The draw
  model is CONFIRMED (all ten candidate cells of case 0 reproduce from the
  seed); the pass reaches query 265/337 and fails on a read-count detail.
- `seagrass_bds_1.6.0.15.json` — 14 cases, 111 placements, 575 words. The
  candidate structure is fully mapped (5 accept, 3 abort per case); one question
  remains, namely which Random object the `nextInt(0x14)` gate runs on.

**Snow/ice:** there is **no `SnowFeature`**. Surface snow is
`TopSnowBlock::buildSnowBlock 0x1e5d0d0` plus the `AddSnowLayer` biome layer
(already translated and in the chain at salt 2). The cold block names are mapped
to palette ids 54..56 and cold chunks emit them.

### 5. Runtime integration — 4 of 5 pieces

| Piece | State |
|---|---|
| Replace generator with `OverworldGenerator` | kind 3, opt-in via `level-type=OVERWORLD` |
| Extended block palette | + gravel, six ores, clay, sand, stone variants, log/leaves/sapling, snow/ice/packed_ice |
| Correct biome bytes in `FullChunkData` | **all 113 captured arrays match** |
| postProcess | bounded slice: seed derivations + lake draws on a private Random |
| mob spawning in the level tick | **not done** (needs the same block-entity surface as MonsterRoom) |

Kind 3 is opt-in because the native carvers sweep a 17x17 chunk neighbourhood
per chunk (147 ms/chunk in Debug) and the existing integration fixtures assert
the old terrain. Default stays kind 2.

Measured in a generated world: ore (153 gravel, 69 coal, 9 iron, 3 redstone in
chunk `(0,0)` of seed 123), clay, vegetation, trees (20 logs + 195 leaves in
`(-3,-3)`), and cold blocks in a cold chunk.

## Numbers for this session

| | |
|---|---:|
| Commits | 44 |
| Tests | 61 (was 50) |
| Native corpora | 50 fixtures |
| Oracle scripts | 33 |
| `src/world` lines | 5,111 |

## What a next session should do, in order

1. **Finish Kelp** (`reverse/reports/MARINE.md`): the draw model is confirmed and
   the pass reaches query 265 of 337; the remaining bug is read counts inside
   the candidate body.
2. **Finish Seagrass** (`MARINE.md`): map is complete; resolve which Random the
   `nextInt(0x14)` gate uses.
3. **Tree subclasses**, easiest first: `MegaTreeFeature`'s height wrapper is
   already translated; `JungleTreeFeature` is the template that worked.
4. Only then attempt `OverworldDecorator::decorate`, and only after enough
   features exist that no stub is needed.
5. MonsterRoom and mob spawning need a block-entity/spawner layer that does not
   exist yet; that is a project of its own.

## Working rules that produced the verified parts

- Translate, never infer. Two guessed tables were written and deleted.
- `+0x18`/`+0x1c` on feature objects are registry **type ids**, not `Block*`.
- `BlockPos::offset(x,y,z)` takes x in `edx`, y in `ecx`, z in `r8d`.
- A breakpoint inside a function GDB itself invoked is unsupported; call the
  function directly as a function oracle instead.
- Oracles never patch feature code, RNG draws or expected results.
- Never commit a partial replay, and audit the tree for violations of that
  rule — two such violations were found late and removed.
