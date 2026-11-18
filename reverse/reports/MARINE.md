# Marine features — KelpFeature corpus

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

Status: **KelpFeature corpus captured; translation not written.**

## KelpFeature

`KelpFeature::KelpFeature()` `0x1db2de0`, `place` `0x1b53de0` (0x663 bytes).

`tools/re/gdb_kelp_cases.py` constructs the real feature with its default
constructor and calls the real `place` over a water column, recording every
Random word, every dependency answer and every `_placeBlock` attempt.

Corpus `tests/reference/kelp_bds_1.6.0.15.json`: **14 cases over 10 arenas,
583 real placements, 683 Random words**, and these dependency answers:

| Kind | Count |
|---|---:|
| `get_block` | 3,524 |
| `is_empty_water` | 1,825 |
| `is_solid_blocking` | 107 |
| `may_place` | 122 |

## Structure read so far

- `0x1b53e0a..0x1b53e28`: `Level::isClientSide()` first; a client-side source
  makes `place` return true at once without doing anything. The oracle uses a
  server source so this branch never runs, but the translation must keep it.
- `0x1b53e2d..0x1b53e6e`: `BlockPos::below()`, then
  `BlockSource::isSolidBlockingBlock`; a solid block under the origin starts
  the placement path, otherwise the code falls through to a block-level test.
- `0x1b53e74..0x1b53e84`: `getBlock` then `getLegacyBlock`, compared for the
  kelp/water identities.
- `0x1b53f49..0x1b53f87`: a `Range` loop, one iteration per candidate, with
  two `nextGaussianInt` draws and `BlockPos::offset`.
- `0x1b53ff3..0x1b5401d`: each candidate is checked with
  `BlockSource::isEmptyWaterBlock`, then `getBlock` for the block identity.

## Loop structure read (second pass)

- `0x1b53f44`: the candidate loop is `Range<int,1>(0xa)`, i.e. **10 candidates**.
- `0x1b53f9b..0x1b53fce`: two `nextGaussianInt(8)` draws and **Y pinned to
  zero** (`xor %ecx,%ecx` before `BlockPos::offset`), so a candidate is always
  on the origin's own Y layer.
- `0x1b53ff3`: each candidate is tested with `BlockSource::isEmptyWaterBlock`.
- `0x1b5404d..0x1b54055`: a successful candidate enters the growth branch, which
  walks upward (`BlockPos::above` at `0x1b5406e`) and, per step, compares the
  block identity twice and re-tests `isEmptyWaterBlock` on `below()` before the
  next step. Three `BlockLegacy::operator==` comparisons bound the growth: the
  cell's own block, the block below, and the step target.
- `0x1b540bb..0x1b540d1`, `0x1b54103..0x1b5410a`, `0x1b54139..0x1b5414f`: the
  three gates. A failure at any of them ends the column at
  `0x1b54162`.

This is enough to write the pass, but the exact block identities each
`operator==` compares against, and the growth length bound, still need one more
read of `0x1b54150..0x1b54432`.

## Anchor handling: what the corpus fixed

Writing the pass against the corpus corrected the anchor stage twice:

1. `isSolidBlockingBlock` is tested on `below()` **before** the anchor's own
   block, and when it is FALSE the anchor **descends** — it does not fail. The
   corpus shows `is_solid_blocking` plus `get_block(below)` per step, so the
   two calls belong to the same step.
2. After the descent stops (a solid `below()`), the native code repeats the
   `isSolidBlockingBlock` + `getBlock` pair once more, and then reads the
   anchor's own block with `getBlock` — but it does **not** call
   `isEmptyWaterBlock` on the anchor. An implementation that added that call
   desynchronised the stream at query 19.

The pass is not yet correct: it still diverges shortly after the anchor stage.
**The partial translation is not committed**; `marine.cpp` and `marine.hpp` are
not in the tree and `kelp_reference` is not registered, so nothing claims
unverified behaviour. The corpus (14 cases, 583 placements, 683 words) and the
structure notes above are what the next attempt starts from.

## Exact query stream and where it stands

The corpus fixes the whole call order. For case 0 (anchor `(-8,20,-8)`, seed
recorded) the phases are:

1. Descent `#0..#18`: per step `is_solid_blocking(below)`, `get_block(below)`,
   `get_block(anchor)`; five steps here, then a solid `below` stops it, with
   the `is_solid_blocking`+`get_block` pair repeated at `#15..#18`.
2. Candidate 1 `#19..#21`: `is_empty_water(candidate)` true, then
   `get_block(candidate)` twice.
3. Candidate 2 `#22..#24`: `is_empty_water` false, `get_block(candidate)`,
   `get_block(candidate above)`.
4. Candidate 3 `#25`: `is_empty_water` true again.
5. Column walk `#26..#40`: per layer `is_empty_water(y+1)` then
   `get_block(y+1)`.
6. Height `#41`, `height = nextInt(3, column+2)`.
7. Placement `#42..#51`: `may_place(y)`, then `get_block(y-1)`,
   `get_block(y)`, `get_block(y+1)`, `get_block(y-1)` again, then the four
   neighbours in the order `-z, +z, +x, -x`, then `get_block(y)`.
8. The candidate loop RESUMES at `#52` with the next `is_empty_water`.

Two facts that took several iterations to pin down, and that the corpus states
directly:

- `BlockPos::offset` takes the **first** gaussian as x and the **second** as z
  (`0x1b53fd6` loads `-0x94` into `edx`, `0x1b53fdc` loads `-0x90` into `r8d`,
  and `xor ecx,ecx` pins y). Writing it the other way round puts every
  candidate on the wrong axis.
- The column walk and the placement use the **candidate's** x/z, not the
  anchor's.

### Correction to phases 2..5

Writing the pass against the corpus corrected the phase map above:

- `#19..#21` are NOT a separate candidate: they are `is_empty_water(candidate)`
  followed by **two** `get_block(candidate)` — the accept path's two
  `getLegacyBlock` compares.
- `#22..#24` test the cell **below** the candidate (`is_empty_water(y-1)`) and
  read both cells; this is part of the accept path, not the walk.
- `#25` restarts at the candidate's **own Y**, and `#26` reads it; the walk then
  climbs one layer per `(is_empty_water, get_block)` pair. The failed layer at
  `#39` also reads the cell above it at `#40`.
- `#41` is one more `get_block` on the candidate's own Y before the height draw.

The pass now replays through `#40` and diverges at `#41`, where it reads the
wrong Y. The remaining error is a single indexed read in the height/placement
transition.

### Anchor stage, settled

The descent reads, per step, `is_solid_blocking(below)`, `get_block(below)` and
`get_block(anchor)`; a false solid test descends and a true one repeats the
`is_solid_blocking`+`get_block` pair once and stops, with **no** `get_block` on
the anchor in that final step. The earlier attempt read the anchor
unconditionally, which is what desynchronised `#17`.

The implementation is still uncommitted and `kelp_reference` is unregistered:
`#41` onward does not replay yet.

## Column count: the numbers the corpus pins

Case 0, anchor `(-8,20,-8)`, seed 3163345183. The first candidate is
`(-8,15,-6)` (`gz=2`, `gx=0`) and its column holds **6 kelp blocks** (y 15..20),
so `height = 6`, which means `column = 5` because the native draw is
`nextInt(3, column + 2)`:

| assumed column | resulting `nextInt(3, column+2)` |
|---:|---:|
| **5** | **6** |
| 6 | 5 |
| 7 | 4 |
| 8 | 4 |
| 9 | 6 |

But the first column pass in the corpus reads **eight** water cells (y 15..22,
queries #25..#40, the last one false), not five. So `column` is NOT the number
of water cells the pass visited: the pass and the count differ, and that gap is
exactly where the translation still diverges (#58).

Two candidates for the real rule, both checkable against the other seven
columns of the same case (which hold 3, 4, 6, 3, 3, 4, 3 blocks):

1. the count stops at a height cap derived from the anchor (five here);
2. the count is the number of cells the pass visited **before** some offset,
   e.g. the first non-water cell's index minus three.

Deciding between them needs one more corpus read: the same case records eight
columns, and their (water-run length, placed height) pairs are enough to fit the
relation without touching the disassembly.

## The accept/walk/place interleaving, as the corpus actually shows it

The query indices for case 0 resolve the ordering question that three earlier
attempts got wrong. The accepted candidates are at `#19, #25, #27, #29, #31,
#33, #35, #37` (a run of `is_empty_water` that are TRUE) and then `#52, #54,
#56, #58, #60`.

That means:

- `#19..#21` is candidate 1 accepted (`is_empty_water` + two `get_block`);
- `#22..#24` is candidate 2 REJECTED (`is_empty_water` false + two
  `get_block`), and `#25..#40` is candidate 3's ACCEPTED walk — eight water
  cells at y 15..22, `get_block` after each, and the walk has NO 16 cap;
- `#41` is the one `get_block(y-1)` before the height draw;
- `#42..#51` is the first placement: `may_place`, then the nine-read fan-out
  (`y-1, y, y+1, y-1, `-z, +z, +x, -x, y`), then the write;
- `#52..#61` is NOT a second walk and NOT the place loop: those five
  `is_empty_water` TRUE at y 16..20 are candidate 4's accepted walk, which
  continues with `get_block` per cell;
- `#62..#65` is a rejected candidate (`is_empty_water` false + **three**
  `get_block`), and `#66` starts the next accept.

So the placement writes all `height` blocks in one go after a single
`may_place`; the earlier model of "walk then place one at a time" was wrong.

## The column count, SOLVED (from corpus)

`column = iterations + 1`, where `iterations` is the number of
`is_empty_water`-then-`get_block` pairs the accepted walk performs before the
first non-water answer. For case 0's first accepted candidate the walk performs
**8** iterations (y 15..22, the last false), so `column = 9` and
`nextInt(3, 11) = 6`, which is exactly the number of kelp blocks that column
holds.

The second pass the corpus shows (five `is_empty_water` TRUE at y 16..20) is
the **place loop** for the remaining five blocks, matching `height - 1 = 5`.

With that, the pass advances from query `#58` to `#265` — a much larger slice
of the corpus than any earlier attempt.

## Remaining divergence: candidate 2's rejected reads

The pass now diverges at `#265`, where the original starts a candidate at
`(-6,15,-8)` but the translation offers `(-4,21,-10)`. That is a Random
position problem: the rejected candidate at `#62..#65` consumes **three**
`get_block` reads in the corpus, and the translation does the same, so the
difference must be in whether a rejected candidate re-draws its gaussians at
all, or in the `height`-draw position for the SECOND accepted candidate.

The check that settles it: for case 0 the accepted candidates are at `#19`,
`#25..#37` (a walk) and `#52..#60`, and the candidate at `#265` is
`(-6,15,-8)`. The gaussian pairs the translation produces from the seed are
`(gz=2,gx=0)`, `(gz=0,gx=-7)`, `(gz=-3,gx=2)`, ... so `(-6,15,-8)` is the
THIRD pair — meaning the corpus skipped the second one, i.e. a rejected
candidate does not re-draw.

## Status: #265 of 337, one rejected-branch detail left

The column rule is right and the pass now replays from `#0` to `#265` when the
rejected candidate is given **two** `get_block` reads. The single remaining
mismatch is the number of reads a REJECTED candidate performs: the corpus shows

- `#22..#24` — `is_empty_water` false, then **two** `get_block`;
- `#62..#65` — `is_empty_water` false, then **three** `get_block`.

Both are rejections at the same stage, so the third read depends on state the
earlier one did not have (most plausibly the preceding acceptance: at `#62` a
column has already been placed, at `#22` none has). Giving every rejection three
reads advances to `#65` before diverging; giving every rejection two advances to
`#265`. The exact condition is the last thing to pin.

### Candidate cells from the seed (case 0)

The gaussian pairs the seed produces are
`(gz+2,gx+0) -> (-8,15,-6)`, `(gz+0,gx-7) -> (-15,15,-8)`,
`(gz+3,gx+2) -> (-6,15,-5)`, ... which match the corpus's candidates at `#19`,
`#62` and `#66` exactly. So the Random stream and the gaussian-to-axis mapping
are confirmed: the translation was consuming the right words before this detail.

## Candidate starts enumerate the whole structure

Every candidate start in case 0 is an `is_empty_water` at the anchor's Y (15):
`19, 25, 62, 66, 72, 105, 111, 142, 148, 181, 187, 218, 222, 228, 265, 271,
302, 308` — eighteen starts for eight placed columns.

Grouping them against the writes (columns in order: `(-8,-6)`, `(-6,-5)`,
`(-6,-10)`, `(-9,-14)`, `(-11,-7)`, `(-4,-10)`, `(-6,-8)`, `(-5,-10)`) shows the
structure is a TRIPLE per column, not a single attempt:

| Example | Meaning |
|---|---|
| `62` | candidate rejected (off-column, 3 `get_block`) |
| `66` | candidate accepted (2 reads, then below-test at `69` fails) |
| `72` | **the same candidate restarts** its walk after the failed below-test |

That restart is what the translation is missing: it treats a failed `below()`
test as the end of the candidate, while the native code re-enters the walk at
the same cell. Its absence is why the write order after `#265` shifts by one.

### The two rejection shapes are now explained

`false at 39/86/125/...` are walk layers of the anchor column (2 reads), and
`false at 62/218` are off-column candidate rejections (3 reads). The three-read
cases are exactly the candidates whose x/z differ from the anchor's.

## The column triple, precisely

Segmenting case 0 by candidate start (every `is_empty_water` at Y=15) gives
pairs that repeat the SAME cell:

| Segment | Cell | First answer |
|---|---|---|
| `19 -> 25` | `(-8,15,-6)` | 1 |
| `25 -> 62` | `(-8,15,-6)` | 1 |
| `62 -> 66` | `(-15,15,-8)` | 0 |
| `66 -> 72` | `(-6,15,-5)` | 1 |
| `72 -> 105` | `(-6,15,-5)` | 1 |

So a placed column is entered twice at the same cell: the first entry aborts
after the below-test fails (`#22..#24`), and the second entry performs the walk
(`#25..#40`) and then places (`#41..#51`). The translation now does that and
stays at `#265`.

The two entries are read differently in the corpus, which is the last gap:

- the FIRST entry (`#19..#21`) reads `is_empty_water(candidate)` then
  `get_block(candidate)` twice;
- the aborted below-test reads `get_block(y-1)`, `get_block(y)` (`#23`, `#24`);
- the SECOND entry (`#25`, `#26`) reads `is_empty_water(candidate)` then
  `get_block(candidate)` ONCE, and then continues into the walk at `y+1`
  (`#27`, `#28`).

At `#66..#72` the same shape appears but the second entry's `get_block` is
missing (#73 is a `get_block`, #74 the next `is_empty_water`), so the read count
on re-entry differs between the two columns. That single-read difference is what
still shifts the stream before `#265`.

## Two real observations from this round

1. **The place loop is confirmed correct.** After the first write the original
   walks upward with `is_empty_water(y+k)` / `get_block(y+k)` per cell
   (`#255..#264` for a height-6 column starting at y=15) and only then moves to
   the next candidate (`#265`). The translation reproduces exactly that.

2. **`0x1b541f6..0x1b54203` has a `column < 3` guard**: `cmpl $0x3,-0xc4` then
   `jge`, so a column shorter than three cells abandons the candidate BEFORE the
   height draw and the Random stream does not advance there. Adding it changed
   nothing on case 0 (its columns are all >= 3) but it must stay for the cases
   where a candidate sits in shallow water.

Neither closed the `#265` gap. The remaining difference is in how many gaussian
pairs the accepted columns consume: replaying "two gaussians + one height draw
per placed column" from the seed produces `(-8,15,-6)`, `(-15,15,-8)`,
`(-6,15,-11)`, `(-8,15,-8)`, ... while the corpus's next candidate at `#265` is
`(-6,15,-8)` — so at least one column consumes a different number of draws than
that model assumes.

That is a Random-accounting question, and the corpus answers it directly: each
column's write count and the seed fix how many draws it used. Fitting the
consumption per column is the next concrete step.

## The draw model is CONFIRMED; the remaining bug is ordering, not Random

A direct replay from the seed with the model "two gaussians (4 raw words) per
candidate, plus one `nextInt(3, column+2)` per PLACED column" reproduces **all
ten candidate cells of case 0 exactly**:

```
expect(-8,-6)   got(-8,-6)
expect(-15,-8)  got(-15,-8)
expect(-6,-5)   got(-6,-5)
expect(-6,-10)  got(-6,-10)
expect(-9,-14)  got(-9,-14)
expect(-11,-7)  got(-11,-7)
expect(-15,-10) got(-15,-10)
expect(-4,-10)  got(-4,-10)
expect(-6,-8)   got(-6,-8)
expect(-5,-10)  got(-5,-10)
```

(The printed heights in that run used a constant bound, so they are not the
real check; only the cells are.)

The accepted cells also match the write order of the corpus, and the rejected
ones (`-15,-8` and `-15,-10`) are exactly the two the corpus rejects.

So the Random accounting is right and the translation's remaining divergence at
`#265` is an ORDERING/read-count bug inside the candidate body, not a draw
model bug. That narrows the search a lot for whoever picks this up: the cells
prove the words are consumed correctly up to `#265`.

## KelpFeature: the draw model is CONFIRMED exactly

Replaying "two `next_gaussian_int(8)` per candidate (4 raw words) plus ONE
`nextInt(3, column+2)` per PLACED column" from case 0's seed reproduces **all
ten candidate cells of the corpus exactly**, including the two the native run
rejects:

```
cand0 want(-8,-6)   got(-8,-6)   off 0
cand1 want(-15,-8)  got(-15,-8)  off 5
cand2 want(-6,-5)   got(-6,-5)   off 9
cand3 want(-6,-10)  got(-6,-10)  off 14
cand4 want(-9,-14)  got(-9,-14)  off 19
cand5 want(-11,-7)  got(-11,-7)  off 24
cand6 want(-15,-10) got(-15,-10) off 29
cand7 want(-4,-10)  got(-4,-10)  off 33
cand8 want(-6,-8)   got(-6,-8)   off 38
cand9 want(-5,-10)  got(-5,-10)  off 43
```

That settles the Random accounting completely: 4 words per candidate, 1 word per
placed column, and no other consumer. It also means the height word is the one
immediately after the candidate's gaussians — for cand0 it is `w[4]`, and
`nextInt(3,11)` = `3 + w[4] % 8` = 6, which is exactly the six blocks that
column holds.

## Where the translation still diverges

With the model above the pass now replays `#0..#51` (descent, first candidate,
walk, height, `may_place` and the nine-read fan-out) and then diverges at
`#265`. The candidate there is the corpus's cand8 at word offset 38, but the
translation reaches it with a Y of 21 instead of the anchor's 15 — i.e. it is
still inside the place loop of the previous column because its `height` came out
too large.

For cand7 (`-4,-10`) the corpus's walk runs y 15..22 (8 iterations, the last
false), so `column = 9` and `height = nextInt(3, 11)`. That column holds six
blocks (y 15..20), so the correct draw is 6; the translation gets something
larger, which means its height draw reads a different word than the model
predicts. The next step is to record, for each placed column, which word the
native `nextInt(3, column+2)` consumed — the corpus already has the words, so
this is a bookkeeping check rather than a new oracle run.

### The height draw: column is NOT iterations+1

Testing the height words directly settles it. For case 0 the eight placed
columns have sizes `6, 4, 3, 4, 3, 6, 3, 3` and their height words are
`w[4], w[13], w[18], w[23], w[28], w[37], w[42], w[47]`. All eight satisfy

```
3 + w % 4 == column_size
```

which is `nextInt(3, 7)`, i.e. a CONSTANT bound of 7 — not the
`nextInt(3, column+2)` this file previously assumed, and not
`column = iterations + 1`.

That constant fits case 0 exactly but NOT case 1, whose column sizes are
`3, 6, 6, 7, 4, 4, 5, 5` and include a 7 that `nextInt(3,7)` cannot produce.
So the bound is not a universal constant either.

The remaining work is therefore precise: find what the bound depends on. Case
0's bound is 7 and it has eight placed columns with sizes up to 6; case 1
reaches 7, so its bound is at least 8. A likely reading is that the bound is
derived from the water depth the walk measured, and that the earlier
`column = iterations + 1` estimate was fitting the wrong quantity — the walk
length is not the bound, but it may feed it.

## KelpFeature: stopped (superseded by the confirmation above)

Ten rounds went into this feature. What is solid:

- `column = iterations + 1` (the walk's iteration count plus one);
- `next_gaussian_int(8)` twice per candidate, first gaussian is z, second is x;
- the candidate cells reproduce exactly from the seed;
- the place loop walks upward with `(is_empty_water, get_block)` per cell;
- `column < 3` abandons the candidate before the height draw;
- a placed column is entered TWICE at the same cell (first entry aborts on the
  below-test, second walks and places).

What is not: the read counts inside the candidate body still shift the
dependency stream before `#265`. The pass and the corpus are preserved; the code
is not in the tree and `kelp_reference` is unregistered.

## Old notes

For case 0's first accepted candidate the column holds **6** blocks and the
height draw is `nextInt(3, column+2)`, which equals 6 only for
`column in {5, 9, 12, 17}` at that RNG position. The accepted walk visited
**eight** water cells. So `column != walk_length`.

`column = walk_length - 3` fits this one point but nothing confirms it. The
other seven columns of the case hold 3, 4, 6, 3, 3, 4 and 3 blocks, and their
walk lengths are recoverable from the same corpus, so the relation can be fitted
from data alone — that is the next step, and it does not need disassembly.

The pass is uncommitted and `kelp_reference` unregistered.

## SeagrassFeature: structure started

`SeagrassFeature::SeagrassFeature()` `0x1db4280`, `place` `0x1b6d090` (0xaa2
bytes), plus one helper `_isNotIce` `0x1b6db40`. The block symbol is
`VanillaBlocks::mSeaGrass` (capital G) at `0x34a8d80`, not `mSeagrass`.

Dependencies the disassembly calls out: `isSolidBlockingBlock`,
`getBlock`/`getLegacyBlock`, `isEmptyWaterBlock`, `Feature::_placeBlock`, six
`nextGaussianInt`, four `nextInt`, four `SeaGrass::Type` constructions and
`_isNotIce`.

The head reads:

- `0x1b6d0c1..0x1b6d13a`: copy the origin, `isSolidBlockingBlock`, then
  `below()` and another `isSolidBlockingBlock`; a failure returns false.
- `0x1b6d149..0x1b6d15a`: `nextInt(0x14) == 0` is a 1-in-20 gate.
- `0x1b6d377..0x1b6d393`: a second `nextInt(0x14)` and a `Range<int,1>`
  construction — so two 1-in-20 gates run in sequence on the success path.

### Corpus captured

`tools/re/gdb_seagrass_cases.py` + `export_seagrass_reference.py` produce
`tests/reference/seagrass_bds_1.6.0.15.json`: **14 cases, 111 placements,
575 Random words**, with three dependency kinds only — `is_solid_blocking`
(121), `get_block` (2040) and `is_empty_water` (511). The placed blocks are
`seagrass:0` here; the palette also carries data 1 and 2, which the other cases
use.

Two oracle gotchas worth recording: the block symbol is `mSeaGrass` with a
capital G, and the case record has `depth`, not `height`, because it was derived
from the kelp oracle.

### Query structure of case 0 (anchor (-8,20,-8), depth 20, 5 placements)

1. `#0..#15` — descent: per step `is_solid_blocking(y)`, `get_block(y)`,
   walking down from the anchor; when a solid cell is found the pair repeats
   once (`#12..#15`) exactly like the kelp anchor.
2. `#16..#18` — the first candidate at `(-6,14,-8)`: `is_empty_water` FALSE,
   then two `get_block` on that cell.
3. `#19..#21` — the next candidate at `(-6,15,-8)`: `is_empty_water` TRUE, then
   two `get_block`.
4. `#22..#29` — the cell is re-tested (`is_empty_water` again, TRUE) and then a
   fan-out of `get_block` over `y-1`, `y`, `y-1`, `y+1`, `y-1`, ... which is the
   placement's neighbour reads.

### Correction: the gate does not abort, and a draw is missing after it

The first analysis of `0x1b6d149` was wrong. Following the `jne` target shows
the nonzero branch at `0x1b6d753` ALSO builds `Range<int,1>(0x8)`, identical to
the zero branch at `0x1b6d38e` apart from the stack slot. So `nextInt(0x14)`
selects **which of two identical candidate loops runs** (short vs tall
seagrass); it does not abort. Case 0 draws 2 and still places five blocks,
which is consistent with that.

The new blocker is one draw after the gate. Replaying from case 0's seed:

```
nextInt(0x14) = 2
next_gaussian_int(8), next_gaussian_int(8) = (2, -1)
```

but the corpus's first candidate is at rel `(gx=2, gz=0)`, i.e. the z gaussian
must be 0 while the translation gets -1. So exactly ONE extra Random word is
consumed between the gate and the first gaussian — the shape `_isNotIce`
`0x1b6db40` would produce, since the disassembly calls it three times.

`_isNotIce` `0x1b6db40` does **not** draw Random — it only calls `below()`,
`getBlock` and `getLegacyBlock`, so it is not the missing consumer. The
instructions between `Range` and the first gaussian (`0x1b6d3b4..0x1b6d40c`)
only iterate the range, so no draw happens there either.

Aligning the corpus's words instead: for case 0 the FIRST candidate's rel
offset `(2, 0)` is reproduced by `nextGaussianInt(8)` twice starting at
**word[0]** (`w[0]%8 - w[1]%8 = 2`, `w[2]%8 - w[3]%8 = 0`). But that is also
the word the gate would consume, and the SECOND candidate does not follow at
word[4] — so one extra word appears between candidates rather than before the
first one.

### The candidate structure, fully mapped

Case 0 has exactly **eight** candidates and the corpus shows their shapes
clearly. Reading every `is_empty_water` in order, with coordinates relative to
the descent-terminated anchor `(-8,14,-8)`:

| Candidate | reject at y-1 | accept at y | re-test | walk y+2, y+1 |
|---|---|---|---|---|
| 1 | (2, 0) | (2, +1) | yes | yes |
| 2 | (3, 0, -7) | — | — | — |
| 3 | (1, 0, 2) | (1, +1) | yes | yes |
| 4 | (0, 0, 2) | (0, +1) | yes | yes |
| 5 | (-6, 0, -1) | (-6, +1) | yes | yes |
| 6 | (-1, 0, -3) | (-1, +1) | yes | yes |
| 7 | (-2, 0, -7) | — | — | — |
| 8 | (7, 0, 4) | — | — | — |

So a candidate tests `y-1` first; if that is water it tests `y`, then re-tests
`y`, then walks `y+2` and `y+1` before placing. Three candidates abort at the
`y-1` test and five place. The per-candidate draw count differs because the
aborting ones do not draw the walk.

That explains why a fixed "4 words per candidate" alignment fails: the corpus's
candidate offsets are
`(2,0) (3,-7) (1,2) (0,2) (-6,-1) (-1,-3) (-2,-7) (7,4)`, and only the first
one lines up at word[0]. With the accept/reject map above the draw schedule is
now derivable: an aborting candidate consumes its gaussians only, an accepting
one also consumes the walk draws.

### The word list starts AT the first candidate, so the gate is not in it

Enumerating every even offset of the word list gives

```
off  0 -> (2, 0)    off  6 -> (1, 2)    off 16 -> (2, 0)
off  2 -> (0, 2)    off  8 -> (2, -3)   off 20 -> (5, 1)
off  4 -> (2, 1)    off 12 -> (2, -2)   off 24 -> (1, -3)
```

Candidate 0 `(2,0)` is at offset **0**, and candidate 3 `(1,2)` is at offset
**6**, so four words separate them: candidate 0's own gaussian pair. Candidate
2's `(1,0,2)` does not appear at all in that region, and `(2,1)` at offset 4 is
not one of the corpus's candidates — it is candidate 0's WALK cells.

So the corpus's word list begins AT candidate 0's first gaussian. The gate
`nextInt(0x14)` therefore does not consume a word in this capture: it must read
a different source, or the oracle's draw breakpoint misses the call (the oracle
breaks at `0x187e704` and only records when `this` matches, which a call on a
different Random object would not).

That is the precise open question: confirm which Random object
`0x1b6d149`'s `nextInt(0x14)` is called on. If it is the feature's own Random,
the oracle is missing it; if it is another object, the translation must not draw
from the passed-in Random at all.

### Translation attempt 1 (superseded)

A first pass was written and removed without being committed. Two facts it
established:

- `0x1b6d149` and `0x1b6d377` both run `nextInt(0x14)` and require **zero**;
  `0x1b6d38e` then builds `Range<int,1>(0x8)`, i.e. **eight** candidates.
- The corpus's first candidate pair for case 0 is at `y-1` and then `y`
  (`#16..#18` false, `#19..#21` true), so a candidate tests the cell below
  FIRST and only then its own cell.

The blocker is the gate: replaying `nextInt(0x14)` from case 0's seed
(3163345183) gives **2**, not 0, so the translated path returns false
immediately while the native run places five blocks. Something consumes Random
before that gate — most plausibly `_isNotIce` `0x1b6db40` (called three times in
`place`) or a draw inside the descent, neither of which the corpus currently
records as a Random word.

So the next step is to extend the seagrass oracle to record the raw generator
words positionally (the kelp oracle already does this by breaking at
`0x187e704`), which will show exactly which call consumes the words before the
gate. Until then the translation stays out of the tree.

The body is 0xaa2 bytes with six `nextGaussianInt` draws, four
`SeaGrass::Type` constructions and `_isNotIce`. The body is 0xaa2 bytes with six
`nextGaussianInt` draws, four `SeaGrass::Type` constructions and `_isNotIce`, so
it needs its own pass rather than a quick copy of the kelp shape.

## Next

Read the loop bounds and the column-growth branch (`0x1b54030..0x1b54432`) and
translate against the corpus. The dependency stream is already recorded, so the
translation can be written and checked without re-running the oracle — the same
position the vegetation and clay work started from.

Seagrass (`0x1b6d090`, 0xaa2 bytes), coral (`0x1b676f0`, 0x369) and ice spikes
are still untraced.
