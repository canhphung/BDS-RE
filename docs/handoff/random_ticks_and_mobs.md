# Handoff: random block ticks, mob spawning and mob AI (BDS 1.6.0.15 → BDS-RE)

Context for an agent continuing BDS-RE. Read this file completely before starting.

## 1. Goal

Translate from the original binary (not re-design) the systems that draw from
the **level Random** every tick, so that BDS-RE's level Random stream — and with
it everything decided by it — equals the original's for the same seed:

1. Random block ticks: `LevelChunk::tick` → `LevelChunk::tickBlocks` →
   `Block::tick` (grass spread etc.).
2. Mob spawning / despawning: `Spawner::tick`, `Spawner::tickDespawn`,
   `Spawner::postProcessSpawnMobs`, `_spawnMobCluster`, `popCapAllows`,
   `MobSpawnRules::getSpawnCount`, `WeighedRandom`.
3. Mob AI that draws the level Random: `Mob::newServerAiStep` →
   `GoalSelector::tick` → goals (`RandomStrollGoal`, `LookAtActorGoal`,
   `RandomLookAroundGoal`, …), `SpawnActorComponent::tick`; mobs also need
   movement (`Mob::aiStep`, `Mob::travel`, `Actor::move`) and replication to
   clients (AddActor, MoveActorDelta, SetActorData, …).
4. Smaller consumers of the same stream: `Dimension::backgroundTickSeasons`
   (5980 draws / 200 ticks), `Level::_saveSomeChunks` +
   `MainChunkSource::getRandomChunk`.

**Why:** `Level::tick` (0x1c86000) shuffles the player list with the level
Random every 20 ticks and then ticks the players in that order. BDS-RE already
reproduces the shuffle exactly (`Server::tick`), but the original also draws the
level Random for the systems above, so the player tick order (and later every
random decision: drops, effect chances, …) diverges. When this work is done,
the order of two players' packets within one tick must equal the original's, and
the `combat_reference` test can compare those ticks strictly (today they are
compared as sets — see `tests/unit/combat_tests.cpp`, comments "compared as a
set" / "level Random's shuffle").

Measured on the original (FLAT world, seed 123, two players, 200 ticks;
`tools/re/gdb_level_random.py`), level Random draws per caller:

| draws | caller chain |
|---|---|
| 17976 | `Random::nextInt` < `LevelChunk::tickBlocks` < `LevelChunk::tick` < `Player::tickWorld` |
| 8988 | `nextInt` < `Spawner::tick` < `LevelChunk::tick` |
| 8988 | `nextInt` < `Spawner::tickDespawn` < `LevelChunk::tick` |
| 5980 | `_genRandReal2` < `nextFloat` < `nextFloat(float,float)` < `Dimension::backgroundTickSeasons()::$_0(Player&)` |
| 4492 | palette `std::shuffle` (start-up only, already in BDS-RE) |
| 3559 + 3559 | `Level::_saveSomeChunks` and `MainChunkSource::getRandomChunk` |
| 1656 | `nextInt` < `GrassBlock::tick` < `Block::tick` < `tickBlocks` |
| 1120 | ChunkPos `std::shuffle` in `Level::tick` (already in BDS-RE) |
| 372 / 356 / 260 | `RandomStrollGoal::canUse` / `LookAtActorGoal::canUse` / `RandomLookAroundGoal::canUse` |
| 128, 114, 121, 121, 111, 82 | spawner helpers (`nextInt(int,int)`, `postProcessSpawnMobs`, `WeighedRandom::getRandomItem`, `getSpawnCount`, `popCapAllows`) |
| 6 | player list `std::shuffle` (already in BDS-RE) |
| ≤ 5 each | `RandomLookAroundGoal::start`, `SpawnActorComponent::tick`, `LookAtActorGoal::start`, `tickDespawn nextInt(int,int)` |

The flat world spawns animals at generation and around players, so mobs exist
from the start; their AI draws the level Random.

## 2. Project rules (from the user)

- **Translate from the binary; do not write your own logic.** Cite the
  original function address next to translated code. Where something is
  inferred, mark it `INFERRED` and say why.
- Verify against the original: run it (allowed, freely) and compare packets
  byte for byte with an oracle script, and/or trace it under gdb.
- Repository is **private**. Develop on branch `claude/clever-newton-bboqgm`
  (or the branch you are given); never open a PR unless asked. Never put model
  ids in commits. End commit messages with the attribution lines the session
  gives you.
- The original binaries, worlds and raw captures live in the gitignored
  `input/` (see `tools/reference/README.md` for how it is laid out and how the
  original is run: OpenSSL 1.0.2u, `ipv6_shim`). Only distilled references go to
  `tests/reference/`.
- Keep the three builds green (GCC Debug+ASan, GCC Release, Clang):
  `cmake -S . -B <dir> && cmake --build <dir> -j8 && (cd <dir> && ctest -j8)`.
  27 tests today.

## 3. Tooling (committed in `tools/re/`)

- `source tools/re/env.sh` — paths (`BDS_BIN`, `BDS_SYMS`, `BDS_RT`, `BDS_SSL`,
  `BDS_SHIM`, `BDS_WORK`); override before sourcing.
- `tools/re/make_syms.sh` — `nm -S -C` symbol table and a run directory
  `$BDS_RT` with an oracle `server.properties` (FLAT, seed 123, port 19220,
  normal, cheats on).
- `tools/re/dis.sh ADDR [SIZE]` — compact disassembly. **It hides rbp
  spills/reloads**; read raw `objdump -d --no-show-raw-insn -C
  --start-address=… --stop-address=…` when you need register flow or
  arguments.
- `tools/re/vt.py CLASS OFF…` — resolve vtable slots (e.g. `vt.py ServerPlayer
  748`). Rodata: file offset = vaddr − 0x400000; data segment: vaddr 0x31ceb18 ↔
  file 0x2bceb18.
- `tools/re/run_original.sh [STDIN]`, `tools/re/run_gdb.sh SCRIPT OUT [STDIN]`
  — start the original (fresh world) plain or under gdb with a Python
  breakpoint script. For console commands (`give`, `effect`, `summon`, …) give
  a FIFO as STDIN and keep a writer open: `mkfifo f; (sleep 99999 > f &)`.
- `tools/re/join_players.py PORT A,B SECONDS` — spawn clients and keep them
  connected (players are ticked only once spawned).
- gdb scripts: `gdb_level_random.py` (draws per caller), `gdb_packet_sends.py`
  (tick phases + packet sends), `gdb_watch_motion.py` (hardware watchpoint with
  call chains). Pattern: `gdb.Breakpoint` subclasses with `stop()` returning
  False, reading memory with `gdb.selected_inferior().read_memory`.
- Protocol client and oracles: `tools/reference/` (`bedrock_client.py`,
  `movement_oracle.spawn`, `combat_oracle.py` helpers for items/transactions/
  console). Existing references in `tests/reference/*.json`.

Useful offsets (Linux 1.6.0.15): Level +0x568 level Random (`Level::getRandom`
0x1c8c500), +0x50 player list, +0x8/+0x20 ChunkPos lists; Actor +0xec0 runtime
id, +0xe7c position (StateVectorComponent), +0xa0 motion, +0x2b0 the actor's own
Random, +0x124 onGround, +0x138 fall distance, +0x11d8 death time; Mob +0x1248
effects, +0x20c hurt time.

## 4. Addresses to start from

| address | size | function |
|---|---|---|
| 0x1c86000 | 0x813 | `Level::tick()` (shuffles every 20 ticks, then `forEachPlayer` $_2/$_3) |
| 0x1c86820 | 0x22d | `Level::tickEntities()` (global/autonomous entities) |
| 0x25f8b90 | 0x360 | `ServerPlayer::tickWorld(Tick const&)` |
| 0x22f4ba0 | 0x8b5 | `Player::tickWorld(Tick const&)` (chunk ticking around the player, then the player) |
| 0x1de3ff0 | 0xb7d | `LevelChunk::tick(BlockSource&, Tick const&)` |
| 0x1de5040 | 0xc12 | `LevelChunk::tickBlocks(BlockSource&)` |
| 0x1e68dd0 | — | `Block::tick` → 0x1e536e0 `BlockLegacy::tick` |
| 0x1e716a0 | 0x438 | `GrassBlock::tick` |
| 0x1c6c020 | 0x438 | `Spawner::tick(BlockSource&, LevelChunk&)` |
| 0x1c6c460 | 0x16e | `Spawner::tickDespawn` |
| 0x1c6c5d0 | 0x201 | `Spawner::postProcessSpawnMobs` |
| 0x1c6ae00 | 0x58e | `Spawner::_spawnMobCluster` |
| 0x1c6acd0 | 0x129 | `Spawner::popCapAllows` |
| 0x1dca6c0 | 0x363 | `MobSpawnRules::getSpawnCount` |
| 0x1d64680 | 0xb4 | `Dimension::backgroundTickSeasons()` |
| 0x1c86c30 | 0x320 | `Level::_saveSomeChunks()` |
| 0x1e22a80 | 0x123 | `MainChunkSource::getRandomChunk(Random&)` |
| 0x22bad00 | 0x1e1 | `Mob::newServerAiStep()` |
| 0x23b3950 | 0x321 | `GoalSelector::tick()` |
| 0x23c46f0 / 0x233b300 / 0x23aa880 | | `RandomStrollGoal::canUse` / `LookAtActorGoal::canUse` / `RandomLookAroundGoal::canUse` |
| 0x2439460 | 0x165 | `SpawnActorComponent::tick()` |
| 0x22b9bc0 | 0xd7a | `Mob::aiStep()` |
| 0x22b5ad0 | 0x197b | `Mob::travel(float,float,float)` |
| 0x21a6b50 | 0x1f6f | `Actor::move` (already translated for items: `src/game/item_actor.cpp` `move`) |
| 0x19c27f0 / 0x19323e0 / 0x1932550 | | `Random::nextInt(int)` / `nextFloat()` / `_genRandReal2()` |

Mob definitions (components, goals, spawn rules) are data in
`input/linux/bds/behavior_packs/vanilla/entities/*.json` and spawn rules; the
C++ reads them through the actor definition system — translate the reading
code too, or bundle the data as BDS-RE already does for items/effects
(`src/game/items.cpp` and `src/game/effects.cpp` were generated from gdb dumps
of the original's live registries; see `docs/BUNDLED_DATA.md`).

## 5. BDS-RE state you build on

- `src/game/server.cpp` `Server::tick()`: items tick, then the `Level::tick`
  shuffle every 20 ticks (`level_tick_ % 20 == 0`: two ChunkPos lists of the
  client ticking offsets, then `player_order_`), then players in
  `player_order_`, then `update_nearby`. `level_random_` is seeded with the level
  seed (`level_seed()`, the StartGame seed) and first drawn by
  `protocol::shuffle_block_palette` (palette = original's for the same seed).
- **Order mismatch to fix as you go:** in the original, items and mobs are
  ticked inside `Player::tickWorld` via `LevelChunk::tick` (per player, chunks
  around it), not before all players. BDS-RE ticks items first. Moving that is
  part of this work (check against `tests/reference/item_actor_*`,
  `break_bds_*` which must stay green).
- `src/world/random.cpp` — exact `Random` (MT19937, lazy init) with
  `next_uint/next_int/next_float/next_gaussian`; `tests/reference/random_vectors.json`.
- `src/world/world.hpp` — world store (flat / rolling / explicit columns, 4
  internal block types: air, stone, grass, dirt, bedrock). No block ticking, no
  mobs, no chunk save scheduling.
- `src/game/item_actor.cpp` — translated `Actor::move`, `pushOutOfBlocks`,
  item physics; replication (`replicate`, MoveActorDelta / SetActorMotion).
- `src/game/combat.cpp`, `effects.cpp`, `attributes.cpp` — mobs can reuse the
  attribute/effect/hurt pipeline (written for players; `Session`-based — you
  will need an actor abstraction).
- Visibility: `Server::update_nearby` (client ticking offsets, `known` sets,
  AddPlayer/AddItemActor/RemoveActor).
- Docs to update: `docs/GAMEPLAY.md` ("Order" bullet), `docs/PROGRESS.md`,
  `docs/BUNDLED_DATA.md`, `tools/reference/README.md`.

## 6. Suggested approach

1. Trace the original's level Random draw-by-draw: extend
   `gdb_level_random.py` to log `(level tick, caller, returned value)` for the
   first N ticks after start with one player at a fixed spot (FLAT, seed 123).
   That log is the acceptance test: BDS-RE must produce the identical sequence
   (add a debug hook on `level_random_`).
2. Get the start-up draws right first (palette shuffle, then anything between
   it and the first tick — chunk generation / `postProcessSpawnMobs` of the
   flat world's animals).
3. Then per tick, in the original's order: `Level::tick` shuffles → each player:
   `Player::tickWorld` (chunk ticks: blocks, spawner, despawn, entities) →
   seasons → `_saveSomeChunks`. Translate each consumer until the sequences
   match for hundreds of ticks.
4. Add mobs as entities visible to clients (packets compared with an oracle
   capture; animals spawned by the flat world at known positions).
5. Finally make `combat_reference` compare the two-player ticks strictly and
   remove the "compared as a set" sorting.

## 7. Pitfalls already learned

- `std::shuffle` here is libstdc++'s classic Fisher–Yates with
  `uniform_int_distribution<unsigned long>` over `Random::operator()`
  (`_genRandInt32`, range 2^32−1): `scaling = 0xffffffff / (i+1)`, reject
  `x >= (i+1)*scaling`, `j = x / scaling`.
- `mce::Math::sin/cos` use a 65536-entry table (`src/game/mce_math.hpp`);
  `invSqrt` is the 0x5f3759df fast inverse square root.
- Float constants: read them from rodata (`vaddr − 0x400000`) instead of
  guessing; several "0.3"/"0.1" are bit patterns like `0x3dcccccc`.
- Console commands run between ticks (verified); packets sent from
  `Mob::normalTick`'s end differ from `_sendDirtyActorData`'s (§ docs/GAMEPLAY.md).
- In unit tests, chunks are not generated unless `World::ensure_chunk` is
  called, and entities are only announced to clients that requested chunks.
- gdb Python: `read_memory(addr, 0)` throws — guard empty strings; breakpoints
  on hot functions (`_genRandInt32`) are slow; filter on `$rdi == level+0x568`.
