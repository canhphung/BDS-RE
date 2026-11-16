# Gameplay reconstruction from the original binary

Directive (user, 2026-11-16): gameplay — terrain generation, movement synchronisation, block placement/breaking and inventory — must follow the logic recovered from the BDS 1.6.0.15 binary, not independently invented algorithms. The experimental M5 gameplay (docs/GAMEPLAY.md) is invented in several places and is being replaced piece by piece.

## Method

1. **Trace** the original function in the Linux ELF (disassembly with symbols; addresses recorded in the code next to each translated function).
2. **Translate** it to C++ preserving data flow, integer widths and floating-point operation order (the original is SSE single/double precision; results must match bit for bit). Direct translation of the original logic is the intended approach; the earlier "independently authored code" rule no longer applies.
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
| Movement | replaced (see progress) | `ServerNetworkHandler::handle(MovePlayerPacket)` 0x182bcf0 |
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
| Movement (MovePlayer) | TRANSLATED, DIFFERENTIAL_VERIFIED | handler 0x182bcf0 in src/game/server.cpp; packet layout 0x18b4190/0x18b40b0; the oracle observes identical relay/no-correction behaviour on the original and BDS-RE (tests/reference/movement_bds_1.6.0.15.json, test `movement_reference`) |
| Mining time and reach | next | PlayerAction handler 0x182ba?/GameMode::startDestroyBlock 0x1ab44a0, continueDestroyBlock 0x1ab50b0, destroyBlock 0x1ab4a80, Player::getDestroySpeed 0x22ed000 |
| Item drops and pickup, inventory | pending | handlers listed above |

## Recorded findings

- `Random::Random()` (0x187d430) seeds from `std::random_device`; generators always pass explicit seeds.
- `Random::nextBoolean` tests bit 27 of the tempered word (`& 0x8000000`), not the low bit.
- `Random::nextInt(int)` is the unsigned remainder of the tempered word, `nextInt()` the word shifted right by one; `nextFloat` is `(float)(u32 * 2^-32)`.
- `Random::nextGaussian` is the polar method computed in single precision with `std::log(float)`/`std::sqrt(float)`; bit equality on other C libraries (MSVC) depends on their `logf`/`sqrtf` and is checked by Windows CI.

### MovePlayer (2026-11-16)

ServerNetworkHandler::handle(MovePlayerPacket) 0x182bcf0: `_getPlayer` (0x182c1c0) returns null unless the runtime id matches the sender's player, and the packet is then ignored. The packet is also ignored while +0x140c (non-zero during dimension changes, compared with 0 by isPlayerInitialized) or the +0x1410 flag is set. With a ride, the rider is positioned by the vehicle. Otherwise Y is clamped with mce::Math::clamp to [-128, 512] (constants 0x281cd04/0x281cd08), the position delta is stored, and: mode 1 with a dead player sets position/rotation; mode 2 sets position/rotation and onGround; other modes set onGround, call checkMovementStats (telemetry only: distance towards Player::DISTANCE_TO_TRANSFORM_EVENT) and Mob::lerpTo(pos, rot, 1 or 3). Head yaw is stored, Actor::checkBlockCollisions runs, the received packet is relayed with Dimension::sendPacketForEntity(player, packet, player) and riders are teleported. Vtable slots resolved via `vtable for ServerPlayer` (0x3283760): +0x60 setPos, +0x70 getPosOld, +0x98 setRot, +0xc0 checkBlockCollisions, +0xe8 Mob::lerpTo, +0x118 positionRider, +0x2e0 Mob::isAlive, +0xac0 checkMovementStats.
