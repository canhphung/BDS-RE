# OverworldGenerator::postProcess — orchestration evidence

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.
Entry point `OverworldGenerator::postProcess(ChunkViewSource&)` `0x1b38950`
(size 0xf19).

This file records the **complete control flow** read from the disassembly. It is
static evidence, not a runtime capture: the per-feature components
(OreFeature, LakeFeature) already have their own native oracles; what was
missing and is recorded here is the orchestration order and every Random draw /
seed change that a faithful `postProcess` must reproduce.

Status: **evidence only**. No orchestration code is registered yet, so nothing
here is claimed as implemented. Phase 3 counters in `TERRAIN.md`/`DECORATION.md`
stay unchanged.

## Structure

| Native region | Behaviour |
|---|---|
| `0x1b3896e..0x1b38a1a` | `ChunkViewSource::getArea()` → `GridArea::getBounds()`; the chunk at bounds `+1,+1` is taken; `PostprocessingManager::tryLock(ChunkPos)`; on failure the whole function returns early (`0x1b39838`). |
| `0x1b38a1f..0x1b38a7a` | `enableBlockEntityAccess()`; `BlockSource(Level&, Dimension&, ChunkSource&, bool, bool)` constructed with `r8d = 0` (fire events) and `r9d = 1`; `Tick(1)` + `BlockTickingQueue(Tick)` attached via `BlockSource::setTickingQueue`. |
| `0x1b38ac9..0x1b38b34` | `LevelChunk::getMax()` → `BlockSource::getBiome(BlockPos const&)` → `Biome +0xf0` → `ThreadLocal<OverworldGenerator::ThreadData>::getLocal()`; the thread data's `+0x41810` pair (two ints) is shifted left by 4 and becomes the `BlockPos` used as the decoration origin. |
| `0x1b38b5e..0x1b38bff` | First `Random::setSeed(Level::getSeed())`, then **two `Random::nextInt()` draws**: `a = nextInt()/2*2+1`, `b = nextInt()/2*2+1`. Then `setSeed((origin.x * a + origin.z * b) ^ Level::getSeed())`. This is the "reseeds its ThreadData Random again" step noted in the handoff. |
| `0x1b38c04..0x1b38c49` | Biome gate: `Biome::operator!=` against `VanillaBiomes::mDesert` then `mDesertHills`; **equal to either → skip the water lake**. |
| `0x1b38c54..0x1b38d51` | Water lake: `nextInt(4)`, and only on a **zero** draw continue. `nextInt(3, 12)` → Z offset, `nextInt(128)` → Y, `nextInt(3, 12)` → X. `if (Y < 0x3c)` then also require generator flag `+0x48` clear (`0x1b38cca`), else skip. `LakeFeature(VanillaBlocks::mStillWater)` then `place(BlockSource, origin.offset(x, y, z), Random)`; on a true return the `water lake placed` flag is set. |
| `0x1b38d5b..0x1b38e9b` | Lava lake, **only when the water lake flag is clear and generator `+0x48` is clear**: `nextInt(8)` == 0 → `nextInt(3,12)` → Z, `nextInt(0x78) + 8` → Y, `nextInt(Y)` → X, `nextInt(3,12)` → `w`. Then `if (w < 0x3c) skip`; if `w >= 0x40` require `nextInt(10) == 0`. `LakeFeature(VanillaBlocks::mStillLava)` placed at `origin.offset(z, w, x)`. |
| `0x1b38ea5..0x1b38f5b` | Before decoration: `setSeed(Level::getSeed())`, two `nextInt()` → `a`, `b`, then `setSeed(((origin.x * a) + (origin.z * b)) ^ Level::getSeed())`. This repeats the seed derivation of `0x1b38b5e` with the same formula on freshly drawn values. |
| `0x1b38f60..0x1b38fb9` | Virtual call through the generator's `+0x158` member (slot `+0x20`), passing `(BlockSource&, BlockPos const&, int x, int z, <something>)`, i.e. the decorator dispatch. |
| `0x1b38fca..0x1b393bb` | Further virtual decorator dispatches through `+0x440`, `+0x238`, `+0x360` members; each is `mov member; call *slot`. Each reads the same origin x/z from `-0x48(%rbp)`. |
| `0x1b391f4..0x1b392a0` | Three `nextInt` draws then `MonsterRoomFeature::place` at `0x1b392a0`. |
| `0x1b3947b` | `BonusChestFeature::place` `0x1b3947b`. |
| `0x1b394e1..0x1b39739` | Final `setSeed`; `BlockSource::setBlock` at `0x1b396ec` and `0x1b39739` — the pending-tick / gravel settling pass over the block ticking queue. |

## What a faithful orchestration must reproduce

1. The `tryLock` early return (no decoration when another thread owns the chunk).
2. `BlockSource(level, dimension, source, false, true)` and `setTickingQueue(Tick(1))`.
3. The decoration origin from the biome's `ThreadData +0x41810` pair, not the chunk position.
4. The **two seed derivations**, each of which consumes exactly two `nextInt()`
   draws from a Random seeded with the level seed. These draws are part of the
   level-visible Random stream in the original only through this private
   ThreadData Random, so they must not be confused with `Level::getRandom()`.
5. Desert/DesertHills skipping the water lake.
6. Water lake draw order: `nextInt(4)` → gate, `nextInt(3,12)`, `nextInt(128)`,
   `nextInt(3,12)`, with the `Y < 60` and generator-flag conditions.
7. Lava lake only if the water lake did not place: `nextInt(8)` → gate,
   `nextInt(3,12)`, `nextInt(120)+8`, `nextInt(Y)`, `nextInt(3,12)`, and the
   `w >= 64 → nextInt(10)==0` condition.
8. Then the decorator dispatches, MonsterRoomFeature, BonusChestFeature, and
   finally the pending-tick pass.

## Why this is not yet registered

The functions above are read, but three of them are still missing their own
translations and native corpora: `MonsterRoomFeature::place` `0x1b54...`,
`BonusChestFeature::place` `0x1b526b0`, and the decorator object graph behind
`+0x158` / `+0x238` / `+0x360` / `+0x440`. Registering the orchestration with
those stubs would silently produce a different Random stream than the original,
which is exactly the failure mode the existing corpora are designed to catch.
The next slice should translate `BonusChestFeature` and `MonsterRoomFeature`
first (both small, both self-contained), then `OverworldDecorator::decorate`
`0x1da5250`, then wire the orchestration.

## Phase 4 structures — not started

`OverworldGenerator` also owns structure generation (`StructureFeature`,
`StructureStart`, `StructurePiece` families). None of it has been traced.
Village, mineshaft, stronghold, desert/jungle temple, witch hut, ocean
monument, shipwreck, underwater ruin, mansion and igloo are all still at
"symbols located only".

## Seed dependency

All of the above consumes `Level::getSeed()`. That value now comes from
`LEVELSettings::parseSeedString`; see [SEED.md](SEED.md). A one-character
`level-seed` therefore randomises every draw above, which is why the terrain
corpora pin a per-run seed read from `level.dat`.
