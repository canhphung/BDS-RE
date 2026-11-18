# ClayFeature — Phase 3 component

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

`ClayFeature::ClayFeature(int)` `0x1b557f0`, `place` `0x1b55840` (0x347 bytes).
Translated in `src/world/clay.cpp`, verified by `clay_reference` against
`tests/reference/clay_bds_1.6.0.15.json`.

## Native behaviour

1. **Anchor gate** `0x1b5586e..0x1b558b7`: `BlockSource::getBlock`, then the
   block's `getMaterial` and `Material::isType(5)`. The feature proceeds when
   that test is true **or** when the anchor already is clay; every other case
   returns false immediately. Stone and sand never reach the scan.
2. **Radius** `0x1b558cf..0x1b558e6`: `nextInt(size - 2) + 2`, where `size` is
   the constructor argument read back from the feature object at `+0x18`.
3. **Vertical half-extent** is the constant `1` stored at `-0x40`, used as
   `origin.y - 1 .. origin.y + 1`.
4. **Scan** is a circle, not a square: for each cell it computes
   `dx = x - origin.x`, `dz = z - origin.z` and skips the cell when
   `dx*dx + dz*dz > radius*radius`.
5. **Placement** `0x1b55ad8..0x1b55b3c`: only a **dirt** cell becomes clay.

## What the corpus fixed

The oracle `tools/re/gdb_clay_cases.py` constructs the real feature and calls
`place` over a dirt/stone/water arena, recording every `getBlock`, the anchor's
`isType` answer, every Random word and every `_placeBlock`.

Four defects were caught only by replaying it:

- **The anchor's `isType(5)` answer must be observed at `0x1b55892`**, the
  `test $1,%al` after the call. Reading it at the `getBlock` return, or at
  `Material::isType`'s own `rdi`, gives garbage: `isType` takes a `Material*`,
  not a `BlockPos`. The first attempt recorded positions like
  `(5, 16842752, 1)`.
- **`getBlock` is called once for the anchor and once per candidate**, and the
  two mean different things. The corpus keeps them apart as `anchor_block` and
  `candidate_block` rows by caller return address, and only the anchor row
  carries the `isType` answer.
- **The loop nesting is X → Z → Y**, not X → Y → Z. The corpus shows
  `(-9,5,-9)`, `(-9,6,-9)`, `(-9,7,-9)`, `(-9,5,-8)`, ... so Y is the innermost
  loop. Two earlier attempts used Y in the middle and desynchronised the query
  stream on the third candidate.
- The scan is a **circle** (`dx*dx + dz*dz <= radius*radius`), which the
  candidate positions confirm: for `radius = 2` and `dx = -2` only `dz = 0`
  survives.

## Corpus

14 cases over 3 distinct arenas: **538 real placements**, 10 Random words and
2,504 dependency answers (14 anchor rows plus 2,490 candidate rows). The file is
214 KB after deduplicating the scenes and storing only changed cells.

## Scope

This is the feature component only. It does not run neighbour notifications,
pending ticks or lighting, and it is not yet called by
`OverworldGenerator::postProcess`; that orchestration stays blocked on the
feature set described in [DECORATE.md](DECORATE.md).
