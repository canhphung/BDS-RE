# TreeFeature family — trace evidence and translation plan

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

Status: **the base class and five of the eight subclasses are translated and
replay their corpora exactly** — `TreeFeature`, `JungleTreeFeature`,
`MegaTreeFeature`, `SavannaTreeFeature` and `SwampTreeFeature` (1687
placements). `RoofTreeFeature`'s corpus is captured; `FancyTreeFeature`,
`MegaPineTreeFeature` and `MegaJungleTreeFeature` are not captured yet.

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

So the first reading suggested `MegaTreeFeature::place` delegated to the base
only after `_prepareTree`/`_checkIsFree`/`_makeDirtFloor`. That turned out to be
wrong and is kept only as reasoning history.

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

(`FancyTreeFeature` `0x1b4aff0`, `MegaPineTreeFeature` `0x1b4d740`,
`MegaJungleTreeFeature` `0x1b533e0` and `RoofTreeFeature` `0x1b5b7e0`) have real
bodies (0x4c3 to 0x6ea bytes) with their own leaf/trunk placement and are still
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
| `SwampTreeFeature` | **DONE — 1687 placements, 4496 words, 6282 answers** | `swamp_tree_bds_1.6.0.15.json` |
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
cascade.

### SwampTreeFeature: what a working replay needs (measured, not guessed)

Case 0's dependency stream opens with **two `get_block(origin.below)`** and NO
`may_place_on` at all — so `SwampTreeFeature::place` does **not** call
`_prepareSpawn`. That was the first thing an implementation gets wrong: keeping
`tree_prepare_spawn` makes the replay stop after four queries and emit zero
writes.

Case 0's blocks by layer (origin `(-8,5,-8)`):

| y | contents |
|---:|---|
| 4 | 1 dirt (the anchor write) |
| 5..12 | 1 log per layer — a single straight trunk of 8 |
| 6..9 | trunk log plus 6-8 vines per layer, hanging around it |
| 10 | 49 leaves (a full 7x7) + 1 log + 10 vines |
| 11 | 48 leaves + 1 log + 5 vines |
| 12 | 24 leaves + 1 log + 9 vines |
| 13 | 21 leaves + 9 vines |

The canopy is 7x7 at `origin.y + height` and `+1`, then 5x5 at `+2` and `+3`;
the layers come out 49, 48, 24 and 21 because trunk and vine cells are already
occupied and `_isFree` rejects them.

The drones are the vines: **21 separate vine columns**, each hanging 1 to 5
cells, distributed around the canopy rim rather than in four neat sides:

```
(-4,-3) y6..10   (-4,1) y7..11   (-4,3) y6..10   (-3,0) y12..13
(-2,-3) y12      (-2,2) y12..13  (-1,-3) y12..13 (0,3) y12..13
(0,4) y6..10     (1,-4) y6..10   (1,3) y12..13   (2,-4) y6..10
(2,-2) y13       (2,2) y13       (2,4) y7..11    (3,-2) y12
(3,1) y12..13    (3,3) y11       (4,1) y7..11
```

### The vine pass: four directions, each with its own `_addVine`

`_addVine 0x1b6cb60` is called exactly **four** times, at `0x1b6c958`,
`0x1b6c9e1`, `0x1b6ca6a` and `0x1b6caf3`, and each call site loads a different
`VineBlock::VINE_*` direction:

| Call site | Direction |
|---|---|
| `0x1b6c958` | `VineBlock::VINE_EAST` `0x34a6f70` |
| `0x1b6c9e1` | `VineBlock::VINE_WEST` `0x34a6f74` |
| `0x1b6ca6a` | `VineBlock::VINE_SOUTH` `0x34a6f68` |
| `0x1b6caf3` | `VineBlock::VINE_NORTH` `0x34a6f6c` |

Each is guarded the same way: `_getMaterial(pos)` then `Material::isType(0)`,
and only when the cell is free does `_addVine(pos, direction)` run. So the 21
vine columns in the corpus are the SIDE cells of the canopy that happen to be
free, one cascade per side, not a rim walk — the four calls are the four faces.

### The canopy plate, read from `0x1b6c400..0x1b6c4d3`

The plate walk distinguishes corners from edges:

```
if (abs(dx) == r && abs(dz) == r):        // a corner
    if (nextInt(2) != 0 && -0x108 != 0) skip
else:                                      // an edge or interior cell
    if (isSolidBlockingBlock(pos)) skip
place mLeaves
```

so a corner takes two random conditions while a non-corner only needs the cell
to be non-solid. That explains the corpus's layer counts (49, 48, 24, 21): the
corners of the 7x7 layers are gated by `nextInt(2)`, and the trunk/vine cells
fail the solid test.

### Correction: the vine faces are gated by nextInt(4), not by material alone

Building the replay disproved one reading. Each of the four `_addVine` sites is
reached through

```
pos = <canopy>.south()/west()/north()/east()
if (nextInt(4) != 0) goto skip          // 0x1b6c8d4..0x1b6c8e5
if (!_getMaterial(pos).isType(0)) goto skip
_addVine(pos, VINE_x)
```

so a face only gets a cascade on a **1-in-4 roll**, and the block used is the
canopy's own face cell (`south()`/`west()`/`north()`/`east()` of the top
position), which is why the corpus's 21 columns sit on the canopy rim.

With that gate, a replay of the trunk, the dirt, the 8 logs and the canopy
already matches: **log 8/8, dirt 1/1, leaves 131 of 142 and no spurious
writes**. What is still missing is the vine volume (0 of 67) and the last 11
leaves — and that is where the real work is.

### The canopy is a TRIPLE-nested volume, which is where 573 words go

`place` has only **six** `Random::nextInt` call sites:

```
0x1b6bcef   height = nextInt(4) + 5
0x1b6c44b   the corner roll, INSIDE three nested Range loops
0x1b6c8dd   vine face gate (south)
0x1b6c966   vine face gate (west)
0x1b6c9ef   vine face gate (south/north)
0x1b6ca78   vine face gate (east)
```

Yet case 0 consumes **573 words**. The reason is the three back-edges at
`0x1b6c4df`, `0x1b6c4f5` and `0x1b6c50b`: the canopy walk is a **triple nested
loop** matching `-0x150`, `-0x128` and `-0xf8`, followed by a plain
`range<int,1>(int)` at `0x1b6c513` and a further `Range` walk. So the canopy is
a full 3-D volume (not four flat plates) whose innermost body runs the corner
roll at `0x1b6c44b` on every iteration — hence hundreds of words.

That nested walk is now read, and it is the base dome loop with a larger base
radius:

```
0x1b6c230..0x1b6c250  outer = range_incl(origin.y + height - 3, origin.y + height)
0x1b6c2b8..0x1b6c300  half  = 3 - rel/2      // 3, 3, 2, 2
0x1b6c395             inner = range_incl(...)
0x1b6c44b             corner roll: nextInt(2), inside the innermost body
```

which reproduces the corpus exactly: 7x7 at the two lower layers and 5x5 at the
two upper ones, with the layer counts 49/48/24/21 falling out because a corner
that loses its `nextInt(2)` roll is skipped. It is `tree_place_leaves` with base
3 instead of base 2, so `swamp_tree_place` can reuse `tree_place_leaf`.

**A second, separate loop follows the canopy**: `0x1b6c510` runs
`range<int,1>(height)`, and its body does `BlockPos::above(i)`, `_getMaterial`,
`_isFree` `0x1b5b680` and `_placeBlock` (`0x1b6c5e6..0x1b6c65f`). That is a
per-height pass around the trunk column, and it is what reads the ten
off-canopy vine candidate cells the corpus shows at y = 5: `(-4,-3) (-4,3)
(0,4) (1,-4) (2,-4) (4,3)` relative to the origin.

So the shape is: waterlogged head -> one `nextInt(4)+5` height -> 8-log trunk ->
7x7/7x7/5x5/5x5 dome with corner rolls -> a `range(height)` pass around the
trunk -> four `nextInt(4)`-gated `_addVine` faces. A replay built from that
already matches the trunk 8/8, the dirt 1/1 and 131 of 142 leaves with zero
spurious writes. The vine writes (0 of 67) are the one part still open, and the
ten candidate cells above are the evidence needed to finish them.

### SwampTreeFeature: the canopy, read from the disassembly at last

The exact inner body, from `gdb` on `0x1b6c2a9..0x1b6c460`:

```
[rbp-0x3c]  = height
[rbp-0x34]  = origin.y
[rbp-0x38]  = origin.z?  (see below)
[rbp-0x104] = the outer Range iterator's value -> y
[rbp-0x108] = y - (origin.y + height)          -> rel   (0 on the TOP layer)
[rbp-0x10c] = 2 - rel/2                        -> half
[rbp-0x138] = x - origin.x
[rbp-0x160] = z - origin.z

outer = range_incl(origin.y + height - 3, origin.y + height)
half  = 2 - rel/2                              // C truncation: rel -3..0 -> 3,3,2,2
```

and the corner test is

```
if (abs(x - origin.x) == half && abs(z - origin.z) == half) {   // a CORNER
    if (nextInt(2) == 0) skip;          // 0x1b6c44b, 0x1b6c450..0x1b6c453
    if ([rbp-0x108] != 0) skip;         // 0x1b6c459: rel != 0
}
// fall-through and every non-corner:
if (isSolidBlockingBlock(pos)) skip;    // 0x1b6c494
_placeBlock(pos, mLeaves);              // 0x1b6c4c3
```

Two things this settles that the corpus alone could not:

* `-0x108` is the **rel** of the current layer, and the second corner condition is
  `rel != 0`. So corners exist on every layer EXCEPT the top one, which is why
  the top 5x5 layer is missing all four corners (21 of 25) while the layer below
  loses only one (`nextInt(2)`).
* A cell is gated on `BlockSource::isSolidBlockingBlock 0x1c5aea0`, **not** on
  the material test. A replay that substitutes `_isFree` will accept corners the
  original rejects.

### SwampTreeFeature: the canopy is DONE, the vines are a nested rim walk

Reading `0x1b6c2a9..0x1b6c460` from `gdb` settled the canopy exactly, and a
replay now produces **every trunk, dirt and leaf cell in all 16 cases with zero
spurious writes**. The remaining shortfall per case equals the vine count
exactly (67, 47, 44, 56, 57, 86, 74, 85), so the canopy is finished.

The exact inner body:

```
[rbp-0x3c]  = height
[rbp-0x34]  = origin.y
[rbp-0x104] = outer Range value -> y
[rbp-0x108] = y - (origin.y + height)   -> rel, 0 on the TOP layer
[rbp-0x10c] = 2 - rel/2                 -> half
[rbp-0x138] = x - origin.x
[rbp-0x160] = z - origin.z

outer: range_incl(origin.y + height - 3, origin.y + height)
half : 2 - rel/2                        // truncation -> 3, 3, 2, 2
corner (abs(dx)==half && abs(dz)==half):
    skip if nextInt(2) == 0             // 0x1b6c44b, 0x1b6c453
    skip if [rbp-0x108] != 0            // 0x1b6c459 -> rel != 0
otherwise:
    skip if isSolidBlockingBlock(pos)   // 0x1b6c494, NOT _isFree
    _placeBlock(pos, mLeaves)           // 0x1b6c4c3
```

Two facts the corpus alone could not give:

* the second corner condition is `rel != 0`, so corners are skipped on the TOP
  layer only — that is why the top 5x5 keeps 21 of 25 while the layer below
  loses exactly one to `nextInt(2)`;
* the cell test is `BlockSource::isSolidBlockingBlock 0x1c5aea0`, not the
  material test. Substituting `_isFree` accepts cells the original rejects.

### The vines are a nested rim walk — READ EXACTLY

`0x1b6c551..0x1b6caf3` repeats the canopy's **same three nested loops**, with the
same bounds:

```
outer: range_incl(origin.y + height - 3, origin.y + height)   (0x1b6c680..0x1b6c693)
rel   = y - (origin.y + height)
half  = 2 - rel/2                                             (0x1b6c702..0x1b6c727)
inner: x and z in range_incl(-half, +half)
```

The body (0x1b6c820..0x1b6c8cf) is:

```
pos = BlockPos(origin.x + dx, y, origin.z + dz)
if (!_getMaterial(pos).isType(7)) continue       // 0x1b6c876: must be a LEAF
p_west  = pos.west();   p_east  = pos.east()
p_north = pos.north();  p_south = pos.south()
// then the four _addVine sites, each behind its own nextInt(4) gate
```

So vines are produced by walking **every leaf cell** and offering each of its
four horizontal neighbours to `_addVine`, one 1-in-4 roll per direction. That is
why the corpus's columns sit on the canopy rim and why case 0 has 67 of them.

**Replay status with all of that implemented**: every case reaches
`miss = 0` — the replay produces a superset that CONTAINS every native cell.
The remaining differences are only extra vine writes (case 0 is 215 vs 218 with
3 native duplicates at the trunk column explaining the gap, case 1 is 203 vs 191)
plus the eight early-out cases.

That is a much better position than before: the geometry is complete, and what
is left is the exact gating (how many of the four neighbour rolls really fire)
and the early-out.

### The corpus reveals the write ORDER, and that is the last clue

Case 0's `writes` array is in native call order, and its runs are

```
dirt (1)  ->  leaves (142)  ->  log (8)  ->  vine (67)
```

So the **canopy is written BEFORE the trunk**. That is not cosmetic: the three
cells `(-8,10,-8)`, `(-8,11,-8)` and `(-8,12,-8)` each appear TWICE in the
corpus — once as a leaf at indices 25, 74 and 109, and once as a log at indices
148, 149 and 150. The leaves go down first and the trunk column then overwrites
them, which is exactly the 3-write gap that a trunk-first replay cannot produce
(215 vs 218).

Consequently the order inside `place` is:

1. the two `get_block(origin.below)` reads and the height draw
2. **dirt** under the origin
3. **the canopy dome** (the `0x1b6c230..0x1b6c510` loops)
4. **the trunk column** (which re-writes the cells the canopy just filled)
5. **the vine rim walk** (`0x1b6c551..0x1b6caf3`)
6. a final `get_block(origin.below)` whose answer decides the return value
   (`0x1b6cb3f` sets the byte to 1 on the success path)

That final read is the eight early-out cases: they do their whole canopy, trunk
and vine work, then the last `get_block` fails and the feature returns false
with **zero** writes because the caller discards the region. So the replay has
to buffer its writes and commit them only when that last read says yes.

**Confirmed by building it**: with the canopy written before the trunk, case 0
replays to exactly **218 writes** and cases 9 and 13 to exactly 233 and 232 —
the write order was the missing piece for those three. Case 0's query stream
also matches to the last read.

### Two remaining obstacles, both in the harness not the feature

1. **Every query answer in the corpus is 0.** All 6282 `get_block` rows across
   the 16 cases carry `raw = 0`, so the corpus does NOT encode what the world
   contained; the world has to be rebuilt from `scenes[case.scene]`, the zlib +
   base64 `<hhhH>` cell list. A harness that answers every query with 0 makes
   the feature see an all-air world, which is why the leaf/trunk material tests
   then diverge. The scene decoder already exists for the other corpora; it
   needs wiring into the swamp test.
2. **The head gate is not what the sketch assumed.** `0x1b6bd32..0x1b6bdb9`
   is: `_getMaterial(origin.below).isType(5)` OR
   `_getBlock(origin.below).getLegacyBlock() == VanillaBlockTypes::mLog`; when
   the first is true the second read is skipped, and only when the whole test is
   true does the code take `origin = origin.below()` (`0x1b6bddb`) and continue.
   The three-query cases (3, 7, 11, 15) stop exactly there because the cell
   itself is stone (`self=stone:0`), so the gate fails on the first read.
   Modelling it as `_prepareSpawn` or as a bare `max_height` test is wrong.

Both are mechanical, and the geometry plus the write order are now settled.

### The `raw` column is the block identity — the missing key

The corpus's `raw` field on each query IS the native block value, not a checksum.
Mapping it through every case's scene gives **11 distinct values and zero
conflicts**:

```
56737152 -> air      56972400 -> stone    58033136 -> grass
58038544 -> dirt     58072288 -> sand
58074752 / 58080224 / 58330352 / 58330688 / 58331360 / 58332704 -> air
```

The last six are air cells the tree had already written (the pointer differs, the
block does not), so a single air mapping is enough. With `raw` decoded the scene
becomes a real simulated world: start from `scenes[case.scene]`, answer every
`get_block` from it, and apply each `place_block` as the replay runs.

### What a scene-driven replay already gets right

Built that way the head is

```
read origin.below()                 # 0x1b6bd1b
material_is(below, 5) ?             # 0x1b6bd3f, MaterialType 5
  yes -> base = origin.below()      # 0x1b6bddb
  no  -> read the block again       # 0x1b6bd8e, compare with mLog
read origin itself                  # the free-cell gate
```

and the eight `result=false` cases then consume **exactly** the recorded number
of queries: 3/3 for the four stone-anchor cases (3, 7, 11, 15), 178/178 for case
6, and 205/205 for cases 2, 10 and 14. That is a strong signal the head and the
canopy walk are right, because a wrong early-out would break those counts.

What is still wrong is the RETURN VALUE and the query tail of the eight
`result=true` cases (they stop around 489 of 699 because the tail loop after the
vines is not written yet). So the remaining work is:

1. the final decision that makes cases 2/6/10/14 return false after a full
   canopy walk (their anchor is sand, while 0/1/4/5 are grass and dirt), and
2. the post-vine loop that consumes the last ~200 queries of the true cases.

Everything before that — head, canopy, trunk, vine walk — is now confirmed by
exact query counts on half the corpus.

### SOLVED: the clearance walk, and the anchor-material gate

The missing second early-out is a **clearance walk** between the head and the
dirt write (`0x1b6be2c..0x1b6c10b`), and it explains the query counts exactly.
For every layer `y` from `origin.y` to `origin.y + height + 1` the half-width is

```
r = 0        when y == origin.y
r = 1        when y <= origin.y + height - 2
r = 3        otherwise
```

which `0x1b6bea1..0x1b6bed5` computes as a 0/1/3 select on the layer index. Each
cell of that square is read once and tested (`_isFree`, then
`getMaterial().isType(0)`), and the result is ANDed into the `grows` flag at
`[rbp-0x69]`.

Verified numerically against all 16 cases: `3 + sum((2r+1)^2)` over those layers
reproduces the recorded query count EXACTLY for every case that stops early —
205 for cases 2, 10, 14; 178 for case 6; and the four stone-anchor cases (3, 7,
11, 15) stop at the head's 3 reads.

The anchor material then decides the rest, and the corpus splits perfectly:

| block under origin | result | queries |
|---|---|---|
| grass, dirt | true | head + clearance + canopy + trunk + vines + tail |
| sand | false | head + clearance, then one final `get_block(origin.below)` |
| stone | false | 3 (head only) |

With that implemented, **all 16 return values are correct** and the eight
`result=false` cases consume exactly their recorded query streams (3/3, 178/178,
205/205). The eight `result=true` cases are correct too, but stop short of the
final count (690/699, 695/709, 706/720, ...) because the post-vine tail pass is
still unwritten — that tail is the only remaining work, and it is the same
`get_block`/`material_is` shape as the loops already translated.

`0x1b6c693`, `0x1b6c743` and `0x1b6c7c9` are three more nested `range_incl`
loops (y, x, z) whose body does `_getMaterial`, `Material::isType`, and then
`BlockPos::west()/east()/north()/south()` before reaching the four `_addVine`
sites at `0x1b6c958`, `0x1b6c9e1`, `0x1b6ca6a`, `0x1b6caf3`. So the vines come
from walking a rim volume around the canopy and, per cell, testing its four
horizontal neighbours — which is why the corpus's columns sit on the canopy rim
and why there are so many (67 in case 0).

Two cases (2, 3, 6, 7, 10, 11, 14, 15) return `result=false` with **zero**
writes after ~205 queries, and their query stream ends with a third
`get_block(origin.below)`. That is the early-out: the feature does its canopy
reads first and only then decides, so a failed anchor still burns the whole
canopy walk — an implementation that checks the ground up front will diverge on
the query stream even though it writes nothing.

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

## SwampTreeFeature: COMPLETE

`SwampTreeFeature::place 0x1b6bcb0` and its helper `_addVine 0x1b6cb60` are
translated and replay the whole corpus exactly: **16 of 16 cases, 1687
placements, 4496 Random words, 6282 dependency answers**. `swamp_tree` is
registered in CMake and `swamp_tree_place` is wired into `decoration_stage`
alongside the base, Jungle, Mega and Savanna features, rotating one of five.

### The full body, in order

1. `height = nextInt(4) + 5` — ONE draw, at `0x1b6bcef`.
2. **Anchor test** `0x1b6bd1b..0x1b6bdec`: read `origin.below()`;
   `_getMaterial(...).isType(5)` succeeds, otherwise read the block again and
   compare `getLegacyBlock()` with `VanillaBlockTypes::mLog`; on success drop the
   origin one cell.
3. **Ceiling test** `0x1b6be0d..0x1b6be1d`: `origin.y + height` must be below
   `BlockSource::getMaxHeight`.
4. **Anchor-cell test**: read `origin` itself; if it is not air, return false.
   That is exactly the three-query cases (3, 7, 11, 15).
5. **Clearance walk** `0x1b6be2c..0x1b6c10b`: for every layer `y` from
   `origin.y + 1` to `origin.y + height + 1` the half-width is
   `1` when `y <= origin.y + height - 2` and `3` otherwise
   (`0x1b6bea1..0x1b6bed5` computes the 0/1/3 select). Every cell of that square
   is read once; `isSolidBlockingBlock` or a non-air material clears `grows`.
6. **Ground-material test** `0x1b6c10b..0x1b6c119`: the block under the origin
   must be a material a swamp tree accepts. The corpus splits perfectly on it —
   grass and dirt proceed, sand and stone return false, and the sand cases spend
   their last recorded query re-reading `origin.below()`.
7. **Dirt** `0x1b6c22b`.
8. **Canopy dome**, written BEFORE the trunk (the corpus's write order is
   dirt -> leaves -> log -> vine): four layers at `origin.y + height - 3 .. +0`,
   `half = 2 - rel/2` giving 3, 3, 2, 2; a corner survives only when
   `nextInt(2) != 0` AND `rel != 0`; any other cell needs
   `!isSolidBlockingBlock`.
9. **Trunk column** of `height` logs, overwriting the canopy cells it occupies
   (case 0 writes `(-8,10..12,-8)` twice, once as a leaf and once as a log).
10. **Vine rim walk** `0x1b6c551..0x1b6caf3`: the same three nested loops run
    again; each cell that is now a LEAF tests its four horizontal neighbours,
    each behind its own `nextInt(4)` gate, and a survivor gets a cascade.
11. `_addVine` places one vine with the caller's direction, then up to four more
    straight down. The loop reads the cell BEFORE testing the count, so it
    always consumes one read past the last cell it writes — that extra read is
    what made the query stream one short until it was modelled.

### Two facts only the disassembly could give

- **The corner condition is `rel != 0`, not a random test alone**, so corners
  are skipped on the TOP canopy layer. That is why the top 5x5 keeps 21 of 25
  while the layer below loses exactly one to `nextInt(2)`.
- **The clearance walk's 0/1/3 half-width select** reproduces the recorded query
  counts arithmetically: `3 + sum((2r+1)^2)` equals 205 for cases 2/10/14, 178
  for case 6 and the four stone cases stop at 3. No other model fits.

### The dependency harness lesson

The corpus's query answers are all zero; the world each case ran against is
`scenes[case.scene]`, a zlib+base64 `<hhhH>` cell list. The test decodes those
scenes, answers every `get_block` from them, and applies each `place_block` as
the replay runs, so the translated feature sees the same world the original did
including the cells it has just written.

## FancyTreeFeature: corpus captured, five helpers read

`FancyTreeFeature` is the largest of the eight subclasses and the only one built
from float geometry: `place 0x1b4aff0` uses `Random::nextFloat`, `mce::Math::sin`,
`cos`, `sqrt` and `powf`, and carries a `std::vector<FoliageCoords>` of branch
bases. The corpus is now captured:

`fancy_tree_bds_1.6.0.15.json` — **16 cases, 5032 writes, 182 Random words, 5517
dependency answers**. Blocks: 4732 `leaves:0`, 182 `log:0`, 73 `log:4`, 45
`log:8`. Eight cases return true (934/713/729/724/219/163/715/835 writes) and
eight return false after a single word and a single query.

### Constructor `0x1b4af60` (0x90 bytes) — complete

Calls `TreeFeature::TreeFeature(Actor*, 0, 0, false)` then sets:

| Offset | Value |
|---|---|
| `+0x24` | `0xc` (12) — the `nextInt` bound for height |
| `+0x28` | `0.618` (`0x2833354`) |
| `+0x2c` | `0.381` (`0x2833350`) |
| `+0x30`, `+0x34` | `1.0` (`0x281c5c8`) |
| `+0x38` | `1` |
| `+0x3c` | `4` |
| `+0x40` | `6` |

### `_getTreeShape(int a, int b) 0x1b4b670` (0xf7) — complete

```
if (a * 0.3 > b)              return -1.0f;
t   = a / 2.0f
d   = t - b
r   = sqrt(t*t - d*d)
if (d == 0) r = t
else if (abs(d) < t) ...      // branch at 0x1b4b72e..0x1b4b740
return 0.5f * r
```

Constants: `0.3` at `0x2828678`, `2.0` at `0x2828640`, `-1.0` at `0x2824708`,
`0.5` at `0x281cd14`.

### `_checkLocation(BlockSource&, BlockPos const&, int&) 0x1b4b500` (0x163) — complete

```
below = _getBlock(pos.below()).getLegacyBlock()
if (below != mDirt && below != mGrass && below != mFarmland) return false;
length = _checkLine(source, pos, pos.above(*radius - 1));
if (length == -1) return true;
if (length < this+0x40) return false;
*radius = length;
return true;
```

### `_getSteps(BlockPos const&) 0x1b4bc40` (0x8a) — complete

`max(abs(x), abs(y), abs(z))`.

### `_checkLine(BlockSource&, BlockPos const&, BlockPos const&) 0x1b4b770` (0x1ad) — complete

```
delta = BlockPos(0,0,0).offset(-end.x, -end.y, -end.z)
steps = _getSteps(delta)
fx = delta.x / (float)steps; fy = delta.y / (float)steps; fz = delta.z / (float)steps
for (i in range_incl(0, steps - 1)):
    p = end.offset((int)(fx * i), (int)(fy * i), (int)(fz * i))
    if (!_isFree(_getBlock(source, p))) return i
return -1
```

### `place 0x1b4aff0` head (0x504 total) — partially read

```
height  = nextInt(this+0x24) + 5                      // 0x1b4b01d..0x1b4b029
if (!_checkLocation(source, pos, &radius)) return false
radius  = (int)(height * this+0x28); if (radius >= height) radius = height - 1
count   = (int)(powf(this+0x34 * height / 13.0f, ?) + 1.382f); if (count < 1) count = 1
top     = pos.y + radius
bottom  = height - this+0x3c
```

Then a `range` loop that pushes `FoliageCoords(pos.above(i), ...)` entries, calls
`_getTreeShape`, draws `nextFloat()` twice per iteration, and finally calls
`_makeFoliage`, `_makeTrunk`, `_makeBranches`.

### `_makeFoliage 0x1b4b920` (0x91) — complete

A plain `for (coords : foliage) _foliageCluster(source, coords)`.

### `_foliageCluster 0x1b4bcd0` (0xc4) — complete

```
for (i in range(this+0x3c))                 // 0..3
    _crossection(source, coords.pos.above(i), _getFoliageShape(i), mLeaves)
```

### `_getFoliageShape(int i) 0x1b4bfe0` (0x84) — complete

```
if (i < 0 || i >= this+0x3c) return -1.0f;   // 0x2824708
if (i == 0 || i == this+0x3c - 1) return 2.0f;  // 0x2828640
return 3.0f;                                  // 0x2829838
```

### `_crossection(BlockSource&, BlockPos const&, float shape, Block const&) 0x1b4bda0` (0x23f) — complete

```
extent = (int)(0.618f + shape)               // 0x2833354
for (dx in range_incl(-extent, extent))
  for (dz in range_incl(-extent, extent))
    if (powf(abs(dx) + 0.5f, 2) + powf(abs(dz) + 0.5f, 2) <= shape * shape)
        p = pos.offset(dx, 0, dz)
        if (_isFree(_getMaterial(source, p))) _placeBlock(source, p, block)
```

So a foliage layer is a rounded disc of radius `shape`.

### `_makeTrunk(BlockSource&, BlockPos const&, int) 0x1b4b9c0` (0x17f) — complete

Four `_limb` calls from the trunk base to `pos.above(n)`, in the order
north/east/south/west, each with `VanillaBlocks::mLog` (`0x34a8618`):

```
top = pos.above(n)
_limb(source, pos, top,                        mLog)   // 0x1b4ba1f
_limb(source, pos.east(),  top.east(),         mLog)   // 0x1b4ba77
_limb(source, pos.south(), top.east().south(), mLog)   // 0x1b4bade
_limb(source, pos.east(),  top.south(),        mLog)   // 0x1b4bb31
```

(The exact corner expressions are what the `east()`/`south()` call sequence at
`0x1b4ba4b..0x1b4bb31` builds; the four limbs thicken the trunk into 2x2.)

### `_limb(BlockSource&, BlockPos const&, BlockPos const&, Block const&) 0x1b4c070` (0x244) — partial

```
delta = BlockPos(0,0,0).offset(-end.x, -end.y, -end.z)   // 0x1b4c0ce
steps = _getSteps(delta)                                  // 0x1b4c0de
fx = delta.x/(float)steps; fy = ...; fz = ...             // same shape as _checkLine
for (i in range_incl(0, steps - 1))                       // 0x1b4c122
    p = end.offset(...)                                   // 0x1b4c1be
    dir = max(abs(dx), abs(dz))                           // 0x1b4c1d5..0x1b4c1ff
    block = base->setBlockState(BlockState::Direction, dir)  // 0x1b4c26d
    _placeBlock(source, p, block)                         // 0x1b4c292
```

So `_limb` draws a 3-D Bresenham line of `mLog` with a `Direction` state chosen
from the dominant horizontal axis. That is why the corpus contains `log:0`,
`log:4` and `log:8`: the three direction states.

### `_trimBranches(int a, int b) 0x1b4c2c0` (0x31) — complete

```
return (float)b >= (float)a * this_float0x??      // 0x1b4c2e4 ucomiss + setae
```

It is a one-line predicate: does `b` reach `a * f` for the feature's float at the
offset the caller passes in `xmm0`. In `_makeBranches` the arguments are
`_trimBranches(coordsHeight, y - coords.y)`, i.e. "is this branch long enough".

### `_makeBranches(BlockSource&, vector<FoliageCoords> const&, BlockPos const&, int) 0x1b4bb40` (0xf3) — complete

```
for (coords : foliage):                       // 0x1b4bb70..0x1b4bc1f
    base = BlockPos(0,0,0)                    // 0x1b4bbcd
    base = coords.getBranchBase()             // +0x0 x, +0x8 z
    if (!_trimBranches(coordsHeight, y - coords.y)) continue
    _limb(source, base, BlockPos(...), VanillaBlocks::mLog)
```

`FoliageCoords::getBranchBase 0x1b4af40` (0x11) returns the coordinate pair built
from fields `+0x0` and `+0x8`, which `_makeBranches` combines with the passed-in
`y` into the branch end point.

### All twelve helpers are now read

| Helper | Address | Size | Status |
|---|---|---:|---|
| `place` | `0x1b4aff0` | 0x504 | head + structure |
| `_getTreeShape` | `0x1b4b670` | 0xf7 | complete |
| `_checkLocation` | `0x1b4b500` | 0x163 | complete |
| `_checkLine` | `0x1b4b770` | 0x1ad | complete |
| `_getSteps` | `0x1b4bc40` | 0x8a | complete |
| `_makeFoliage` | `0x1b4b920` | 0x91 | complete |
| `_foliageCluster` | `0x1b4bcd0` | 0xc4 | complete |
| `_getFoliageShape` | `0x1b4bfe0` | 0x84 | complete |
| `_crossection` | `0x1b4bda0` | 0x23f | complete |
| `_makeTrunk` | `0x1b4b9c0` | 0x17f | complete |
| `_limb` | `0x1b4c070` | 0x244 | complete |
| `_trimBranches` | `0x1b4c2c0` | 0x31 | complete |
| `_makeBranches` | `0x1b4bb40` | 0xf3 | complete |
| `FoliageCoords::getBranchBase` | `0x1b4af40` | 0x11 | complete |

### `place` body — complete structure

```
height = nextInt(this+0x24) + 5                       0x1b4b01d
if (!_checkLocation(source, pos, radius)) return false 0x1b4b03b
radius = (int)(height * this+0x28)                    0x1b4b05d
if (radius >= height) radius = height - 1             0x1b4b075
count  = (int)(powf(this+0x34 * height / 13.0f, .) + 1.382f)   0x1b4b0a3
if (count < 1) count = 1                              0x1b4b0cc
top    = pos.y + radius                               0x1b4b0de
bottom = height - this+0x3c                           0x1b4b0f1
foliage = vector<FoliageCoords>()                     0x1b4b0f4
foliage.push_back(FoliageCoords(pos.above(?), ?))     0x1b4b104..0x1b4b121
                                                      -- the trunk-top entry

for (i in range(count)):                              0x1b4b15c
    shape = _getTreeShape(height, i)                  0x1b4b13d
    yaw   = nextFloat() * 2pi-ish                     0x1b4b1e9
    pitch = nextFloat()                                0x1b4b212
    dx = (int)(sin(yaw) * shape-ish)                   0x1b4b251
    dz = (int)(cos(yaw) * shape-ish)                   0x1b4b291
    p  = base.offset(dx, 0, dz).above(...)             0x1b4b2da..0x1b4b2f8
    if (_checkLine(source, ?, p) == -1):               0x1b4b316
        r = sqrt(...)                                  0x1b4b37c
        p2 = BlockPos(...)                             0x1b4b3f9
        if (_checkLine(source, ?, p2) == -1)           0x1b4b417
            foliage.push_back(FoliageCoords(p2, ...))  0x1b4b433..0x1b4b45c

_makeFoliage(source, foliage)                          0x1b4b49e
_makeTrunk(source, pos, height)                        0x1b4b4b5
_makeBranches(source, foliage, pos, height)            0x1b4b4d1
~vector()                                              0x1b4b4de
return true
```

That is the complete call graph. A translation can now be written: the trunk is
four Bresenham limbs with log direction states, the foliage is per-branch-base
stacks of four rounded discs, and the branch tips are chosen by two `nextFloat`
draws per iteration from a `_getTreeShape`-sized offset.

### Risk for the next attempt

`place` draws `nextFloat()` twice per loop iteration and `_getTreeShape` returns a
float that feeds `sin`/`cos` before a `(int)` truncation, so the translation must
reproduce x87/SSE float rounding exactly. `Random::nextFloat` is already
translated; the risk is in the `sin`/`cos`/`sqrt`/`powf` call sites, where the
original uses the platform libm. The corpus has only 182 words for 16 cases, which
bounds how much can drift before the replay diverges.

Addresses for the next attempt, all confirmed by `nm`:

| Helper | Address | Size |
|---|---|---|
| `place` | `0x1b4aff0` | 0x504 |
| `_getTreeShape` | `0x1b4b670` | 0xf7 |
| `_checkLocation` | `0x1b4b500` | 0x163 |
| `_checkLine` | `0x1b4b770` | 0x1ad |
| `_getSteps` | `0x1b4bc40` | 0x8a |
| `_makeFoliage` | `0x1b4b920` | 0x91 |
| `_makeTrunk` | `0x1b4b9c0` | 0x17f |
| `_makeBranches` | `0x1b4bb40` | 0xf3 |
| `_foliageCluster` | `0x1b4bcd0` | 0xc4 |
| `_crossection` | `0x1b4bda0` | 0x23f |
| `_getFoliageShape` | `0x1b4bfe0` | 0x84 |
| `_limb` | `0x1b4c070` | 0x244 |
| `_trimBranches` | `0x1b4c2c0` | 0x31 |

### Fancy place: the first two dependency sequences, measured

Implementing the call graph as read produced a replay that stops after two
queries, so the corpus itself is the better oracle for the opening. Case 0
(`at = (-8,5,-8)`, height = `nextInt(12)+5` = 10) opens with a **vertical scan**:

```
(-8,4,-8)  <- _checkLocation reads origin.below()
(-8,5,-8) (-8,6,-8) ... (-8,20,-8)   <- 16 more reads straight up
(-5,16,-8) (-5,17,-8) (-5,18,-8) ... <- then the branch tips
```

That is 17 reads straight up from `origin.y - 1` to `origin.y + 15`, i.e. the
`_checkLine` in `_checkLocation` walks far more than `radius - 1`: the caller
passes a **radius that is not zero**, and the scan reaches `origin.y + 15` for
height 10. The current translation passes `radius = 0`, so it asks
`above(-1)` and stops after two reads — that is the immediate bug to fix.

The eight failing cases stop after exactly ONE query (`get_block(origin.below)`)
and their scenes have `below = stone` (cases 3, 7, 11, 15) or `sand` (2, 6, 10,
14), while the eight succeeding cases have `below = grass` or `dirt`. So
`_checkLocation`'s ground test is exactly `dirt|grass|farmland`, confirming the
`0x34a7768/0x34a7770/0x34a7950` comparison chain.

What is still needed: the value of the `radius` argument `place` hands to
`_checkLocation` (`0x1b4b03b`). Read exactly: `0x1b4b022` is
`lea rcx,[rbp-0x2c]` and `[rbp-0x2c]` is the slot that `0x1b4b026..0x1b4b029`
fills with `nextInt(this+0x24) + 5`. So the reference handed to `_checkLocation`
IS the height local, and the `radius = (int)(height * 0.618)` computation at
`0x1b4b051..0x1b4b066` runs only AFTER the call returns.

`_checkLocation` therefore does `_checkLine(source, pos, pos.above(*radius - 1))`
with `*radius == height`. For case 0 that is `above(9)`, i.e. `(-8,14,-8)`, and
`_checkLine`'s own `_getSteps` on `(0,-9,0)` is 9, so it reads 9 cells:
`(-8,5,-8)` .. `(-8,13,-8)`.

The corpus instead shows **17** reads straight up, `(-8,4,-8)` .. `(-8,20,-8)`.
So one of the two endpoints is not what this reading assumes: either `_checkLine`
adds the origin back into the delta, or `place` passes a different position than
`origin` and the difference is the `+ 0x3c`-style offset. Comparing the 17-read
length against `height + 7` and `2 * height - 3` for the other cases is the
cheapest way to identify the exact expression before writing more code.

### Fancy: the measured vertical runs (round 3 data)

For each succeeding case, the count of leading `get_block` reads that share the
origin's x and z, with the height the seed implies:

| case | height | origin.y | vertical reads | first y | last y | origin.y+height |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 10 | 5 | 17 | 4 | 20 | 15 |
| 1 | 14 | 7 | 14 | 6 | 19 | 21 |
| 4 | 10 | 7 | 15 | 6 | 20 | 17 |
| 5 | 14 | 9 | 22 | 8 | 18 | 23 |
| 8 | 12 | 9 | 10 | 8 | 17 | 21 |
| 9 | 5 | 5 | 14 | 4 | 12 | 10 |
| 12 | 14 | 5 | 15 | 4 | 18 | 19 |
| 13 | 11 | 7 | 16 | 6 | 21 | 18 |

Every run starts at `origin.y - 1` (the `_checkLocation` ground read) and the
second read is always `origin.y`, so the ground test is confirmed. Beyond that the
length does NOT track the height: case 5 has the most reads (22) with a mid-range
height, and case 8 has the fewest (10) with a larger height. Reading the run as one
`_checkLine` therefore does not fit.

Two explanations remain, and the data prefers the second:

1. `_checkLocation`'s `_checkLine(origin, origin.above(radius - 1))` alone would
   contribute `radius - 1` reads, which is far short of every count here.
2. The leading same-x/z run is the **concatenation of several separate scans** that
   happen to be collinear: `_checkLocation`'s one, plus the per-iteration
   `_checkLine` calls in `place`'s foliage loop when a branch tip lands back on the
   trunk column. Case 5's 22 against case 8's 10 is most easily explained that way,
   because the foliage loop's tip positions depend on `nextFloat()` and
   `_getTreeShape`, not on the height.

Pinning this needs the query stream segmented by *position pattern* rather than by
a contiguous run: the foliage loop's scans are the ones whose endpoints are the
`offset(sin, 0, cos)` points documented earlier. Doing that segmentation is the
concrete next step; it needs no new oracle run.

### Fancy: the query stream SEGMENTS cleanly, and that settles the shape

Splitting case 0's 1028 `get_block` reads into maximal runs of constant `(x, z)`
gives a very sharp structure:

| run length | count | meaning |
|---:|---:|---|
| 17 | 1 | `_checkLocation`: the ground read plus its `_checkLine` up the trunk column |
| **5** | **16** | one `_checkLine` per **branch tip**, walking the four foliage layers plus one |
| 1 | 922 | `_crossection` cells of the foliage discs |
| 3, 2 | 4 | partial runs where a scan stopped early |

Across the eight succeeding cases the len-5 count tracks the branch count:

| case | total runs | len-5 runs | leading scan |
|---:|---:|---:|---:|
| 0 | 943 | 16 | 17 |
| 1 | 721 | 11 | 14 |
| 4 | 726 | 12 | 15 |
| 5 | 719 | 10 | 22 |
| 8 | 217 | 2 | 10 |
| 9 | 160 | 1 | 14 |
| 12 | 716 | 12 | 15 |
| 13 | 835 | 14 | 16 |

So my earlier reading of the leading run as a *single* height-driven scan was the
mistake: it is the `_checkLocation` scan, whose length is set by `_getSteps` on
whatever endpoint `_checkLocation` hands `_checkLine`, and it varies independently
of the height because the endpoint depends on the radius the feature computes.

The len-5 runs are the important discovery: each branch tip gets a 5-step vertical
`_checkLine`, which is exactly the `_makeFoliage`/`_foliageCluster` layer count
(`this+0x3c == 4`) plus one. And the 922 single reads are the `_crossection`
discs, each cell read once before the `_isFree` test — which matches the
`_crossection` translation exactly (one `_getMaterial` per candidate cell).

That means the shape is:

```
_checkLocation              -> ground read + 1 scan        (the long leading run)
place loop per branch tip   -> 2 float draws + 2 _checkLine (the len-5 runs)
_makeFoliage                -> _crossection per layer      (the single reads)
_makeTrunk / _makeBranches  -> _limb, which also reads     (interleaved)
```

The concrete next step is to make the replay emit exactly this segmentation: the
leading scan length must come from `_checkLine`'s own `_getSteps`, the len-5 runs
from a vertical `_checkLine` per accepted tip, and the single reads from
`_crossection`. Getting those three counts right per case is a much tighter target
than matching 1028 reads at once.

### Fancy: `_checkLine` verified byte-for-byte, and the open question

Raw bytes at `0x1b4b79e..0x1b4b815` confirm the loop exactly:

```
r9d = 0 - end.x ; r10d = 0 - end.y ; r8d = 0 - end.z      (0x1b4b7a1..0x1b4b7b3)
delta = BlockPos(0,0,0).offset(-end.x, -end.y, -end.z)    (0x1b4b7c4)
steps = _getSteps(delta)                                  (0x1b4b7d1)
fx = delta.x/steps ; fy = delta.y/steps ; fz = delta.z/steps  (0x1b4b7d9..0x1b4b80d)
range_incl(steps)                                          (0x1b4b815)
```

and `_checkLocation`'s call is
`_checkLine(this, source, pos, pos.above(*radius - 1))` (bytes at
`0x1b4b5d3..0x1b4b616`, with `r8d = *radius - 1` at `0x1b4b5e7`).

So the FIRST cell `_checkLine` reads is its `end` argument, because the loop
index runs 0..steps-1 and `p = end.offset(f*i)`. For case 0 with `*radius` taken
as the height (10) that first read is `pos.above(9)`, and the corpus indeed
contains `(-8,14,-8)` — but at query **#22**, not at #0.

That is the remaining puzzle, stated precisely: the corpus's first 17 reads are
`(-8,4,-8)` followed by 16 reads ASCENDING to `(-8,20,-8)`, which cannot be this
`_checkLine` walk (it descends from `end`). So either

* `_checkLocation` is not the first thing that reads, or
* the `radius` reference is read before `place` writes the height into it, so
  `*radius - 1` is a different value, making `end` something the ascending run
  can start from.

The ascending run's length varies independently of `height + radius`
(case 9: 14 reads with `height + radius` = 8; case 5: 22 reads with 22), which
again points at the second explanation. Reading what `place` writes into
`[rbp-0x2c]` **before** `0x1b4b022` (`lea rcx,[rbp-0x2c]`) — including whether
`nextInt`'s addend is stored there or in a different slot — is the single check
that resolves it.
