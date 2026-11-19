# Native corpora

Every `*_bds_1.6.0.15.json` here is a corpus captured from the unchanged Linux
BDS 1.6.0.15 binary (SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`). Each file
records that hash, the function (and address) it covers, the generator script
and an `oracle_sha256` of the raw capture, so a fixture cannot be silently
regenerated against a different binary.

Oracles never patch feature code, RNG draws or expected results. A setup helper
may call the original's own setter to build input geometry; nothing else.

A fixture is only useful together with a test that replays it. `CMakeLists.txt`
registers one test per fixture; the exceptions are listed below.

## Fixtures with no registered test

- `kelp_bds_1.6.0.15.json` — `KelpFeature::place` `0x1b53de0`, 14 cases, 583
  placements, 683 Random words. The oracle works and the corpus is complete, but
  the translation does not replay it: the read counts inside the candidate body
  still shift the stream at query 265. Kept because the draw model is confirmed
  against it (all ten candidate cells reproduce from the seed). See
  `reverse/reports/MARINE.md`.
- `seagrass_bds_1.6.0.15.json` — `SeagrassFeature::place` `0x1b6d090`, 14
  cases, 111 placements, 575 Random words. The oracle works and the corpus is
  complete; the translation has not started. See `reverse/reports/MARINE.md`.
- `jungle_tree_real_ids_bds_1.6.0.15.json` — `JungleTreeFeature::place`
  captured with the class's REAL constructor type ids (leafType 3, trunkType 1)
  instead of 0. 16 cases, 935 writes, blocks `log:3`, `leaves:3`,
  `cocoa:1/2/3/5`, `vine:1/2/4/8`. This is the corpus the Jungle translation
  must satisfy; it diverges from the base feature at query 124 because Jungle
  hangs vines down a column, so it is not registered until that body is
  translated. The older `jungle_tree_bds_1.6.0.15.json` (523 writes, ids 0)
  verifies the BASE TreeFeature path, not JungleTreeFeature.
- `swamp_tree_bds_1.6.0.15.json` — `SwampTreeFeature::place` `0x1b6bcb0`,
  16 cases, 1687 writes, 4496 Random words. Oracle complete; translation not
  started. See `reverse/reports/TREES.md`.
- `roof_tree_bds_1.6.0.15.json` — `RoofTreeFeature::place` `0x1b5b7e0`,
  16 cases, 1424 writes, 305 Random words. Oracle complete; translation not
  started.
- `tree_leaf_gates_bds_1.6.0.15.json` — 128 recorded `nextInt(2)` gates with
  their candidate cell, draw and call decision, used to derive the leaf-volume
  rule. The rule is now verified end to end by `tree_place_leaves` (448 leaf
  cells), so this corpus is supporting evidence rather than a test input.

## Fixtures used through a protocol/integration test

`biomes`, `break`, `chunks`, `hunger_health`, `movement`, `player_actions` and
the rest are registered; check `CMakeLists.txt` for the exact test names.
