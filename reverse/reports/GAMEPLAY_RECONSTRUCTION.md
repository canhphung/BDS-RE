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
| Mining time and reach | TRANSLATED, DIFFERENTIAL_VERIFIED (packet oracle) | ItemUse handle 0x1ae6290 (reach 12.5, error 7), GameMode::destroyBlock 0x1ab4a80, _destroyBlockInternal 0x1ab4e50, PlayerAction handler 0x182d140 (StartBreak crack rate), GameMode::getDestroyRate 0x1ab5820, Player::getDestroyProgress 0x22ed290, Player::getDestroySpeed 0x22ed000; tests/reference/break_bds_1.6.0.15.json, asserted in tests/unit/gameplay_tests.cpp |
| Item drops and pickup | TRANSLATED, DIFFERENTIAL_VERIFIED (gdb trace replay) | popResource 0x1e545b0, ItemActor ctor 0x23fe150, normalTick 0x23fe8a0, Actor::move 0x21a6b50, AABB::clipCollide 0x21877e0, fetchCollisionShapes 0x1c5d990, pushOutOfBlocks 0x21bb8a0, _merge 0x23ff7f0, playerTouch 0x23ff130, Player::take 0x22f0640, sendMotionPacketIfNeeded 0x21bb110, MoveActorAbsoluteData 0x1919ca0..0x191a410; 201 ticks bit-exact (tests/reference/item_actor*_bds_1.6.0.15.json) |
| Inventory transactions | pending | handlers listed above |

## Recorded findings

- `Random::Random()` (0x187d430) seeds from `std::random_device`; generators always pass explicit seeds.
- `Random::nextBoolean` tests bit 27 of the tempered word (`& 0x8000000`), not the low bit.
- `Random::nextInt(int)` is the unsigned remainder of the tempered word, `nextInt()` the word shifted right by one; `nextFloat` is `(float)(u32 * 2^-32)`.
- `Random::nextGaussian` is the polar method computed in single precision with `std::log(float)`/`std::sqrt(float)`; bit equality on other C libraries (MSVC) depends on their `logf`/`sqrtf` and is checked by Windows CI.

### MovePlayer (2026-11-16)

ServerNetworkHandler::handle(MovePlayerPacket) 0x182bcf0: `_getPlayer` (0x182c1c0) returns null unless the runtime id matches the sender's player, and the packet is then ignored. The packet is also ignored while +0x140c (non-zero during dimension changes, compared with 0 by isPlayerInitialized) or the +0x1410 flag is set. With a ride, the rider is positioned by the vehicle. Otherwise Y is clamped with mce::Math::clamp to [-128, 512] (constants 0x281cd04/0x281cd08), the position delta is stored, and: mode 1 with a dead player sets position/rotation; mode 2 sets position/rotation and onGround; other modes set onGround, call checkMovementStats (telemetry only: distance towards Player::DISTANCE_TO_TRANSFORM_EVENT) and Mob::lerpTo(pos, rot, 1 or 3). Head yaw is stored, Actor::checkBlockCollisions runs, the received packet is relayed with Dimension::sendPacketForEntity(player, packet, player) and riders are teleported. Vtable slots resolved via `vtable for ServerPlayer` (0x3283760): +0x60 setPos, +0x70 getPosOld, +0x98 setRot, +0xc0 checkBlockCollisions, +0xe8 Mob::lerpTo, +0x118 positionRider, +0x2e0 Mob::isAlive, +0xac0 checkMovementStats.

### Block breaking (2026-11-16)

Packet oracle: tools/reference/capture_break_oracle.py, survival player at (0.5, 66.62, 0.5) on ground with an empty hand, FLAT world, no StartBreak before the break request. Results (tests/reference/break_bds_1.6.0.15.json):

- grass (distance 3.06), dirt, bedrock (4.40) and stone (11.80) all break: UpdateBlock to air, LevelEvent 2001 at the block centre with the block runtime id, PlayerAction StopBreak (runtime id, zero position) to the breaker. Grass and dirt also spawn an ItemActor (AddItemActor); stone by hand and bedrock drop nothing.
- stone at 12.78 > 12.5: error 7 → onTransactionError: four UpdateBlock (resendBlocksAroundArea 0x1ae6120), InventoryContent for windows 0, 120, 124, 119 and PlayerHotbar.
- StartBreak crack data: grass 3640, dirt 4369, stone 436, bedrock 0 — exactly floor(65535 × getDestroyProgress) with hardness 0.6/0.5/1.5/−1 and the 30 (Material::isAlwaysDestroyable) / 100 divisors (constants 0x281c5c4, 0x28323a4). StartBreak has no reach check (the 12.78 block also produced 3600).
- The PlayerAction handler's action chain: 0 StartBreak (extinguishFire, Block::attack, LevelEvent 3600 unless creative), 1 AbortBreak and 2 StopBreak (LevelEvent 3601), 6, 8 jump, 9/10 sprint, 11/12 sneak, 13 dimension change (ChangeDimensionRequest), 14, 15/16 gliding, 17/18 crack continue, 20 enchantment seed, 21–24 vtable calls. No action destroys a block; the earlier BDS-RE "action 13 creative break" was invented and is removed.
- SurvivalMode::destroyBlock 0x1abcd90 only refuses when flags +0xa4 and +0xa6 are both set (zeroed by the constructor), so bedrock is not protected on this path.

### Dropped items (2026-11-16)

Method: the original was run under gdb with tools/reference/gdb_item_actor.py logging the ItemActor at every normalTick entry (position, motion, box, flags) plus merge/remove/playerTouch/Player::take and packet writes. Two scenarios: a single drop picked up by a player, and two adjacent drops that merge before the hole is refilled over them. tests/unit/item_actor_tests.cpp replays both from the recorded initial states and requires identical float bits every tick, identical AddItemActor/SetActorData bytes, the same 15 MoveActorDelta packets and the pickup InventoryTransaction bytes.

Findings that differ from what a re-implementation would naturally do:
- Ground friction is the constant 0x3f16872c (0.6f * 0.98f), one ulp above the literal 0.588f; the replication threshold 0x358637be is one ulp above 1e-6f.
- fetchCollisionShapes iterates x, z, then y over floor(min - 1)..floor(max + 1) and keeps only strictly intersecting boxes; clipping is sequential per box, so both the order and the exclusion of touching boxes change results (a buried item is pushed up instead of being held).
- AABB::clipCollide on an overlapping box pushes along the smallest penetration axis (first minimum on ties) unless the actor is stuck (penetration above FLT_EPSILON on two consecutive moves) and has an exit.
- MoveActorAbsoluteData::calculateDelta copies the header (onGround, teleported) from the previously sent data, so the landing tick is flagged one packet late.
- AddItemActor is sent after the item's first tick, preceded by SetActorData with the same metadata; Player::take's InventoryTransaction lists the world-interaction action before the container action.
- Merging: the larger stack absorbs the smaller (the other one on a tie), takes the larger throw time and the smaller age, and broadcasts ActorEvent 69 with the new count.
