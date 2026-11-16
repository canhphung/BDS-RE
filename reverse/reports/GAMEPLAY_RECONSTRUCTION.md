# Gameplay reconstruction from the original binary

Directive (user, 2026-11-16): gameplay — terrain generation, movement synchronisation, block placement/breaking and inventory — must follow the logic recovered from the BDS 1.6.0.15 binary, not independently invented algorithms. The experimental M5 gameplay (docs/GAMEPLAY.md) is invented in several places and is being replaced piece by piece.

## Method

1. **Trace** the original function in the Linux ELF (disassembly with symbols; addresses recorded in the code next to each translated function).
2. **Translate** it to C++ preserving data flow, integer widths and floating-point operation order (the original is SSE single/double precision; results must match bit for bit).
3. **Verify with the original as an oracle**:
   - *Function oracle*: run `bedrock_server` under gdb, stop at `main`, call the original functions by address on a scratch object and record outputs (`tools/reference/oracle_*.py`). The C++ translation must reproduce them exactly.
   - *Chunk oracle*: generate a world with the same seed in the original and in BDS-RE and compare decoded chunks (block states, biomes) from reference captures.
   - *Packet oracle*: replay client actions against the original with `tools/reference/bedrock_client.py` and compare server responses.

## Inventory of invented logic to replace

| Area | Current (invented) | Original entry points |
|---|---|---|
| Terrain DEFAULT | FNV-1a seed hash, SplitMix64 "rolling" heights (src/world/terrain.cpp) | `OverworldGenerator` (loadChunk 0x1b38610, prepareHeights 0x1b36ac0, getHeights 0x1b373d0, buildSurfaces 0x1b37f80, postProcess 0x1b38950), biome `Layer` stack, caves, features |
| Terrain FLAT | bedrock/dirt×2/grass | `FlatWorldGenerator` (0x1b6ed70; default layers JSON in .rodata: bedrock 1, dirt 2, grass 1, biome 1, encoding_version 5) — appears consistent, chunk comparison pending |
| Seed | numeric or FNV-1a string hash | level-seed parsing in the original not yet traced |
| Movement | own speed budgets and collision checks | `ServerNetworkHandler::handle(MovePlayerPacket)`, ServerPlayer movement validation |
| Block break/place | own timing/reach rules, drops straight into inventory | `PlayerActionPacket` / `InventoryTransactionPacket` handlers, `GameMode::destroyBlock`, `useItemOn`, block loot |
| Inventory | own slot model and transaction checks | `InventoryTransaction` / `PlayerInventoryProxy` (ItemUse mismatch branch already traced, docs/ITEM_USE.md) |

## Progress

| Component | Status | Evidence |
|---|---|---|
| `Random` (MT19937 with lazy init, nextInt/Float/Double/Boolean/Gaussian) | TRANSLATED, DIFFERENTIAL_VERIFIED | src/world/random.cpp; 7,915 outputs equal to the original functions (tests/reference/random_vectors.json, test `random_reference`) |
| `ImprovedNoise`, `PerlinNoise`, `PerlinSimplexNoise`, `SimplexNoise` | next | float-based; ImprovedNoise::_readArea 0x1b78400, PerlinNoise::getRegion 0x1b738f0 |
| Biome layers | pending | 185 `Layer` functions, `BiomeSource` |
| OverworldGenerator heights/surfaces | pending | depends on noise and biomes; intermediate BlockVolume can be dumped from the original under gdb |
| Caves, ores, trees, structures | pending | `LargeCaveFeature`, `OreFeature`, `TreeFeature` families |
| Movement, block actions, inventory | pending | handlers listed above |

## Recorded findings

- `Random::Random()` (0x187d430) seeds from `std::random_device`; generators always pass explicit seeds.
- `Random::nextBoolean` tests bit 27 of the tempered word (`& 0x8000000`), not the low bit.
- `Random::nextInt(int)` is the unsigned remainder of the tempered word, `nextInt()` the word shifted right by one; `nextFloat` is `(float)(u32 * 2^-32)`.
- `Random::nextGaussian` is the polar method computed in single precision with `std::log(float)`/`std::sqrt(float)`; bit equality on other C libraries (MSVC) depends on their `logf`/`sqrtf` and is checked by Windows CI.
