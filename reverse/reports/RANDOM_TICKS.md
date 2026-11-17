# Level Random and random block sampling — 2026-11-17

This report tracks implementation checkpoints for
`docs/handoff/random_ticks_and_mobs.md`. It does **not** complete that handoff.
Random block selection and GrassBlock are now wired into the per-player chunk
scheduler for clear weather and the current host palette. The lighting integration
has 10,145 original pair samples and 2,560 native tickBlocks cases; its synchronous
cache adapter is inferred. See [function evidence and boundaries](BLOCK_LIGHTING.md).
Seasons, chunk-save scheduling and the Spawner/despawn decision draws are now
integrated (see "Level Random consumers, second checkpoint"); the mob spawn
cluster, chunk post-processing spawns and mob AI still need integration.
The sections below preserve evidence from the earlier function checkpoints.
The existing combat comparison remains unchanged; packet-order sorting has not
been removed.

Original Linux binary SHA256:
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

## Original execution and trace

`tools/re/capture_level_random.py` launches its own GDB child and a fresh isolated
world. It never kills other servers or removes existing worlds. The probe filters
the exact `Level + 0x568` Random, records each raw `_genRandInt32` return value,
the calling PCs, tick boundaries, player initialization and optional block samples.
GDB's Python `stop()` runs before its breakpoint condition is tested, so the
Python callback also explicitly checks the Random object's address.

The original ignores `level-type=FLAT`: LevelSettings arrived at the constructor
with generator 1. The oracle's explicit `--generator flat` sets that field to 2
before the Level constructor. No game algorithm, RNG state or generated result is
patched. The override is recorded in metadata and the client confirmed generator
2 in StartGame. Use `--generator original` for an unmodified generator setting.

One seed-123 run with clients A/B completed 241 tick calls, including **200 full
ticks after both clients initialized**. It captured 83,954 level RNG words in
total, of which 70,226 were in the post-initialization window. The latter's
consumers were:

| Consumer (first non-Random caller) | Draws |
|---|---:|
| LevelChunk::tickBlocks | 22,800 |
| Spawner::tick | 11,532 |
| Spawner::tickDespawn | 11,419 |
| Dimension::backgroundTickSeasons player callback | 8,000 |
| Level::_saveSomeChunks | 5,409 |
| MainChunkSource::getRandomChunk | 5,409 |
| GrassBlock::tick | 2,094 |
| ChunkPos shuffles | 1,120 |
| RandomStrollGoal::canUse | 798 |
| LookAtActorGoal::canUse | 796 |
| RandomLookAroundGoal::canUse | 613 |
| Spawner::popCapAllows | 82 |
| WeighedRandom::getRandomItem | 66 |
| MobSpawnRules::getSpawnCount | 66 |
| RandomLookAroundGoal::start | 12 |
| Player shuffles | 10 |

These counts describe this run, not constants to consume or replay in the
implementation. Chunk availability, dirty state, spawning and AI determine
the actual call sequence. The fresh-world startup had exactly 4,492 palette
shuffle draws and its first empty tick had 112 chunk-offset shuffle draws.

## Translated sampling code

`BlockTickRandom` in `src/world/block_ticks.cpp` translates these instructions:

| Original site | Meaning |
|---|---|
| Data at 0x32d753c | Process-global `_randValue`, initial word 42,184,323 |
| 0x1de566a..0x1de568b | Precipitation step: 32-bit `state * 3 + 0x3c6ef35f`, then arithmetic shift by 2 |
| 0x1de569b..0x1de56d9 | Local weather column: low 4 bits are X; bits 8..11 are Z |
| Float at 0x282863c; 0x1de5991..0x1de5a1e | `trunc(2.5f * subchunk_count)` samples, hence 2 for one subchunk and 40 for sixteen |
| 0x1c637f0 | Allocated height = allocated subchunk span size * 16 |
| 0x1de5ac0..0x1de5b2d | Advance the same LCG; X/Z as above; Y is signed `(value >> 16) % allocated_height` |
| 0x1de5b40..0x1de5c2e | Read primary block then extra-layer block and call tick for each tickable block; not implemented in this checkpoint |

Negative Y samples are preserved. Treating these shifts or the remainder as
unsigned changes which blocks tick and how often grass draws the level RNG.
This LCG is separate from `Random`; it must not consume level RNG words.

`Random::observe_draws` and the optional Server constructor observer provide a
diagnostic hook before the first palette-shuffle draw. The hook reports every
raw word once, including words drawn by higher-level float/gaussian/shuffle
callers, without advancing or reseeding the generator itself.

## Distilled reference and limits

`tests/reference/world_ticks_bds_1.6.0.15.json` retains original values from two
traces: the 200-tick A/B run and a shorter run that placed stone at (0,255,0) to
exercise a 256-block allocated height. It contains:

- The exact 4,492 startup words and 112 first-empty-tick words, compared with a
  real BDS-RE Server through its observer.
- 32 complete original selection groups: 16 each at allocated heights 16 and
  256, including each precipitation-column step, every sampled coordinate and
  the intermediate/final LCG states. There are 672 block positions in total.
- Hashes/configuration for the ignored raw traces. Expected coordinates and
  RNG words are extracted, not generated by the implementation under test.

`world_tick_reference` checks these cases. It does **not** establish that BDS-RE's
later 200 ticks match: those consumers are still missing. In particular, no
fixed draw-count padding, recorded-RNG playback or fabricated mob simulation
has been added. The next integration must preserve the actual `Player::tickWorld`
chunk order, once-per-tick chunk ownership, world light/material state and live
mob decisions before the full draw-by-draw trace can become an acceptance test.

## Further traced evidence

Checkpoint validation: GCC 14.2 Debug and ASan+UBSan, GCC Release, and Clang 19.1
Release each passed 28/28 CTest locally. The two movement/action UDP reference
tests now choose separate ephemeral IPv6 ports, removing their collision on
the default 19133 when CTest runs them concurrently. Their packet expectations
are unchanged. No native CI result is claimed for this checkpoint here.

- Clear-weather `tickBlocks` still draws `nextInt(100000)` and `nextInt(16)`;
  weather values at 0x283c870/0x283c874 are 100000.0f/3000.0f. No fixed skip
  count should substitute for the lightning/rain branches.
- GrassBlock::tick at 0x1e716a0 checks brightness/material above the grass. A
  dark/opaque cover (material translucency below the float 0.2 at 0x2828638)
  can consume one `nextInt(4)` to turn it into dirt; bright
  spread draws Z (`nextInt(3)-1`), Y (`nextInt(2)`), X (`nextInt(3)-1`) in that
  order. It can descend through up to five air blocks and checks the target's
  dirt variant, cover, brightness, solidity and liquid material before mutation.
  A correct light/material provider is required; no guessed lighting rule is
  supplied by the selection helper.
- Dimension's season callback at 0x1d694b0 tries up to ten samples, two float
  draws each, with radius `chunk_radius * 0.7f * 16` and angle in [0,TAU).
  It stops after finding a loaded season-dirty chunk and applying processing.
  Vec3::yRot at 0x1d768b0 uses double libm sin/cos rounded to float, rather
  than the mce sine table. Integrated: `Server::background_tick_seasons`.
- Save scheduling attempts up to 30 chunks (0x1c86ca0), draws a dimension and
  then a chunk, and depends on dirty/save state. The 5,409 observed attempts
  over 200 ticks are not a constant budget.

## GrassBlock function checkpoint

`src/world/grass_ticks.cpp` now translates the server-side GrassBlock::tick
algorithm at 0x1e716a0, with a `GrassTickSource` interface for the original's
BlockSource/light/material queries. It is not yet called by Server::tick.
This avoids replacing the missing host lighting backend with an invented rule.

`gdb_grass_ticks.py`, run with `capture_level_random.py --grass-oracle`, invokes
the original function in the paused, fully loaded flat world. For each case it
restores the soil/grass patch through the original BlockSource::setBlock and
seeds a separate original Random object. Breakpoints at the original function's
query return sites record the actual dependency values and request coordinates:

| Site | Captured observation |
|---|---|
| 0x1e71706 / 0x1e71a35 | Original raw brightness at source/target cover |
| 0x1e7172e / 0x1e71895 / 0x1e7192d / 0x1e719b2 | Original block kind/solidity and read coordinates |
| 0x1e71762 / 0x1e719e7 | Material translucency and liquid flag |
| 0x1e71977 | Original dirt mapped-type value |
| 0x1c59b20, called directly from GrassBlock::tick | Mutation position, dirt/grass target and flags 3 |
| 0x187e704, filtered to the case's original Random | Every raw RNG return word |

Lighting, material values, branch results and RNG outputs are not overridden.
`GrassBlock::MIN_BRIGHTNESS` and `Brightness::MAX` were read as 4/15. The cover
translucency comparison uses the original float **0.2** (`0x3e4ccccd`), not 0.5.

`tests/reference/grass_ticks_bds_1.6.0.15.json` combines two independent oracle
invocations: daylight and `time set 18000`. Each uses seeds 0..15 in five scenes:
bright soil, opaque cover, a spread target reached through descending air,
coarse-dirt rejection, and a grass-path cover. All 160 cases retain every
dependency call, mutation request and raw RNG word. The test checks call order
and arguments as well as results. It includes 48 zero-draw cases, 64 one-draw
cases, 48 three-draw cases and 38 mutation requests.

This verifies the function with recorded real dependency inputs. It does not
verify BDS-RE lighting, global tick scheduling, BlockSource mutation side effects,
weather, mob behavior or the complete level RNG trace. Liquid covers and the
client-side early-return path are statically translated but not exercised by
this corpus. The original clone-Random oracle is separate from the earlier
global level-Random acceptance trace.

Grass checkpoint validation: GCC 14.2 Debug, ASan+UBSan, GCC Release and
Clang 19.1 Release each passed **29/29 CTest** locally, including the new
160-case function comparison and all earlier item/combat/network references.

## Per-player chunk/item scheduler checkpoint

The original run `chunk-item-order-v1` places A at (0.5,4,15.75), B at
(32.5,4,0.5), gives each one dirt, and drops it using ordinary client inventory
transactions. `--tick-order --drop-items` records the actual post-shuffle lists,
chunk actor lists, item owner/position/age, player normalTick and nearby scans.
The trace hash is retained in `chunk_order_bds_1.6.0.15.json`.

The runtime now uses `tick_player_worlds` with its real Level offset/player
lists and loaded columns. Ordinary items tick in owner-chunk order; each chunk
ticks once across overlapping player areas, followed by that player's normalTick
and nearby scan. The shared chunk actor index appends and uses the original
SmallSet swap-last erase. It is ready to hold mob IDs, but no mobs are added yet.

| Original site | Scheduling evidence |
|---|---|
| 0x22f5520 / 0x22fb580 | Player tick offsets are Level +0x8 |
| 0x22f4ba0 | Player::tickWorld chunk traversal |
| 0x1de4501 / 0x1de451d / 0x1de452d | Spawner, despawn, then tickBlocks precede actor snapshot (corrects the handoff's suggested inner order) |
| 0x1de3b30 / 0x1de31e0 / 0x1de3260 / 0x1deb820 | Chunk actor SmallSet enumeration, add, remove and swap-last erase |
| 0x21aa380 / 0x21aa419 | Actor::_updateOwnerChunk queues transfer before normalTick |
| 0x1d65130 / 0x21aa490 | Dimension processes queued moves before player worlds; move removes old owner then appends to new |
| 0x25f8bd3 | Nearby scan follows each player's world, not all players together |

Five distilled original frames (48,49,50,61,70) cover two items, 75 distinct
chunks per frame, overlap and both player orders. Item 12 ticks before item 11
despite being created later. Item 11 crosses z=16 on tick 48, is still owned by
(0,0) at tick 49 entry, and is owned by (0,1) at tick 50 entry. The reference
test compares callback order exactly for captured list/availability inputs, then
runs real runtime drops for three ticks to check this deferred transfer and
once-per-tick age progression. It also checks items stop ticking without players.

This is not a full later-RNG or mob comparison. Missing-target transfer keeps
an inactive host record (marked INFERRED); the original NBT transfer/removal and
reload are not translated. Mixed mob/item storage, original chunk readiness,
nearby scan enumeration order, lighting and spawner/block/AI/season/save
consumers remain unfinished. The original mature chunk's neighbor guard only
applies before postprocessing; a blanket 3x3-neighbor requirement must not be
imposed on already processed chunks.

Scheduler checkpoint validation: GCC 14.2 Debug and ASan+UBSan, GCC Release
and Clang 19.1 Release each passed **30/30 CTest** locally. The broad gameplay
test's old global-item-phase expectation now follows the per-player order
(item announcement before the later player's dirty attributes); original packet
reference expectations and combat's bounded set comparison remain unchanged.

## Level Random consumers, second checkpoint

Trace `world-random-s1` (seed 123, flat, tick-distance 4, players A and B, 105
ticks; draws.jsonl sha256 185283ff…b963) per Level tick:

| ticks | draws |
|---|---|
| 0 | block palette shuffle (4,492) |
| 1, 21, 41, … | the three Level::tick shuffles (56 + 56 + players − 1) |
| 2–8 | none: no dimension exists before the first player |
| 9 (A), 10– (A, B) | seasons 20 per listed player, then 30 × (`nextInt(1)`, `getRandomChunk` `nextInt(size)`) |
| 12– | chunk ticks begin (before the players initialise at 39/45): `postProcessSpawnMobs` on each chunk's post-processing tick, then per chunk `Spawner::tick`, `tickDespawn`, `tickBlocks`; mob goals; fewer save attempts once post-processed chunks need saving |

Implemented in `src/game/server.cpp`:

- `Server::background_tick_seasons` — Dimension::tick 0x1d64e50 →
  backgroundTickSeasons 0x1d64680 → callback 0x1d694b0. Player chunk radius is
  Player +0x1a68 (6 in Player::Player, from RequestChunkRadius). BDS-RE terrain
  has no snow-accumulating biome, so no chunk is season-dirty and all ten tries
  draw.
- `Server::save_some_chunks` — Level::_saveSomeChunks 0x1c86c30 at the end of
  Level::tick; `MainChunkSource::getRandomChunk` 0x1e22a80 draws only for a
  non-empty storage map. `LevelChunk::needsSaving(60, 300)` (0x1de83d0) reads 21
  `DirtyTicksCounter`s {total = INT_MIN, last = 0}: `touch` (0x1df89e0) starts a
  dirty run, `operator++` (safeIncrement) runs in LevelChunk::tick, and
  `setUnsaved` (0x1de4fa0) sets them to `max()` after post-processing. BDS-RE
  keeps neither counters nor chunk states, so it never selects a chunk; the
  original selects its first post-processed chunks from tick 13 and the save
  task then finishes on a worker thread (ticks skipped while +0x19b0 is set are
  timing dependent).
- `Server::tick_spawner` — LevelChunk::tick 0x1de4433..0x1de451d. Spawner::tick
  (0x1c6c020) runs when the chunk centre is within 96 blocks (9216 at
  0x283c86c) of the nearest player (Dimension::distanceToNearestPlayerSqr2D
  0x1d672a0, Y cleared on both sides); doMobSpawning (GameRuleId 5) is the
  default true; `nextInt(2000) > 10` returns. The spawn branch
  (`_spawnMobCluster` 0x1c6ae00) is **not translated**: the stream diverges
  when it is taken. tickDespawn (0x1c6c460): `nextInt(50)`; on 0 a non-empty
  chunk entity set draws `nextInt(0, size)`; only despawnable mobs with no
  player within MAX_SPAWN_DISTANCE are removed.

`world_tick_reference` now also replays ticks 2–11 of that trace (joins before
ticks 9 and 10) and requires every level-Random word to match. Known
divergences after tick 11: BDS-RE ticks chunks only for initialised players
(the original starts at tick 12, during the join), does not model the chunk
post-processing state (+0x1054) or `postProcessSpawnMobs`, has no mobs (goal
draws) and no chunk save selection.
