# TreeFeature family — trace evidence and translation plan

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

Status: **traced, not translated, no corpus yet.** Recorded so the next
session can go straight to the oracle instead of re-reading the binary.

## Methods in `TreeFeature` and their sizes

| Method | Address | Size |
|---|---|---:|
| `TreeFeature(Actor*, int, int, bool)` | `0x1b59cf0` | 0x61 |
| `_isFree(Block const&)` | `0x1b5b790` | 0x91 |
| `_isFree(Material const&)` | `0x1b5b760` | 0x49 |
| `_getLeafType()` | — | 0x11 |
| `_getTrunkType()` | — | 0x11 |
| `_prepareSpawn(BlockSource&, BlockPos const&, int)` | `0x1b59e30` | 0x35e |
| `_placeTrunk(BlockSource&, BlockPos const&, Random&, int)` | `0x1b5ac20` | 0x4f2 |
| `_placeLeaf(BlockSource&, BlockPos const&)` | `0x1b4c5e0` | 0xcc |
| `_placeFallenTrunk(BlockSource&, BlockPos const&, Random&, int)` | — | 0x37f |
| `_addVine(BlockSource&, BlockPos const&, int)` | — | 0x104 |
| `_addCocoaPlants(BlockSource&, BlockPos const&, Random&, int)` | — | 0x218 |
| `_addJungleFeatures(BlockSource&, BlockPos const&, Random&)` | — | 0x225 |
| `place(BlockSource&, BlockPos const&, Random&)` | `0x1b5a230` | 0x5d |
| `place(BlockSource&, BlockPos const&, Random&, int)` | `0x1b5a290` | 0x60f |

Total ≈ 0x1300 bytes: one focused session of oracle + translation, not a
sub-task.

## Entry points read so far

**`place(..., Random&)` `0x1b5a230`** is a thin overload: it draws
`nextInt(3) + 5` (`0x1b5a25e..0x1b5a266`) and forwards to
`place(..., int)` with that height. So the tree height for a plain
`TreeFeature` is 5..7, and every subclass that overrides only the 3-argument
entry still funnels through the 4-argument one.

**`place(..., int)` `0x1b5a290`** is the real body. Its call graph:

| Calls | Target |
|---:|---|
| 1 | `_prepareSpawn` `0x1b59e30` |
| 1 | `_placeTrunk` `0x1b5ac20` |
| 1 | `_placeLeaf` `0x1b4c5e0` |
| 1 | `_addJungleFeatures` |
| 1 | `_addCocoaPlants` |
| 1 | `Feature::_placeBlock` |
| 2 | `Random::nextInt(int)` |
| 2 | `abs` |

plus 36 int-argument helper calls that are the `Range<int,1>` iteration the
other features also use.

The observed order in the disassembly is `_prepareSpawn` first (returning
false aborts the whole placement), then `_placeTrunk`, which returning true
ends the feature early, then the leaf/decoration stage.

**`_prepareSpawn` `0x1b59e30`** dependencies:

| Calls | Target |
|---:|---|
| 2 | `BlockSource::getMaxHeight` |
| 1 | `Block::mayPlaceOn` `0x1e6a540` |
| 1 | `Feature::_getBlock` |
| 1 | `TreeFeature::_isFree(Block const&)` |
| 1 | `Feature::_placeBlock` |

So it checks the column against the world height, requires the block below to
accept placement (`mayPlaceOn` on `below()` at `0x1b59ec4..0x1b59ee2`), then
walks a `Range` of cells testing `_isFree`.

**`_placeTrunk` `0x1b5ac20`** dependencies:

| Calls | Target |
|---:|---|
| 5 | `Feature::_placeBlock` |
| 4 | `Random::nextFloat` |
| 4 | `Feature::_isEmptyBlock` |
| 3 | `BlockLegacy::operator==` |
| 3 | `Block::getLegacyBlock` |
| 2 | `Random::nextInt(int)` |
| 1 | `_placeFallenTrunk` |
| 1 | `Feature::_getManuallyPlaced` |

The four `nextFloat` draws and the fallen-trunk branch are the parts a corpus
must pin down; a reading cannot tell which draw feeds which axis.

## Oracle status: blocked by a native abort, first attempt

`tools/re/gdb_tree_cases.py` is written but does **not** complete. Calling
`place(...,int)` `0x1b5a290` from GDB stops the inferior:

```
gdb.error: The program being debugged stopped while in a function called from GDB.
```

That is not a GDB defect: the tree body reaches a path that faults or aborts
when invoked out-of-band. Two candidate causes, both testable:

1. `_prepareSpawn` reads `BlockSource::getMaxHeight` and then walks a `Range`
   derived from the constructor's fields; if the arena in the oracle does not
   satisfy a precondition the native code assumes (for example a chunk that is
   actually loaded at the queried column), it dereferences something invalid.
2. `place(...,int)` may construct or use `ChunkPos`/`BlockVolume` state that
   only exists during real chunk generation, which a paused world does not
   have.

The vegetation, lake, ore, clay and bonus-chest oracles all call their
features successfully in the same harness, so the fault is specific to the
tree path rather than the setup. The next session should narrow it by calling
the sub-methods individually (`_prepareSpawn` alone, then `_placeTrunk` alone)
and watching which one faults, instead of calling `place` as a unit.

## Plan for the session that translates it

1. `gdb_tree_cases.py` following the vegetation/clay oracle shape: construct a
   `TreeFeature` with its real constructor `0x1b59cf0` and call
   `place(..., Random&, int)` with explicit heights, over arenas that vary
   ground block, free space above, and obstruction. Record every
   `_placeBlock`, every Random word, and every dependency answer from
   `_getBlock` / `_isEmptyBlock` / `mayPlaceOn` / `getMaxHeight` / `_isFree`.
2. Translate in dependency order: `_isFree`, `_prepareSpawn`, `_placeTrunk`,
   `_placeLeaf`, then `place(...,int)` and the thin 3-arg overload.
3. Only after the base class replays exactly should
   `FancyTreeFeature` / `SwampTreeFeature` / `SavannaTreeFeature` /
   `JungleTreeFeature` / `MegaTreeFeature` / `MegaPineTreeFeature` /
   `MegaJungleTreeFeature` / `RoofTreeFeature` be attempted; they override
   `place` and rely on the same helpers.

## Why this is the right next component

`OverworldDecorator::decorate` is mostly a dispatch table into the tree, marine
and snow features (see [DECORATE.md](DECORATE.md)). The tree family is the
largest single consumer, so translating it unblocks both the decorator and the
`postProcess` integration that step 5 needs.
