# Vegetation features — native corpus (Phase 3, first slice)

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

Status: **native corpus captured; translation not yet registered.** This slice
establishes the oracle and the exact per-feature contracts, which is the step
that failed when attempted by reading alone: a first attempt guessed the vine
facing tables and was wrong (see "Why oracle first" below).

## Oracles

`tools/re/gdb_vegetation_cases.py` runs inside a paused original server
(`capture_level_random.py --gdb-post-script`). For every case it:

1. resets a 20×20 ground arena, Y 2..21, through the **original**
   `BlockSource::setBlock` with flag 0 (a small compiled helper loops over it to
   avoid one GDB round trip per cell — it only restores input geometry);
2. constructs the feature with its **real constructor**;
3. seeds a real original `Random` and calls the **real `place()` entry**;
4. records every raw Random word (`0x187e704`), every `Feature::_placeBlock`
   attempt (`0x1c59b20`, including position, block name+data and flags), the
   final block state, and the next four `nextInt` outputs to prove continuation;
5. records every dependency predicate call (position + the original's own
   boolean answer).

No feature code, Random draw, branch, predicate result or expected output is
patched.

| Feature | Constructor | `place()` | Native words/case |
|---|---|---|---:|
| `TallGrassFeature` | `0x1b66370` | `0x1b66570` (`place(...,int,int)` `0x1b663c0`) | 540 |
| `FlowerFeature` | `0x1b5ef20` | `0x1b5ef70` (`_placeMushroom` `0x1b5efc0`) | 384 |
| `PumpkinFeature` | `0x1dac580` | `0x1b52430` | 384–398 |
| `CactusFeature` | `0x1dabd40` | `0x1b5d3b0` | 70–80 |
| `WaterlilyFeature` | `0x1dac160` | `0x1b53000` | 60 |
| `ReedsFeature` | `0x1dab920` | `0x1b5d070` | 80 |
| `VinesFeature` | `0x1db25e0` | `0x1b531a0` | 0 |

`Random::nextGaussianInt` `0x1b52610` is `nextInt(n) - nextInt(n)` — two draws,
not a Gaussian.

The per-case word counts independently confirm the loop shapes read from the
disassembly: flower and pumpkin run 64 candidates at 3 draws each (192, plus
the 64-entry scene-dependent extras); waterlily runs 10 at 6; tall grass runs 90
at 6; reed runs 20 at 4 (two gaussians plus two `nextInt`); vine draws nothing
because its loop only calls `mayPlace`.

## Corpus

`tools/re/export_vegetation_reference.py` validates provenance and writes
`tests/reference/vegetation_bds_1.6.0.15.json` (≈500 KB): 70 cases, **184 real
placements**, **15,304 native Random words**, **5,085 recorded predicate
answers** (`is_empty` 4,170, `can_survive` 851, `may_place` 64).

Initial scenes are deduplicated (9 distinct arenas) and stored as
base64+zlib `<hhhH>` cells; each case keeps only the cells that changed, plus
its writes, words, tail and dependency stream. The exporter self-checks that the
compact encoding reconstructs the observed final state exactly.

Recorded dependency kinds and their native argument register:

| Kind | Original function | BlockPos argument |
|---|---|---|
| `is_empty` | `BlockSource::isEmptyBlock` `0x1c60cb0` | `rsi` |
| `is_solid_blocking` | `BlockSource::isSolidBlockingBlock` `0x1c5aea0` | `rsi` |
| `can_survive` | `Block::canSurvive` `0x1e69f10` | `rdx` |
| `may_place` | `Block::mayPlace(BlockSource&, BlockPos)` `0x1e6a5a0` | `rdx` |
| `may_place_on` | `Block::mayPlaceOn` `0x1e6a540` | `rdx` |

Each answer is read from `%al` at the call's return address, and every recorded
position was validated to lie inside the scene box.

## Why oracle first

A first attempt at `src/world/vegetation.cpp` transcribed the vine direction
tables from memory as `opposite = {-1,0,3,2,1,4}` and
`facing = {0,1,2,3,4,5}`. Reading the real `.rodata` gives

```
Facing::OPPOSITE_FACING   (0x2830c98) = [1, 0, 3, 2, 5, 4, 6, 0]
Direction::FACING_DIRECTION (0x2849d34) = [255, 255, 2, 0, 1, 3, 2, 3]
```

which is a different mapping. The guessed file was deleted rather than kept,
and this corpus is the replacement: the translation will be written against it
and must reproduce all 70 cases exactly.

The same trap appeared while writing the oracle itself: block globals were first
hardcoded, and `VanillaBlocks::mAir` does not exist (air is
`BedrockBlocks::mAir`), while `0x34a7768` is `VanillaBlockTypes::mGrass`
(a `BlockLegacy`), not `VanillaBlocks::mGrass` (a `Block`). The oracle now
resolves every global through `gdb.parse_and_eval("&'symbol'")`.

## Confirmed native behaviour (to implement next)

Read from the disassembly and corroborated by the word counts above:

- **TallGrass** `0x1b663c0`: first descends the column while the block is air or
  has `BlockProperty 0x20` and Y > 0 (the `BlockPos` is mutated in place), then
  runs `count` candidates; each draws `nextGaussianInt(spread)` for Z,
  `nextGaussianInt(4)` for Y, `nextGaussianInt(spread)` for X, and places when
  the target is air and `canSurvive` holds. The `0x1b66570` overload passes
  `r8 = 0x5a`, `r9 = 0x8`.
- **Flower** `0x1b5f230` / `_placeMushroom` `0x1b5efc0`: 64 candidates with 8/4/8
  spreads. `_placeMushroom` additionally requires the cell below to be non-air,
  and either places on podzol/mycelium or requires the heightmap/`mayPlaceOn`
  path.
- **Pumpkin** `0x1b52430`: 64 candidates with Z spread 8, Y spread 8, X spread
  4 (note X uses 4, unlike the others); needs `isEmptyBlock`, grass below,
  pumpkin `mayPlace`, then `nextInt(4)` for the facing state.
- **Cactus** `0x1b5d3b0`: 10 candidates with 8/4/8 spreads; `nextInt(3)+1` then
  `nextInt(width)+1`, each block checked with `canSurvive` on `above(k)`.
- **Waterlily** `0x1b53000`: 10 candidates with 8/4/8; needs `isEmptyBlock`
  then lily `mayPlace`.
- **Reeds** `0x1b5d070`: 20 candidates with Z and Y gaussians plus two
  `nextInt` draws, X pinned to 0; material checks on the four horizontal
  neighbours.
- **Vines** `0x1b531a0`: no Random; walks `face = 2..5`, requires
  `isEmptyBlock` and vine `mayPlace(...,face)`, then sets the direction bit from
  `OPPOSITE_FACING`/`FACING_DIRECTION`.

## Translation complete

`src/world/vegetation.cpp` now reproduces **all 70 cases**: 184 placements,
15,304 Random words and 16,387 dependency answers. The corpus caught seven
distinct defects that reading the disassembly alone had missed, and each is
recorded at its site in the code:

1. **Tall grass does not call `BlockSource::isEmptyBlock`.** It calls
   `getBlock` and compares the legacy identity with `BedrockBlockTypes::mAir`
   inline (`0x1b664d5..0x1b664f8`), so its only observable predicate is the 90
   `can_survive` calls.
2. **Tall grass's descent is unobservable.** The loop mutates a copy of the
   `BlockPos` (`-0x38`), but only tests the descended Y against zero
   (`0x1b66450`) and places using the ORIGINAL argument (`-0x18` at
   `0x1b664be`). It draws nothing, so it was left out with that reasoning
   written down rather than silently reproduced.
3. **Pumpkin draws X and Z with spread 8 and Y with spread 4.** The native order
   at `0x1b524a6..0x1b524ee` is gaussian(8) -> `-0x48`, gaussian(8) -> `-0x4c`,
   gaussian(4) -> `-0x50`, and `BlockPos::offset` takes x from `-0x48`, y from
   `-0x50`, z from `-0x4c`. The naive X/Y/Z reading is wrong and the first
   candidate lands elsewhere.
4. **Cactus uses the ordinary Z/Y/X order**, not pumpkin's, and a false
   `canSurvive` skips only that block's placement — the column counter still
   advances (`0x1b5d522..0x1b5d52b`), which is why the corpus shows consecutive
   `can_survive` rows.
5. **Cactus's `canSurvive` internally queries four neighbour materials.** Those
   rows appear after each `can_survive` in the corpus and are consumed by the
   test's `can_survive` replay, not by the feature.
6. **Reed has no air check below the candidate.** An earlier version invented
   one; `0x1b5d0f9` goes straight from `below()` to the four `getMaterial`
   queries. Reed also draws X first and pins Y to zero.
7. **Reed's water condition is inverted relative to the obvious reading.**
   `0x1b5d1f8`: `test $1,%al; jne 0x1b5d205; jmp 0x1b5d32b` — the column walk is
   taken when `Material::isType(5)` SUCCEEDS, so a dry attempt is skipped. That
   is exactly why the corpus records 20 attempts, 80 words and zero placements.

The reed column walk itself (`0x1b5d205..0x1b5d326`) is the one part not yet
translated: no attempt in this corpus reaches it, so it is marked `TODO` in the
code instead of being guessed. A corpus whose scene lets a reed attempt pass the
material check is the next step for that function.

## Next

Trees, marine features and snow/ice covering (Phase 3 remainder), plus the reed
column walk above. `OverworldGenerator::postProcess` stays unregistered until
its remaining features exist — see [POSTPROCESS.md](POSTPROCESS.md).