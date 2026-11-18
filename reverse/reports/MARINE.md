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

## Next

Read the loop bounds and the column-growth branch (`0x1b54030..0x1b54432`) and
translate against the corpus. The dependency stream is already recorded, so the
translation can be written and checked without re-running the oracle — the same
position the vegetation and clay work started from.

Seagrass (`0x1b6d090`, 0xaa2 bytes), coral (`0x1b676f0`, 0x369) and ice spikes
are still untraced.
