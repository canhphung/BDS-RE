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

## Oracle status: narrowed to `_placeTrunk`

The oracle now has a `TREE_ORACLE_MODE` switch so each stage can be called on
its own. Results:

| Mode | Result |
|---|---|
| `prepare` (`_prepareSpawn` only) | **works**: 16/16 cases, 105–123 dependency answers each |
| `fallen` (`_placeFallenTrunk` only) | **works**: 16/16 cases, 32 Random words, 0 placements |
| `trunk` (`_placeTrunk` only) | **faults** with the same GDB stop |
| `place` (the four-argument body) | faults, because it reaches `_placeTrunk` |

So the suspect is not `_placeFallenTrunk`: it runs cleanly in isolation and
simply returns false on this arena. Reading `_placeTrunk`'s prologue narrows
the fault further:

- `0x1b5ac50`: `_getManuallyPlaced()` reads `this+0x8`, which the
  `Feature::Feature(Actor*)` base sets from its argument; the oracle passes a
  null `Actor*`, so this is false.
- `0x1b5ac6a..0x1b5ac7b`: with that false, the native code draws
  `nextInt(0x50)` and only takes the fallen-trunk branch on zero. Those are the
  first Random draws `_placeTrunk` consumes.
- `0x1b5ae13..0x1b5ae2e`: the placement path builds
  `VanillaBlocks::mLog` with `BlockState::MappedType`, reading the trunk type
  from `this+0x18` — the constructor's second int. That path calls
  `Block::setBlockState<int>` `0x1b4c580` and then `_placeBlock`.

### The abort was the oracle's fault, not the binary's

That guess was right. The `TreeFeature` constructor is
`TreeFeature(Actor*, int leafType, int trunkType, bool)` and the two ints are
**type ids**, not `Block*` pointers. The oracle was passing
`VanillaBlocks::mLog` / `mLeaves` object addresses as those ids, and
`Block::setBlockState<int>` `0x1b4c580` feeds the value straight into
`BlockLegacy::setBlockState<int>(..., unsigned char)` `0x1b4d010` as a state
index. A block pointer truncated to a state index is out of range, which is
what killed the inferior.

Constructing with `leafType = 0`, `trunkType = 0` makes `_placeTrunk` run:
**16/16 cases, 87 real log placements, 40 Random words, 103 `get_block`
answers**, and the same now applies to `place(..., int)`. The corpus is
`tests/reference/tree_place_trunk_bds_1.6.0.15.json`.

This is worth recording as a general lesson for this binary: fields like
`+0x18` / `+0x1c` on feature objects are registry type ids, and passing a
`Block*` where an id belongs produces a crash far from the actual mistake.

### `_placeTrunk` translated and verified

`src/world/tree.cpp` now reproduces all 16 native cases: **87 placements,
40 Random words, 103 dependency answers**, through `tree_trunk_reference`.
Four details the corpus fixed:

- **A non-replaceable cell SKIPS the placement; it does not end the loop.**
  Case 3 reads five cells but places only at the last one. An earlier version
  used `break` and stopped after one cell.
- **There is a vine pass after every log.** `0x1b5ae50..0x1b5af02` offers the
  four horizontal neighbours a vine in the order west, east, north, south, and
  each direction draws `nextFloat()` **first**, only testing the cell when the
  roll is below the trunk-height scale. The data values the corpus records are
  west 8, east 2, north 1, south 4.
- **The trunk-height scale gates the whole vine pass.** `0x1b5ae5a` compares it
  against 0.0f and jumps past every vine when equal, so the `nextFloat` draws
  only happen for a nonzero scale.
- **A block-name interning bug in the first corpus.** The exporter collected
  names from `initial` rows and `writes[3]`, but the compact form stores name
  *indices* in `initial`, so the fixture's `block_names` grew junk entries
  (`':'`, `'d'`, `'e'`). The test then resolved every cell to `air` and the loop
  placed nothing. Fixed by taking names from the raw oracle for `initial` and
  from `final` (a flat name list) instead of the encoded columns.

So `_prepareSpawn` `0x1b59e30` is callable from a paused world; the abort comes
from `_placeTrunk` `0x1b5ac20`. Its corpus is exported as
`tests/reference/tree_prepare_bds_1.6.0.15.json` (16 cases, 12 distinct arenas,
903 `get_block`, 16 `may_place_on`, 8 placements) so the translation can be
written and checked without re-running the oracle.

Reading its prologue precisely:

- `0x1b59e56`: if `origin.y < 1`, return false immediately.
- `0x1b59e6b..0x1b59e8e`: if `origin.y + height + 1 > getMaxHeight()`, return
  false.
- `0x1b59ec4..0x1b59ee2`: `mayPlaceOn` on `origin.below()`; false returns false.
- `0x1b59f07..0x1b5a028`: a `Range` walk over the volume the tree would occupy
  (the three coordinate comparisons at `0x1b59f64`, `0x1b59f87` and `0x1b59fb2`
  bound it), testing `_isFree` per cell.
- `0x1b5a03b..0x1b5a073`: `getMaxHeight` again, then a `BlockPos` for the cell
  being tested.
- `0x1b5a094..0x1b5a0aa`: `_getBlock` then `_isFree`; a non-free cell aborts.
- `0x1b5a146..0x1b5a16a`: on success, `_placeBlock` on `below()` with dirt.

### Translated and verified

`src/world/tree.cpp` now reproduces all 16 native cases: 8 placements, 919
dependency answers, and zero Random draws — `_prepareSpawn` never touches the
stream. Three details the corpus fixed:

- **The Y loop runs `origin.y .. origin.y + height + 1`.** `range_incl`
  (`0x1b3aec0`) stores `end = max + 1` and the native upper bound is
  `origin.y + 1 + height`, so the loop covers `height + 2` layers, not
  `height + 1`. The fixture's last 25 `get_block` rows are the extra top layer.
- **The half-width condition is `>=`, not `<`.** `0x1b59f87` is
  `cmp; jl` to *skip*, so the half-width becomes 2 when
  `y >= origin.y + height - 1`. Reading it as a strict `<` put a 5x5 scan on
  the wrong layers and desynchronised the stream on the second cell.
- **`mayPlaceOn` queries the block at its own position**, so the corpus records
  a nested `get_block` right after each `may_place_on` row; the test consumes
  both to stay aligned. Inside `_placeTrunk` the first suspicious call is
`_placeFallenTrunk` `0x1b5a8a0`, reached at `0x1b5ad4d` right after
`Feature::_getManuallyPlaced` (`0x1b5ac50`, which only reads `+0x8` and cannot
fault) and two `nextInt` draws. `_placeFallenTrunk` uses a static `mask` table
at `0x28338d0` and its own `Range` walk, so it is the next thing to test in
isolation.

## Original first attempt (kept for the reasoning)

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

### Full `place(..., int)` corpus captured

With the constructor type-id fix the four-argument body now runs, so its corpus
is exported as `tests/reference/tree_place_bds_1.6.0.15.json`: 16 cases,
**511 placements** (16 dirt, 47 log, 448 leaves), 144 Random words, 16
`may_place_on` and 1,422 `get_block` answers.

Reading the orchestration `0x1b5a2c2..0x1b5a30a` corrects an easy misreading:

- `_prepareSpawn` is called first; false aborts the whole feature.
- `_placeTrunk` is called next, and a **TRUE** return jumps to
  `0x1b5a30f` — the leaf pass — while a **FALSE** return stores 1 and returns
  immediately (`0x1b5a306..0x1b5a30a`). So "trunk placement failed" makes the
  feature succeed early, which is the opposite of the intuitive reading.

`_placeLeaf` is `0x1b59d60` (not `0x1b4c5e0`, which is a
`FancyTreeFeature` destructor): it fetches the block, asks
`Block::getMaterial` three times through `Material::isType`, then builds the
leaf block with `Block::setBlockState<int>` and writes it. Those three
`isType` answers are what decide whether a leaf cell is replaceable, and they
are the next thing the oracle must record.

`_placeLeaf` `0x1b59d60` reads the cell's block, calls `Block::getMaterial`
once and then asks `Material::isType(9)`, `isType(0)` and `isType(7)` in that
order; any true answer builds the leaf block through
`Block::setBlockState<int>` with `VanillaBlocks::mLeaves` and writes it,
otherwise the cell is skipped. Those three answers are what decide a leaf
cell, and they are what the corpus needs to record.

### Oracle limitation: nested breakpoints stop the inferior

Recording those three answers by breaking at `0x1b59db4`, `0x1b59dc7` and
`0x1b59ddd` (the `test $1,%al` sites) works when `_placeLeaf` is called on its
own, but breaks the full `place(...,int)` run: GDB reports
`The program being debugged stopped while in a function called from GDB`.
Stopping inside a function that is itself being invoked from GDB's
`parse_and_eval` frame is not supported, and `place` calls `_placeLeaf` deep in
its own call tree.

So the dependency stream for the leaf pass cannot be captured the way
`_prepareSpawn` and `_placeTrunk` were. Two workable alternatives for the next
session:

1. **Call `_placeLeaf` per cell from GDB** with a real `BlockSource` and record
   the three answers there, then have `tree_place` replay them. This is a
   function oracle rather than an integration oracle, and it matches how the
   GrassBlock work was done.
2. **Drive the whole tree through the real server** (join a client, let
   `postProcess` run the feature) and record the writes, deriving the leaf rule
   from which cells changed. That needs the postProcess integration first, so
   option 1 is the practical one.

### `_placeLeaf` solved with a standalone function oracle

Option 1 above works: `tools/re/gdb_leaf_cases.py` calls `_placeLeaf`
`0x1b59d60` directly per cell with a real `BlockSource`, so GDB owns the whole
frame and the three `test $1,%al` sites at `0x1b59db4`, `0x1b59dc7` and
`0x1b59ddd` can be observed without the nested-call abort.

Result: **30 cases, 9 placements, 81 `Material::isType` answers**, one case per
block type in the palette (air, stone, dirt, grass, sand, water, leaves, log,
gravel, vine) at three offsets. The decision table the corpus yields:

| Cell block | isType(9) | isType(0) | isType(7) | result | placed |
|---|---|---|---|---|---|
| air | 0 | 1 | not asked | true | yes |
| vine | 1 | not asked | not asked | true | yes |
| leaves | 0 | 0 | 1 | **false** | yes |
| stone, dirt, grass, sand, water, log, gravel | 0 | 0 | 0 | false | no |

So `_placeLeaf` places when the cell's material is type 9 (plant), 0 (air) or 7
(leaves), testing in that order and stopping at the first true answer.

Two details that reading alone got wrong:

- **The return value is `_placeBlock`'s bool, not "did it place".**
  `0x1b59e1e..0x1b59e26` returns `%al` from the write. Placing leaves on an
  existing leaves cell therefore reports **false** even though the attempt ran,
  which the `leaves:0` row records. Returning `true` unconditionally failed
  exactly that case.
- **The trio short-circuits**, so the corpus stores `-1` for calls the original
  never reached. A replay that evaluates all three unconditionally desynchronises
  the answer stream.

`tests/reference/tree_leaf_bds_1.6.0.15.json` is 3 KB, and
`tree_leaf_reference` checks all 30 cases.

## Leaf volume: shape measured, then solved

Superseded by "The gate rule, solved" below, which gives the final rule; the
measurements here are the evidence that led to it.

The full `place(...,int)` corpus gives the leaf volume exactly. For height 5 at
origin `(-8,5,-8)` the 59 leaf cells are, relative to the origin:

| rel-Y | count | cells |
|---:|---:|---|
| 2 | 24 | the full 5x5 **minus the trunk cell (0,0)** |
| 3 | 23 | 5x5 minus (0,0) and three corners |
| 4 | 7 | (-1,0), (-1,1), (0,-1), (0,1), (1,-1), (1,0), (1,1) |
| 5 | 5 | (-1,0), (0,-1), (0,0), (0,1), (1,0) |

So the band is `origin.y + 2 .. origin.y + height`, the half-width is
`1 - (y - (origin.y + height)) / 2` with C truncation, the square corners are
cut, and the trunk column is never a leaf. A pass built from those rules
produces exactly 59 cells — but `nextInt(2)` still disagrees on 3 of them.

The gate at `0x1b5a4f2..0x1b5a50f` is:

```
nextInt(2); if (result == 0) jump 0x1b5a551   ; skip the cell
if (-0x58 == 0) jump 0x1b5a50f                ; place
jump 0x1b5a551                                ; skip
```

with `-0x58 = y - (origin.y + height)`, so it is zero only on the top layer.
Reading it as "place when the draw is nonzero and we are not on the top layer"
under-places (32 cells); "place whenever the draw is nonzero" over-places (37).
Getting the top-layer branch right needs the recorded per-cell draws, which
the corpus does not keep: the reverted implementation is not committed.

### Branch targets corrected

Reading the jump destinations rather than just the compare clarifies the gate
considerably, and corrects an earlier note in this file:

- `0x1b5a4cf` and `0x1b5a4e3` jump to **`0x1b5a50f`, which is the `_placeLeaf`
  call** (confirmed by following it: `0x1b5a50f..0x1b5a54c` builds the
  `BlockPos` and calls `0x1b59d60`). So a cell on the diamond edge
  (`|dx| == half` or `|dz| == half`) is placed **unconditionally**, without a
  Random draw.
- Only interior cells reach `nextInt(2)` at `0x1b5a4f2`; a zero draw jumps to
  `0x1b5a551`, which is the loop increment (skip).

Measured against those rules the full pass produces **68 cells** at height 5
(`25, 25, 9, 9` per layer) where the corpus records **59** (`24, 23, 7, 5`).
The differences are `-1, -2, -2, -4`: the top layer loses the most, so the
`-0x58` test at `0x1b5a500` clearly drops cells on the top layer, and one
further interior cell is dropped on every layer.

Both readings were tried: "skip when the draw is zero" gives 62 cells
(`23, 23, 8, 8`) and "never skip" gives 68. The corpus cannot separate them
because it does not record which cell each `nextInt(2)` belonged to, and the
per-cell draw assignment is exactly what is missing.

### The gate decision data is now recorded

The oracle logs, for every `nextInt(2)` at `0x1b5a4f2`: the candidate cell from
the `BlockPos` locals (`-0x84` x, `-0x54` y, `-0xac` z, loaded into
`esi`/`edx`/`ecx` at `0x1b5a51a/520/523`), the draw at `0x1b5a4f7`, whether
`_placeLeaf` at `0x1b5a54c` was **called**, and its **return value** read at
`0x1b5a551`.

`tests/reference/tree_leaf_gates_bds_1.6.0.15.json` holds **128 gates across
16 cases** (8 KB).

### Half-width formula, corrected

The formula recorded earlier in this file was wrong. Measuring the native leaf
extent per layer gives `2, 2, 1, 1` for heights 5, 6 **and** 7:

| height | rel-Y band | half-widths |
|---:|---|---|
| 5 | 2..5 | 2, 2, 1, 1 |
| 6 | 3..6 | 2, 2, 1, 1 |
| 7 | 4..7 | 2, 2, 1, 1 |

So the band always starts at `height - 3` and the half-width is

```
half = 2 - trunc((relY - (height - 3)) / 2)
```

with **truncating** division, which reproduces all three rows. The earlier
`1 - (y - (origin.y + height)) / 2` was an arithmetic coincidence for the first
layer only.

### The gate rule, solved

The 128 recorded gates plus the native leaf sets pin the rule down completely.
`src/world/tree.cpp` now reproduces **all 8 leaf volumes: 448 leaf cells**,
through `tree_place_leaves`.

The pass is:

1. Band `origin.y + height - 3 .. origin.y + height` (`0x1b5a313..0x1b5a326`).
2. Half-width `2 - rel/2` with truncation, which gives `2, 2, 1, 1` for every
   height — the earlier `1 - (y - (origin.y + height)) / 2` was wrong.
3. The **trunk column `(0,0)` is skipped on every layer except the top**, where
   the native set keeps it. This was the missing piece: with it the counts per
   layer become `24, 23, 7, 5` exactly.
4. Any cell that is **not a corner** (`|dx| == half && |dz| == half`) goes
   straight to `_placeLeaf` — `0x1b5a4cf` / `0x1b5a4e3` jump to `0x1b5a50f`,
   the call site, with **no Random draw**.
5. Corners draw `nextInt(2)` (`0x1b5a4f2`). A zero draw skips
   (`0x1b5a4fa` jumps to the increment); a nonzero draw also skips the **top
   layer** because `0x1b5a500` tests `y - (origin.y + height)`, which is zero
   there.

Verified counts for height 5 at `(-8,5,-8)`: `24, 23, 7, 5` = 59 cells, the
same per-layer numbers the native run produced.

The lesson worth keeping: two earlier attempts failed because they modelled the
draw as gating *all* cells and because the half-width formula was an arithmetic
coincidence on the first layer. The recorded per-gate corpus is what made the
corner-only rule and the top-layer trunk exception visible.

## `tree_place` complete: all 16 cases match

`tree_place` chains the four verified components and reproduces the whole
native corpus: **16 cases, 511 placements, 144 Random words and every
dependency answer**, through `tree_place`.

The orchestration, read from `0x1b5a2c2..0x1b5a884`:

1. `_prepareSpawn`; false returns false.
2. `_placeTrunk`; a **false** return stores 1 and returns **true** at once, so
   a failed trunk placement makes the feature succeed early.
3. otherwise the leaf volume, then the jungle/cocoa decoration stage.
4. **`_placeBlock(dirt)` at `origin.below()` and return true** — the native tail
   at `0x1b5a834..0x1b5a884`. `_prepareSpawn` already wrote dirt there, so the
   corpus records that cell **twice**; the duplicate is deliberate, and it was
   the one write the earlier 65/66 attempt missed.

## Plan for the session that translates it## Plan for the session that translates it

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
