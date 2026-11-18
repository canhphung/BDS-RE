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

## Next

Read the loop bounds and the column-growth branch (`0x1b54030..0x1b54432`) and
translate against the corpus. The dependency stream is already recorded, so the
translation can be written and checked without re-running the oracle — the same
position the vegetation and clay work started from.

Seagrass (`0x1b6d090`, 0xaa2 bytes), coral (`0x1b676f0`, 0x369) and ice spikes
are still untraced.
