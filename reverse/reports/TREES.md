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

## Tree subclasses: constructor table (all eight, read from the binary)

Each constructor calls `TreeFeature::TreeFeature(Actor*, int leafType, int
trunkType, bool) 0x1b59cf0`, whose ints land in `+0x18` (leafType) and `+0x1c`
(trunkType). Values below are the actual register arguments.

| Subclass | ctor | leafType | trunkType | Extra fields |
|---|---|---:|---:|---|
| `JungleTreeFeature` | `0x1b65d60` | 3 | 1 | — |
| `SavannaTreeFeature` | `0x1b51170` | 0 | 0 | — |
| `SwampTreeFeature` | `0x1b6bc60` | 0 | 0 | — |
| `RoofTreeFeature` | `0x1b5b790` | 1 | 1 | — |
| `FancyTreeFeature` | `0x1b4af60` | 0 | 0 | `+0x24 = 0xc`, `+0x28/%xmm2 = 0.618`, `+0x2c/%xmm1 = 0.381`, `+0x30/%xmm0 = 1.0` |
| `MegaTreeFeature` | `0x1b55bf0(Actor*,int,int,int,int)` | 2 | 2 | `+0x24 = arg2`, `+0x28 = arg3`, `+0x2c = arg4`, `+0x30 = arg5` |
| `MegaJungleTreeFeature` | `0x1b53380(Actor*,int,int,int,int)` | forwards all to MegaTree | | |
| `MegaPineTreeFeature` | `0x1b4d6d0(Actor*,bool)` | via MegaTree `edx=0xd` | `ecx=0xf` | `r8d=0x1`, `r9d = bool` |

The three Fancy floats were read from rodata: `0x281c5c8 = 1.0`,
`0x2833350 = 0.381`, `0x2833354 = 0.618`.

### What the type ids actually do — they change the block STATE

This was measured, not assumed. The existing `jungle_tree` corpus was captured
with `leafType = trunkType = 0` (the script hardcoded them), so it contains only
`log:0` and `leaves:0`. Re-running the same oracle with Jungle's REAL
constructor values (`0x1b65d60`, leafType 3, trunkType 1) produces a different
world entirely:

| Corpus | writes | `block_names` |
|---|---:|---|
| old (`leaf=0, trunk=0`) | 523 | `log:0`, `leaves:0` |
| real (`leaf=3, trunk=1`) | **935** | `log:3`, `leaves:3`, `cocoa:1/2/3/5`, `vine:1/2/4/8` |

So the type ids select the wood/leaf variant (jungle wood) and the cocoa/vine
decoration follows from it. Any subclass translation MUST use its real
constructor ids, and the old `jungle_tree` fixture is a fixture for the BASE
feature, not for `JungleTreeFeature`.

This also means `jungle_tree_place` in `src/world/tree.cpp` currently replays
the wrong corpus: it is verified against leaf/trunk 0. The function itself is
fine — it takes `trunk`/`leaves` as `TreeBlock` arguments, so data 3 flows
through — but the TEST passes data 0 and the fixture was captured with 0. The
fix is to re-export the fixture captured with the real ids and pass
`TreeBlock{"log", 3}` / `TreeBlock{"leaves", 3}` in the test.

### Oracle template for all eight subclasses

`tools/re/gdb_tree_subclass_cases.py` and
`tools/re/export_tree_subclass_reference.py` replace the per-subclass copies.
They are driven by environment variables:

| Variable | Meaning |
|---|---|
| `TREE_SUBCLASS_NAME` | label written into the fixture |
| `TREE_OUT_NAME` | output file stem (`jungle` -> `jungle.json`) |
| `TREE_CTOR` | constructor address |
| `TREE_CTOR_MODE` | `base` (Actor*,leaf,trunk,bool) or `mega` (Actor*,int,int,int,int) |
| `TREE_CTOR_LEAF`, `TREE_CTOR_TRUNK` | base type ids (base mode) |
| `TREE_CTOR_EXTRA` | four ints for `+0x24/+0x28/+0x2c/+0x30` (mega mode) |
| `TREE_PLACE` | `place(BlockSource&, BlockPos const&, Random&)` address |

A run with the defaults reproduces the base `TreeFeature` shape; a run with
Jungle's real ids produced the 935-write corpus above.

### JungleTreeFeature is NOT a thin wrapper after all

With the real ids the corpus diverges at query 124, and the queries just before
it show why: `#118..#122` read five consecutive cells `(-6,13,-10)` to
`(-6,13,-6)` — a vertical vine run hanging down a column — while the base
`tree_place_trunk` vine pass only offers the four horizontal neighbours of each
log with fixed data 8/2/1/4.

So `JungleTreeFeature::place` does more than draw a height: it places jungle
wood (`log:3`, `leaves:3`) and hangs vines down from the canopy. The 523-write
corpus that was taken for it hid this because it ran with type ids 0.

That makes `jungle_tree_place` in `src/world/tree.cpp` incorrect as written: it
delegates straight to `tree_place`. The Jungle body needs its own trace, the way
Savanna and Swamp do.

### MegaTreeFeature: also not a thin wrapper — measured

`tools/re/gdb_tree_subclass_cases.py` with `TREE_CTOR_MODE=mega`,
`TREE_CTOR=0x1b55bf0`, `TREE_CTOR_EXTRA=13,15,1,0` and `TREE_PLACE=0x1b55c70`
produced `tests/reference/mega_tree_bds_1.6.0.15.json`: **16 cases, 510 writes,
152 Random words**, with blocks `log:2` and `leaves:2` — the constructor's type
id 2 does change the wood, as predicted.

Replaying it through `tree_place` diverges at query 48. The corpus shows why:
`#42..#47` read a 2x3 block of cells and `#48..#52` read five consecutive cells
`(-10,11,-10)` to `(-10,11,-6)`, and the write groups are a 2x2 trunk (four
columns of equal height) plus a wide flat canopy of single-block columns — not
the base feature's single-column trunk with an octagonal dome.

So `MegaTreeFeature::place` delegates to the base only after
`_prepareTree`/`_checkIsFree`/`_makeDirtFloor` and it uses
`_placeDoubleTrunkLeaves`, all of which are still untranslated. `mega_tree_place`
as written (height + `tree_place`) is therefore wrong, and the fixture is kept
unregistered until the real body exists.

### Correction: MegaTree IS a wrapper — the difference is the leafType branch

Reading `MegaTreeFeature::place 0x1b55c70` again settles it: it is exactly
`nextInt(3) + 5` (`0x1b55c9b..0x1b55ca3`) followed by a non-virtual call to
`TreeFeature::place 0x1b5a290`. The height does NOT read `this+0x24`; that is
`_calcTreeHeigth 0x1b55ce0`, which the MegaPine/MegaJungle subclasses use.

So the earlier "not a wrapper" conclusion was wrong. The real difference is
inside the BASE: `_placeTrunk 0x1b5ac96` compares `this+0x18` (leafType) with 2
and, when equal, skips the trunk-height-scale computation entirely
(`xorps`/`movss 0.0f` then `jmp 0x1b5ad2c`). With leafType 2 the scale is 0, so
the trunk loop bound differs and the canopy is much wider.

That explains the corpus: its writes are a single trunk column at the origin
(y 4..12) plus a wide flat canopy at y 9..12, i.e. the base shape with a
different scale — not a different body at all.

`place` is therefore correctly implemented as `nextInt(3)+5` + `tree_place`, and
the leafType id is now threaded through: `tree_place_trunk` takes a `leaf_type`
argument and skips the scale draw when it is 2.

With that, `mega_tree` replays **16 cases, 510 placements, 152 Random words and
1428 dependency answers** exactly. Both height helpers are split correctly
(`mega_tree_height` for `place`, `mega_tree_calc_height` for `_calcTreeHeigth`).
MegaTreeFeature is wired into the runtime alongside the base and Jungle
features, rotating one of three, and a generated chunk around `(-3,-3)` of seed
123 places one MegaTree each.

### What the type ids do mechanically

`Block::setBlockState<int>` `0x1b4c580` passes the value to
`BlockLegacy::setBlockState<int>(..., unsigned char)` as a **state index** on a
block that is already fixed (`VanillaBlocks::mLog` for the trunk,
`VanillaBlocks::mLeaves` for the leaf). So `leafType`/`trunkType` select a
block STATE variant, not a different block.

Both corpora captured so far (`tree_place`, `jungle_tree`) contain only
`log:0` and `leaves:0`, so the variants do not surface there. A subclass whose
type id is non-zero on a block with real state data (Roof leaf=1, Jungle
leaf=3) is where this would show, and the corpus will record it — the exporters
already carry the full `name:data` string.

## Tree subclasses: the two thin wrappers

Two of the eight subclasses are 0x65-byte wrappers that only choose a height and
delegate to the base `place`:

| Subclass | `place` | Height | Constructor |
|---|---|---|---|
| `JungleTreeFeature` | `0x1b65db0` | `nextInt(7) + 4` (`0x1b65ddb..0x1b65de0`) | `0x1b65d60`, leafType 3 / trunkType 1 |
| `MegaTreeFeature` | `0x1b55c70` | `nextInt(3) + 5` (`0x1b55c9b..0x1b55ca0`) | `0x1b55bf0`, four ints |

`JungleTreeFeature` is translated and verified: `jungle_tree_place` draws the
height and calls the verified base, and `jungle_tree` replays the native corpus
captured THROUGH the subclass — **16 cases, 523 placements, 160 Random words**.
That also re-verifies the whole base path on a second independent corpus.

`MegaTreeFeature`'s height wrapper is translated (`mega_tree_height`) but its
constructor takes four type ids that map to block pairs this project has not
assembled, so it is not wired into a test.

The other six (`FancyTreeFeature` `0x1b4aff0`, `SwampTreeFeature` `0x1b6bcb0`,
`SavannaTreeFeature` `0x1b511c0`, `MegaPineTreeFeature` `0x1b4d740`,
`MegaJungleTreeFeature` `0x1b533e0`, `RoofTreeFeature` `0x1b5b7e0`) have real
bodies (0x4c3 to 0xea4 bytes) with their own leaf/trunk placement and are still
untranslated.

## Tree subclasses: constructor table (all eight, read from the binary)

Each constructor calls `TreeFeature::TreeFeature(Actor*, int leafType, int
trunkType, bool) 0x1b59cf0`, whose ints land in `+0x18` (leafType) and `+0x1c`
(trunkType). Values below are the actual register arguments.

| Subclass | ctor | leafType | trunkType | Extra fields |
|---|---|---:|---:|---|
| `JungleTreeFeature` | `0x1b65d60` | 3 | 1 | — |
| `SavannaTreeFeature` | `0x1b51170` | 0 | 0 | — |
| `SwampTreeFeature` | `0x1b6bc60` | 0 | 0 | — |
| `RoofTreeFeature` | `0x1b5b790` | 1 | 1 | — |
| `FancyTreeFeature` | `0x1b4af60` | 0 | 0 | `+0x24 = 0xc`, `+0x28/%xmm2 = 0.618`, `+0x2c/%xmm1 = 0.381`, `+0x30/%xmm0 = 1.0` |
| `MegaTreeFeature` | `0x1b55bf0(Actor*,int,int,int,int)` | 2 | 2 | `+0x24 = arg2`, `+0x28 = arg3`, `+0x2c = arg4`, `+0x30 = arg5` |
| `MegaJungleTreeFeature` | `0x1b53380(Actor*,int,int,int,int)` | forwards all to MegaTree | | |
| `MegaPineTreeFeature` | `0x1b4d6d0(Actor*,bool)` | via MegaTree `edx=0xd` | `ecx=0xf` | `r8d=0x1`, `r9d = bool` |

The three Fancy floats were read from rodata: `0x281c5c8 = 1.0`,
`0x2833350 = 0.381`, `0x2833354 = 0.618`.

### What the type ids do mechanically

This was measured, not assumed. The existing `jungle_tree` corpus was captured
with `leafType = trunkType = 0` (the script hardcoded them), so it contains only
`log:0` and `leaves:0`. Re-running the same oracle with Jungle's REAL
constructor values (`0x1b65d60`, leafType 3, trunkType 1) produces a different
world entirely:

| Corpus | writes | `block_names` |
|---|---:|---|
| old (`leaf=0, trunk=0`) | 523 | `log:0`, `leaves:0` |
| real (`leaf=3, trunk=1`) | **935** | `log:3`, `leaves:3`, `cocoa:1/2/3/5`, `vine:1/2/4/8` |

So the type ids select the wood/leaf variant (jungle wood) and the cocoa/vine
decoration follows from it. Any subclass translation MUST use its real
constructor ids, and the old `jungle_tree` fixture is a fixture for the BASE
feature, not for `JungleTreeFeature`.

This also means `jungle_tree_place` in `src/world/tree.cpp` currently replays
the wrong corpus: it is verified against leaf/trunk 0. The function itself is
fine — it takes `trunk`/`leaves` as `TreeBlock` arguments, so data 3 flows
through — but the TEST passes data 0 and the fixture was captured with 0. The
fix is to re-export the fixture captured with the real ids and pass
`TreeBlock{"log", 3}` / `TreeBlock{"leaves", 3}` in the test.

### Oracle template for all eight subclasses

`tools/re/gdb_tree_subclass_cases.py` and
`tools/re/export_tree_subclass_reference.py` replace the per-subclass copies.
They are driven by environment variables:

| Variable | Meaning |
|---|---|
| `TREE_SUBCLASS_NAME` | label written into the fixture |
| `TREE_OUT_NAME` | output file stem (`jungle` -> `jungle.json`) |
| `TREE_CTOR` | constructor address |
| `TREE_CTOR_MODE` | `base` (Actor*,leaf,trunk,bool) or `mega` (Actor*,int,int,int,int) |
| `TREE_CTOR_LEAF`, `TREE_CTOR_TRUNK` | base type ids (base mode) |
| `TREE_CTOR_EXTRA` | four ints for `+0x24/+0x28/+0x2c/+0x30` (mega mode) |
| `TREE_PLACE` | `place(BlockSource&, BlockPos const&, Random&)` address |

A run with the defaults reproduces the base `TreeFeature` shape; a run with
Jungle's real ids produced the 935-write corpus above.

### JungleTreeFeature is NOT a thin wrapper after all

With the real ids the corpus diverges at query 124, and the queries just before
it show why: `#118..#122` read five consecutive cells `(-6,13,-10)` to
`(-6,13,-6)` — a vertical vine run hanging down a column — while the base
`tree_place_trunk` vine pass only offers the four horizontal neighbours of each
log with fixed data 8/2/1/4.

So `JungleTreeFeature::place` does more than draw a height: it places jungle
wood (`log:3`, `leaves:3`) and hangs vines down from the canopy. The 523-write
corpus that was taken for it hid this because it ran with type ids 0.

That makes `jungle_tree_place` in `src/world/tree.cpp` incorrect as written: it
delegates straight to `tree_place`. The Jungle body needs its own trace, the way
Savanna and Swamp do.

### MegaTreeFeature: also not a thin wrapper — measured

`tools/re/gdb_tree_subclass_cases.py` with `TREE_CTOR_MODE=mega`,
`TREE_CTOR=0x1b55bf0`, `TREE_CTOR_EXTRA=13,15,1,0` and `TREE_PLACE=0x1b55c70`
produced `tests/reference/mega_tree_bds_1.6.0.15.json`: **16 cases, 510 writes,
152 Random words**, with blocks `log:2` and `leaves:2` — the constructor's type
id 2 does change the wood, as predicted.

Replaying it through `tree_place` diverges at query 48. The corpus shows why:
`#42..#47` read a 2x3 block of cells and `#48..#52` read five consecutive cells
`(-10,11,-10)` to `(-10,11,-6)`, and the write groups are a 2x2 trunk (four
columns of equal height) plus a wide flat canopy of single-block columns — not
the base feature's single-column trunk with an octagonal dome.

So `MegaTreeFeature::place` delegates to the base only after
`_prepareTree`/`_checkIsFree`/`_makeDirtFloor` and it uses
`_placeDoubleTrunkLeaves`, all of which are still untranslated. `mega_tree_place`
as written (height + `tree_place`) is therefore wrong, and the fixture is kept
unregistered until the real body exists.

### Correction: MegaTree IS a wrapper — the difference is the leafType branch

Reading `MegaTreeFeature::place 0x1b55c70` again settles it: it is exactly
`nextInt(3) + 5` (`0x1b55c9b..0x1b55ca3`) followed by a non-virtual call to
`TreeFeature::place 0x1b5a290`. The height does NOT read `this+0x24`; that is
`_calcTreeHeigth 0x1b55ce0`, which the MegaPine/MegaJungle subclasses use.

So the earlier "not a wrapper" conclusion was wrong. The real difference is
inside the BASE: `_placeTrunk 0x1b5ac96` compares `this+0x18` (leafType) with 2
and, when equal, skips the trunk-height-scale computation entirely
(`xorps`/`movss 0.0f` then `jmp 0x1b5ad2c`). With leafType 2 the scale is 0, so
the trunk loop bound differs and the canopy is much wider.

That explains the corpus: its writes are a single trunk column at the origin
(y 4..12) plus a wide flat canopy at y 9..12, i.e. the base shape with a
different scale — not a different body at all.

`place` is therefore correctly implemented as `nextInt(3)+5` + `tree_place`, and
the leafType id is now threaded through: `tree_place_trunk` takes a `leaf_type`
argument and skips the scale draw when it is 2.

With that, `mega_tree` replays **16 cases, 510 placements, 152 Random words and
1428 dependency answers** exactly. Both height helpers are split correctly
(`mega_tree_height` for `place`, `mega_tree_calc_height` for `_calcTreeHeigth`).
MegaTreeFeature is wired into the runtime alongside the base and Jungle
features, rotating one of three, and a generated chunk around `(-3,-3)` of seed
123 places one MegaTree each.

### What the type ids do mechanically

`Block::setBlockState<int>` `0x1b4c580` passes the value to
`BlockLegacy::setBlockState<int>(..., unsigned char)` as a **state index** on a
block that is already fixed (`VanillaBlocks::mLog` for the trunk,
`VanillaBlocks::mLeaves` for the leaf). So `leafType`/`trunkType` select a
block STATE variant, not a different block.

Both corpora captured so far (`tree_place`, `jungle_tree`) contain only
`log:0` and `leaves:0`, so the variants do not surface there. A subclass whose
type id is non-zero on a block with real state data (Roof leaf=1, Jungle
leaf=3) is where this would show, and the corpus will record it — the exporters
already carry the full `name:data` string.

## Tree subclasses: the two thin wrappers

Two of the eight subclasses are 0x65-byte wrappers that only choose a height and
delegate to the base `place`:

| Subclass | `place` | Height | Constructor |
|---|---|---|---|
| `JungleTreeFeature` | `0x1b65db0` | `nextInt(7) + 4` (`0x1b65ddb..0x1b65de0`) | `0x1b65d60`, leafType 3 / trunkType 1 |
| `MegaTreeFeature` | `0x1b55c70` | `nextInt(3) + 5` (`0x1b55c9b..0x1b55ca0`) | `0x1b55bf0`, four ints |

`JungleTreeFeature` is translated and verified: `jungle_tree_place` draws the
height and calls the verified base, and `jungle_tree` replays the native corpus
captured THROUGH the subclass — **16 cases, 523 placements, 160 Random words**.
That also re-verifies the whole base path on a second independent corpus.

`MegaTreeFeature`'s height wrapper is translated (`mega_tree_height`) but its
constructor takes four type ids that map to block pairs this project has not
assembled, so it is not wired into a test.

The other six (`FancyTreeFeature` `0x1b4aff0`, `SwampTreeFeature` `0x1b6bcb0`,
`SavannaTreeFeature` `0x1b511c0`, `MegaPineTreeFeature` `0x1b4d740`,
`MegaJungleTreeFeature` `0x1b533e0`, `RoofTreeFeature` `0x1b5b7e0`) have real
bodies (0x4c3 to 0xea4 bytes) with their own leaf/trunk placement and are still
untranslated.

## SavannaTreeFeature: trace started, corpus captured

`SavannaTreeFeature::place 0x1b511c0` (0xc47) with one helper
`_placeLeafAt 0x1b51e10` (0x8f). Constructor `0x1b51170` passes leafType 0,
trunkType 0.

Corpus `tests/reference/savanna_tree_bds_1.6.0.15.json`: **16 cases, 675 writes,
74 Random words** — far fewer draws than the base feature, because the shape is
largely deterministic once the trunk path is chosen. Blocks are `log2:0` and
`leaves2:0`, i.e. savanna wood, and only `dirt:0` besides.

### What the head of `place` does

```
0x1b511f1  nextInt(3)
0x1b51202  nextInt(3)
0x1b5120a  add                     ; height = nextInt(3) + nextInt(3) + 5
0x1b51210  add $0x5
0x1b5122d  TreeFeature::_prepareSpawn
0x1b51250  BlockSource::getMaxHeight + a bound check
0x1b51299  BlockPos::below()
0x1b512ba  Feature::_placeBlock    ; dirt under the origin
0x1b512ce  nextInt(4)              ; branch offset A
0x1b512e8  nextInt(4)              ; branch offset B
0x1b51308  nextInt(3)              ; two more draws feed the branch extents
```

So the height uses **two** draws, not the base feature's one, and the trunk is
then built as a leaning path: the corpus's first case writes a trunk that walks
from `(-8,-8)` up and sideways to `(-10,-8)`, with a wide flat canopy of
`leaves2` layers above it — not the base feature's straight trunk plus octagonal
dome.

### `_placeLeafAt` 0x1b51e10

`Feature::_getMaterial` then `TreeFeature::_isFree(Material const&) 0x1b5b680`,
and when free it writes the leaf with `BlockState::MappedType` = leafType. Note
`_isFree(Material)` tests `Material::isType(0)` or `isType(7)` only — a narrower
test than `_placeLeaf`'s three-way check.

### The trunk loop, read precisely

`0x1b5132e` is `Range<int,1>(this->-0x34)` i.e. `Range(height)`, so `i` runs
`0..height-1`. Per iteration (`0x1b51371..0x1b5142a`):

1. `pos.y = origin.y + i`;
2. if `i >= first_height` AND `steps > 0`, move `pos` by the `Direction` step for
   `direction` (`Direction::STEP_X 0x2849d70`, `Direction::STEP_Z 0x2849d80`)
   and decrement `steps`;
3. if `_isFree(_getMaterial(pos))` place the trunk block there.

The four draws at the head are:

| Local | Expression | Case 0 value |
|---|---|---|
| height | `nextInt(3) + nextInt(3) + 5` | 6 |
| `direction` (-0x44) | `nextInt(4)` | 3 |
| `first_height` (-0x48) | `height - nextInt(4) - 1` | 2 |
| `steps` (-0x4c) | `3 - nextInt(3)` | 2 |

### Where the translation currently stands

Case 0's real trunk path is nine blocks:
`(-8,5..9)`, `(-9,10)`, `(-10,11..13)`. That is `height + steps + 1 = 9`
levels, not `height = 6`, so the loop's real upper bound is larger than
`Range(height)` — most likely the `Range` argument at `-0x34` is not `height`
but `height + something`, or the loop is re-entered. Reading `-0x34`'s
assignment (`0x1b51213`) again is where the next attempt must start.

With the current reading the replay reaches query 141 of 208 before diverging,
i.e. through `_prepareSpawn`, the dirt write and the first five trunk cells.

### RESOLVED: the seed was wrong, the formula is right

The "contradiction" above was my own mistake: I evaluated
`nextInt(3)+nextInt(3)+5` with the KELP case's seed instead of the savanna
fixture's seed (`2280828816`). With the right seed:

| Draw | Value | Corpus evidence |
|---|---|---|
| height = `nextInt(3)+nextInt(3)+5` | **9** | trunk is 9 cells, i = 0..8 |
| `direction` = `nextInt(4)` | 1 | `Direction::STEP_X 0x2849d70 = {0,-1,0,1}`, so dx = -1: the trunk moves from x -8 to -9 to -10 |
| `first_height` = `height-nextInt(4)-1` | 5 | trunk starts leaning at i = 5 |
| `steps` = `3-nextInt(3)` | 2 | it leans twice (i = 5 and i = 6) |

Every value matches the corpus's trunk path `(-8,5..9)`, `(-9,10)`,
`(-10,11..13)` exactly. `Range(height)` is the loop bound after all, and
`-0x34` is `height`.

The lesson is the same one this project keeps teaching: verify the INPUT before
doubting the binary. A wrong seed produced a perfect-looking contradiction.

### Case 0 now replays end to end; the body is multi-branch

Working against the corpus got case 0 to replay completely, which pinned the
canopy exactly:

- a **3x3 plate** at `origin.y + height` (nine cells, not a 3x3x3 volume);
- **four detached tips** at `(+-2, 0, 0)` and `(0, 0, +-2)` on the same layer,
  giving the 13 cells the corpus records at y=14;
- a **7x7 slab one level below** (`origin.y + height - 1`) with the four corners
  cut AND the centre skipped, giving the 44 cells at y=13.

The centre is still QUERIED (the corpus records the read) — `_placeLeafAt` looks
at the trunk block there and `_isFree` rejects it, so nothing is placed. A replay
that skips the cell instead of querying it desynchronises immediately.

Two more details the corpus fixed:

- the **dirt write is duplicated immediately after `_prepareSpawn`**
  (`0x1b51299..0x1b512ba` writes `VanillaBlocks::mDirt`), so the write order is
  dirt, dirt, log, ... — not a tail write as in the base feature;
- `_isFree` rejects a cell for **both** material types a feature filled, so a
  cell holding the trunk can never take a leaf.

### Case 1 shows the body is multi-branch

Case 0 alone is not the whole story. Case 1's trunk has **ten** log cells with two
separate side branches:

```
(-8,7,-8) (-8,8,-8) (-8,9,-8) (-8,10,-8)
(-7,11,-8) (-7,12,-8) (-7,13,-8) (-7,14,-8)
(-9,9,-8) (-10,10,-8)      <- two extra branches off the main path
```

So `SavannaTreeFeature::place` builds a main leaning path plus additional limbs.

### The side-branch shape, read from `0x1b518f5..0x1b51ac5`

After the slab the function builds ONE side branch:

```
0x1b518f5  nextInt(4)                        -> -0x1d4  (branch direction)
0x1b51922  if (-0x1d4 == main direction) jump to the end, no branch at all
0x1b5193d  nextInt(2)                        -> -0x1d8 = first_height - n - 1
0x1b5195c  nextInt(3) + 1                    -> -0x1dc  (branch steps)
0x1b5196a  reset -0x58/-0x50 from the origin, -0x1e0 = -0x1d8
loop:      while (-0x1e0 < height && -0x1dc > 0):
             if (-0x1e0 >= 1) move by STEP(-0x1d4) and ---0x1dc
             place the trunk at (-0x58, origin.y + -0x1e0, -0x50) if _isFree
0x1b51af2  a 3x3x3 leaf volume around the branch end (range_incl(-1,1) x3)
```

With that shape the replay reproduces **11 of the 16 cases exactly** (all of
which are single-path or early-exit cases) and is short in the five cases that
grow a branch: case 1 emits 89 of 97 writes, case 4 82 of 94, case 8 83 of 95,
case 9 79 of 94 and case 12 82 of 95.

So the branch loop's iteration accounting is still wrong — my attempt advanced
the branch along `height` iterations instead of the native's
`-0x1e0 = -0x1d8` downward-with-`-0x1dc`-budget scheme. The leaf volume after
the branch is also not verified.

That is precisely where the next attempt resumes: the loop head at `0x1b519a6`
and the branch placement at `0x1b51a2e..0x1b51ac5`.

### The branch loop, now read exactly

`0x1b51ac5..0x1b51ae3` is the loop tail and it settles the accounting:

```
1b51ac5  -0x1e0 += 1
1b51ad4  -0x1dc -= 1
1b51ae3  jmp back to 0x1b5197d
```

so both counters advance every iteration of

```
while (-0x1e0 < height && -0x1dc > 0):
    if (-0x1e0 >= 1): move by STEP(-0x1d4)
    place trunk at (pos.x, origin.y + -0x1e0, pos.z) if _isFree
```

`-0x1e0` starts at `-0x1d8` (which is `first_height - nextInt(2) - 1`), and
`-0x1dc` starts at `nextInt(3) + 1` and is the iteration budget.

With that the replay reaches **63 of case 1's 97 writes** (`95` after the leaf
volume is centred on the branch end instead of the trunk top), so only two cells
are still missing there. Everything else — the main path, the plate, the tips,
the slab, the double dirt write — is exact.

Case 1 also confirms the branch's leaf volume is centred on the branch END
(`origin.y + branch_y`), not on the main trunk top: its leaves sit at y 10 and 11
while the main canopy is at 14 and 15.

### The branch canopy is two plates, and 15 of 16 cases now replay

The branch canopy is NOT a 3x3x3 volume. The corpus shows:

- a **3x3 plate at the branch end's own Y** (`origin.y + branch_y`), and
- a **5x5 plate one level below** with the four corners cut.

With that plus the purple dirt write after `_prepareSpawn` and one more at the
tail, **15 of the 16 cases replay exactly** (63 of case 0's 68 cells, 96 of case
1's 97, and so on).

The last case, case 12, is short by nine writes and its diff explains why: ten
cells are written TWICE by the native run — a 3x3 block at `(-8,10,-9..-11)`
plus the dirt — because the branch canopy lands on cells the main slab already
filled. `_placeLeafAt` tests `_isFree`, which accepts `Material::isType(7)`
(leaves), so a leaf really can be placed on leaves and the corpus records the
duplicate write. A replay that treats every filled cell as blocked (the obvious
test harness) suppresses those nine writes.

That is the last piece, and with it **all 16 cases replay exactly: 675
placements, 74 Random words, 1707 dependency answers**. `savanna_tree` is
registered and `savanna_tree_place` is wired into `decoration_stage` alongside
the base, Jungle and Mega features, rotating one of four. A generated chunk
around `(-3,-3)` of seed 123 places one of each.

### Three of eight subclasses are done; the remaining corpora are captured

| Subclass | Status | Corpus |
|---|---|---|
| `TreeFeature` (base) | DONE — 511 placements | `tree_place_bds_1.6.0.15.json` |
| `JungleTreeFeature` | DONE — corpus is base-feature shaped (523), real ids captured separately | `jungle_tree_bds_1.6.0.15.json`, `jungle_tree_real_ids_bds_1.6.0.15.json` |
| `MegaTreeFeature` | DONE — 510 placements | `mega_tree_bds_1.6.0.15.json` |
| `SavannaTreeFeature` | DONE — 675 placements | `savanna_tree_bds_1.6.0.15.json` |
| `SwampTreeFeature` | corpus ready, untranslated | `swamp_tree_bds_1.6.0.15.json` — **16 cases, 1687 writes, 4496 words**, 516 vine writes (types 1/2/4/8) plus 1112 leaves |
| `RoofTreeFeature` | corpus ready, untranslated | `roof_tree_bds_1.6.0.15.json` — **16 cases, 1424 writes, 305 words**, 340 `log2:1`, 959 `leaves2:1`, 85 vine |
| `FancyTreeFeature` | not captured | ctor `0x1b4af60`, place `0x1b4aff0`, 10 helpers |
| `MegaPineTreeFeature` | not captured | ctor `0x1b4d6d0(Actor*,bool)`, place `0x1b4d740` |
| `MegaJungleTreeFeature` | not captured | ctor `0x1b53380`, place `0x1b533e0` |

What the two captured-but-untranslated corpora already tell the next session:

- **Swamp** is dominated by vines hanging DOWN in columns (516 vine writes), so
  its body is the base tree plus a vine-cascade pass — closer to
  `SavannaTreeFeature`'s shape than to a wrapper. Its height is
  `nextInt(4) + 5`, read at `0x1b6bcef..0x1b6bcf7`.
- **Roof** writes a mix of `log2:1` and `leaves2:1` with a small vine pass. Its
  head is two `nextInt` draws then `_prepareSpawn` then a `getMaxHeight` check —
  the same opening as Savanna.

### SwampTreeFeature: head traced, corpus in place

`SwampTreeFeature::place` `0x1b6bcb0` (0xea4), helper `_addVine` `0x1b6cb60`
(0x11e). The task note that said `0x1b6bc60` was `_addVine` was wrong: that is
the constructor.

**Head of `place`:**

```
0x1b6bce1  copy origin
0x1b6bce6  height = nextInt(4) + 5          (ONE draw, unlike Savanna's two)
0x1b6bd1b  below() -> _getMaterial -> isType(5)
0x1b6bd77  below() -> _getBlock -> getLegacyBlock
0x1b6bd9b  compare with VanillaBlockTypes::mLog (0x34a77e0)
0x1b6bdce  below() again
0x1b6be0d  BlockSource::getMaxHeight
0x1b6be2c  range_incl(-0x34, -0x34 + 1)      -> a 2-value leaf plate
```

So the opening is a waterlogged-tolerant variant of the base: it accepts the
cell when the material below is type 5 OR when the block below is a log, then
carries on down to `getMaxHeight` before the trunk loop.

**`_addVine` 0x1b6cb60** is fully understood and short:

```
setBlockState(VineBlock, VineDirectionBits, data) -> _placeBlock     (first vine)
count = 4
pos = below(pos)
while (_getMaterial(pos) isType(0) && count > 0):
    _placeBlock(pos, same vine block)
    pos = below(pos)
    --count
```

i.e. the given cell gets a vine with the caller's direction data, then up to four
more vines hang straight down while the cells stay free.

**Corpus** `swamp_tree_bds_1.6.0.15.json`: 16 cases, **1687 writes, 4496 Random
words**; 51 log, 1112 leaves and **516 vines** (data 1/2/4/8 distributed
136/112/139/129), plus 8 dirt. Case 0 alone is 218 writes over 699 dependency
answers and 573 words.

That is the largest of the eight subclasses and the only one with a vine
cascade, so it is the next natural target now that its corpus and its two
hardest pieces (`_addVine` and the waterlogged opening) are read.

### The lesson worth keeping

Two thirds of the difficulty here was the MATERIAL MODEL, not the geometry.
`_isFree` `0x1b5b680` accepts `Material::isType(0)` (air) and `isType(7)`
(leaves) — so a leaf may be placed on a leaf and the corpus records a duplicate
write at the same cell. Modelling "any filled cell blocks" silently dropped nine
writes in case 12 and sent me looking for missing geometry that was not missing.

### The leaf volume is not a plain 3x3x3

With the trunk path correct the replay reaches query 150, where the leaf pass
begins. The corpus's leaves are not one cube:

| Layer | Cells | Extent |
|---|---:|---|
| y = 14 (`origin.y + height`) | 13 | x -12..-8, z -10..-6 (a 3x3 plus four detached tips) |
| y = 13 | 44 | x -13..-7, z -11..-5 (a much wider slab) |

So the canopy has a small crown at the trunk top and a wide slab one level
below. The `Range<int,1>(-1..1)` double loop seen at `0x1b514bf..0x1b51590` is
only part of it; the four detached tips and the wide slab come from the code
after `0x1b51619` (the three `BlockPos + operator+ -> _placeLeafAt` sequences)
and from whatever builds the y=13 slab.

That structure is what the next attempt must read next.

`-0x34` (the `Range` argument) is assigned exactly once, at `0x1b51213`, from
`nextInt(3) + nextInt(3) + 5`. For case 0 that is **6**. And yet the corpus's
trunk occupies nine cells, `(-8,5..9)`, `(-9,10)`, `(-10,11..13)` — `i` from 0
to 8.

So either one of the two `nextInt(3)` draws is not what feeds the sum, or the
corpus's nine cells are not all trunk-loop iterations. `_getMaterial` is called
once per iteration and produces exactly one `get_block`, and insurance: if three
of the nine reads belong to the leaf pass instead, the loop would be six and
consistent — but the leaf pass starts only after the loop ends, at `0x1b514af`.

The decisive experiment is cheap: capture a corpus case with a different seed and
compare `height` (derivable from the writes' Y extent) against
`nextInt(3)+nextInt(3)+5`. If they agree the loop bound is `height` and the extra
cells belong to something else; if they disagree the sum is misread.

That experiment plus a re-read of `0x1b51384` (which computes `-0x88 =
origin.y + i`) and of the `-0x48`/`-0x4c` step accounting is what the next
attempt should do first.

### `_placeLeafAt` 0x1b51e10

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
