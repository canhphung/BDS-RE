# BonusChestFeature and MonsterRoomFeature — trace evidence

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

These are the two features `OverworldGenerator::postProcess` calls directly
(`0x1b3947b` and `0x1b392a0`) besides the decorator dispatches. See
[POSTPROCESS.md](POSTPROCESS.md) for the call context.

Status: **traced, not translated, no corpus yet.** Nothing is registered.

## BonusChestFeature

`place(BlockSource&, BlockPos const&, Random&) const` `0x1b526b0` (0x219 bytes),
three helpers: `::$_0` lambda `0x1b528d0`, `_place` `0x1b529a0`,
`_getFacingDataId` `0x1b52d70`. Default constructor `0x1b3b760`.

### `place` 0x1b526b0

1. `Random::getSeed` (`0x1b52fc0`) then constructs a **second local Random**
   seeded with it (`0x187e350`). Every later draw in this feature comes from
   that copy, not from the caller's Random. This is the same
   "own Random for the feature" pattern the mob code uses, and it means the
   caller's stream is untouched.
2. Two nested loops over `-0xa1c` and `-0xa20`, each `0x3 .. 0xa` inclusive
   (`cmpl $0xa` / `jle`): a 7x7 grid of attempts around the origin.
3. For each grid cell the `$_0` lambda is called for the four corner
   combinations in this order: `(+dx, +dz)`, `(-dx, -dz)`, `(+dx, -dz)`,
   `(-dx, +dz)` (`0x1b52742`, `0x1b52792`, `0x1b527e2`, `0x1b52832`). A `true`
   return stops both loops and makes `place` return true.

### `$_0` 0x1b528d0

`BlockPos(x, origin.y, z)` → `BlockSource::findNextTopSolidBlockUnder(BlockPos&)`
`0x1c61410`. On success it increments the target's Y by one
(`0x1b52927..0x1b52931`), calls `_place`, and on success calls
`Level::getLevelData()` → `LevelData::setBonusChestSpawned(true)`
(`0x1cd1fe0`).

### `_place` 0x1b529a0

1. `BlockSource::isEmptyBlock` on the target, else return false
   (`0x1b529ce..0x1b529db`).
2. `_getFacingDataId(region, target, 0, 0)` for the chest facing, then
   `Block::getBlockStateFromLegacyData` (`0x1b281c0`) and `Feature::_placeBlock`
   with `VanillaBlocks::mChest` (`0x1b529ff..0x1b52a56`).
3. `BlockSource::getBlockEntity` (`0x1c5b8e0`); if one exists,
   `RandomizableBlockActorContainerBase::setLootTable` (`0x1f015e0`) with the
   literal at `0x283346c`, which reads
   `loot_tables/chests/spawn_bonus_chest.json`, and the **local** Random's
   `nextInt()` (`0x1b52aa9`) as the seed argument.
4. Four torches. The candidate positions are built by walking the target
   east→west→north→south through the `BlockPos` helpers at
   `0x1b52ada..0x1b52b2b`, and each is tested in the order
   `(-0x80)`, `(-0x68)`, `(-0x98)`, `(-0xb0)`: `isEmptyBlock`, then
   `BlockSource::isConsideredSolidBlock` (`0x1c5af40`) on the cell below, then
   `_placeBlock(VanillaBlocks::mTorch)`. Note the first checked slot is
   `-0x80` (west) even though `-0x78`/`-0x68` were produced first; the
   positions are stored and the checks run in the order above
   (`0x1b52b3e`, `0x1b52bc1`, `0x1b52c40`, `0x1b52cff`).

### `_getFacingDataId` 0x1b52d70

Arguments are `(BlockSource&, BlockPos const&, int a /*ecx*/, int b /*r8d*/)`.
Returns a legacy data value: `2` when `b > 0` and the `+z` neighbour is
non-empty; `3` when `b < 0` and the `-z` neighbour is non-empty; `4` when
`a > 0` and the `+x` neighbour is non-empty; `5` when `a < 0` and the `-x`
neighbour is non-empty; and finally `2` when none matched
(`0x1b52fa4`). `place` always passes `0, 0`, so the chest always takes data
value 2 in this feature.

## MonsterRoomFeature

`place` `0x1b549a0` (0x995 bytes) plus `_getFacingDataId` `0x1b55340`,
`_getRandomEntityId`, default constructor `0x1b54960`.

Dependency survey from the disassembly (call counts inside `place`):

| Calls | Dependency |
|---:|---|
| 7 | `BlockSource::getMaterial` |
| 8 | `Material::isSolid` |
| 5 | `Random::nextInt(int)` |
| 4 | `Feature::_placeBlock` |
| 3 | `BlockSource::isEmptyBlock` |
| 2 | `BlockSource::removeBlock` |
| 2 | `BlockSource::getBlockEntity` |
| 1 | `Random::nextInt()` |
| 1 | `MonsterRoomFeature::_getRandomEntityId` |
| 1 | `MonsterRoomFeature::_getFacingDataId` |
| 1 | `MobSpawnerBlockActor::getSpawner` |

Structure read so far: it starts with two constants (`0x2` at `0x1b549ab` and
`0x3` at `0x1b549c4`) that look like a minimum and a loop bound, then walks a
room shell testing materials, removes blocks, places the mossy-cobble room and
chests, and fills a `MobSpawnerBlockActor` with a random entity id.

## Why this is not translated yet

`BonusChestFeature` is small enough to finish in one pass: it needs
`findNextTopSolidBlockUnder`, `isConsideredSolidBlock`, `getBlockEntity`,
`setLootTable` and the block-state-from-legacy-data conversion, plus an oracle
that constructs the feature and calls `place`. Its local-Random detail means the
corpus must record that second stream separately.

`MonsterRoomFeature` additionally needs the block entity and spawner surface
(`MobSpawnerBlockActor`, `BaseSpawner`, entity-id selection), which the project
has not translated at all. Wiring it before that exists would mean inventing the
spawner behaviour, so it should follow the block-entity work rather than lead it.

## Next

1. Write `gdb_bonus_chest_cases.py` following the vegetation oracle pattern and
   translate `src/world/bonus_chest.cpp` against it.
2. Trace `MobSpawnerBlockActor`/`BaseSpawner` before attempting
   `MonsterRoomFeature`, then give it its own oracle.
3. Then proceed to `OverworldDecorator::decorate` `0x1da5250`.
