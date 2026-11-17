# Mobs — natural spawning and replication (BDS 1.6.0.15, Linux)

Status of the mob system translated from the original binary. The addresses
are those of `input/linux/bds/bedrock_server`
(sha256 69142612…c4ad).

## Translated

| Original | BDS-RE |
|---|---|
| LevelChunk::tick +0x1054 finalization state: 0 → (3×3 neighbours loaded, `hasChunksAt` box grown by 1.0) 1 → `Spawner::postProcessSpawnMobs`, `setUnsaved`, 2 (0x1de403b..0x1de4418) | `Server::tick_chunk_state` |
| Spawner::postProcessSpawnMobs 0x1c6c5d0 | `Spawner::post_process_spawn_mobs` |
| Spawner::tick 0x1c6c020 (gate `nextInt(2000) > 10`, surface cluster, then each lower spawn block as underground) | `Server::tick_spawner` + `Spawner::tick_spawn` |
| Spawner::_updateBaseTypeCount 0x1c6bd40 (9×9 chunks, 5 base categories) | `Spawner::update_base_type_count` |
| Spawner::_spawnMobCluster 0x1c6ae00 | `Spawner::spawn_mob_cluster` |
| BlockSource::findNextSpawnBlockUnder 0x1c61560 | `find_next_spawn_block_under` |
| GrassBlock::getMobToSpawn 0x1e71fb0, BlockLegacy::getMobToSpawn 0x1e56cd0, getTypeToSpawn 0x1e56ad0, WeighedRandom::getRandomItem 0x1e5ab10 | `mob_to_spawn` |
| MobSpawnRules::getSpawnCount 0x1dca6c0 (rules layout from the setters 0x1dca490..0x1dca6ac) | `spawn_count` |
| Spawner::isSpawnPositionOk 0x1c6b390, Dimension::fetchNearestPlayer 0x1d66f80 | `is_spawn_position_ok`, `Region::nearest_player_distance_sqr` |
| Spawner::popCapAllows 0x1c6acd0, Dimension::getPopCap 0x1d645c0, caps from Dimension::Dimension 0x1d63966 | `Spawner::pop_cap_allows` |
| Spawner::_handlePopulationCap 0x1c6b6c0 | `Spawner::handle_population_cap` |
| MobSpawnUtils::spawnHorseHerd 0x1dc6640 | in `spawn_mob_cluster` |
| ActorClassTree::isTypeInstanceOf 0x230a140, getMobCategory 0x230a040, MobSpawnInfo::categoryID 0x1c50d20 | same names |
| Mob::checkSpawnRules 0x22bb790, Animal::_getWalkTargetValue 0x22d2960 | `Region::spawn_mob` |
| BlockSource::canSeeSky 0x1c5ac30 / LevelChunk::isSkyLit 0x1de3480, Dimension::updateLightRamp 0x1d65400 | `can_see_sky`, `brightness_ramp` |

The plains `Biome::getMobs()` list (Biome +0xa0, 16 `MobSpawnerData` of 0x110
bytes) was read from the running original by `tools/re/gdb_spawn_oracle.py`;
`spawn_reference` checks the table against it.

## Evidence

`tools/re/capture_level_random.py --spawn-oracle` records, besides every
level-Random draw, each `postProcessSpawnMobs` / passed `Spawner::tick` (chunk,
draw index), each `isSpawnPositionOk` nearest-player answer, the entities each
`_updateBaseTypeCount` counted (mobs wander between spawns) and every
`spawnMob`. `tools/re/make_spawn_reference.py` turns a run into
`tests/reference/spawn_cluster_bds_1.6.0.15.json`; `spawn_reference` re-seeds
the level Random, advances it to each event and runs the translated spawner:
153 events over 342 ticks, 817 level-Random words and the 5 spawned mobs
(identifier and position) match.

`--packet-log` stores every packet the clients received. The AddActor
(numeric ActorType in protocol 282) and MobArmorEquipment bodies the
original sent for a baby chicken and a pig are compared byte for byte with
`add_mob_actor` / `empty_armor_equipment`.

## Mob AI (src/game/mob_ai.cpp)

| Original | BDS-RE |
|---|---|
| GoalSelector::tick 0x23b3950, _canUseInSystem 0x23b3c80, _canCoExist 0x23b3e60 | `tick_goals` |
| LookAtActorGoal canUse/canContinueToUse/start/stop/tick 0x233b300..0x233ba80 | `GoalKind::LookAtPlayer` |
| RandomLookAroundGoal 0x23aa880..0x23aa9b0 | `GoalKind::RandomLookAround` |
| RandomStrollGoal::canUse 0x23c46f0 (the `nextInt(interval)` draw) | `GoalKind::RandomStroll` |
| EatBlockGoal 0x23bec40..0x23beeb0 (ActorEvent 10, grass → dirt at tick 4) | `GoalKind::EatBlock` |
| LookControl::tick 0x2335ef0, Actor::getAttachPos 0x21aaaa0 (locations 3, 7) | `tick_look` |
| Mob::normalTick body turn → BodyControl::clientTick 0x2331a00, clampEnough 0x2331c60 | `tick_body` |
| SpawnActorComponent::tick 0x2439460 (egg timer, `nextInt(6000) + 6000`) | `tick_spawn_actor` |
| mce::Math::wrapDegrees 0x26ddf10 / clampRotate 0x26de0f0; RADDEG/DEGRAD bit patterns | `wrap_degrees`, `clamp_rotate` |
| SpatialActorNetworkData / MoveActorAbsoluteData::isDifferenceSignificant 0x1919f70 (rotation sum > 4°), PacketUtil::degreesToByte 0x18a0f40 | `Server::tick_mob` (MoveActorDelta) |

Goal lists, priorities, control flags and parameters were dumped from the
original's GoalSelector objects (`tools/re/gdb_goal_oracle.py`): the order is
the definition's behavior components alphabetically, then those of the groups
added at spawn; a group component already present replaces it in place.

`tools/re/gdb_mob_ai_oracle.py` records each mob's AI step (draw index,
position), LookAtActorGoal::canUse results and LookControl inputs/outputs;
`tools/re/make_mob_ai_reference.py` builds `tests/reference/mob_ai_bds_1.6.0.15.json`.
`mob_ai_reference` replays 783 steps of 5 mobs: 2164 level-Random words and
778 pitch/head-yaw results match exactly. A mob's replay ends at the step in
which the original starts a random stroll.

## Land/air motion checkpoint (2026-11-17)

The next checkpoint shares Actor collision state with items, translates ordinary
Mob::travel / Actor::moveRelative, auto-step/slide and non-riding pair push, and
connects land/air physics to the runtime. `mob_motion_reference` compares 647
native function cases, including explicit step inputs and a retained-motion
fall. `random_pos_reference` compares 96 destinations/failures and 2,880 own-RNG
words. The destination helper is not yet connected to RandomStroll; navigation
and movement/jump control are still missing. See
[MOB_MOTION.md](MOB_MOTION.md) for addresses, raw-run provenance and boundaries.

## Not yet translated (the level Random diverges when they run)

- PathFinder, PathNavigation, MoveControl and JumpControl: a stroll never
  starts (RandomStrollGoal::canUse draws, then returns false). RandomPos land
  destinations and basic motion/push now have separate verified functions;
  runtime mobs can fall and push each other, but cannot choose and follow paths.
- TemptGoal, FollowParentGoal, PanicGoal, BreedGoal, FloatGoal,
  MountPathingGoal, RunAroundLikeCrazyGoal: their preconditions (tempt items,
  babies following, damage, love mode, water, riders) are not handled; they
  never start.
- SpawnActorComponent::spawnEntity (the egg item and sound).
- INFERRED: Actor::getAttachPos offsets (0, 0.85·h, 0.85·w) / (0, 0.85·h, 0)
  from the oracle values; a player's attach position 3 taken as its eye
  position.
- getDefaultEnemyCountCallback 0x1db5530 (monster cluster size at night):
  `spawn_count` returns 0 there.
- Water spawning (no liquids in this world), structure spawning areas,
  hardcoded spawns, despawning of mobs (`Mob::shouldDespawn`).
- Entity definitions (`minecraft:entity_spawned`) are read from the vanilla
  behavior pack: adult/baby weights, sheep colours, horse/donkey health and
  speed ranges. They draw from the actor's own Random (host thread Random
  here). Horse/donkey baby scale is not applied.
- INFERRED: chunks present when the server starts count as finalized (the
  original stores +0x1054 with the chunk); mob unique ids use the high half
  0xffffffff seen in the capture; the ticked-mob count for the 200 cap counts
  mobs in ticked chunks.

## Mob damage, death and loot (src/game/mob_combat.cpp, src/game/loot.cpp)

| Original | BDS-RE |
|---|---|
| Player::attack 0x22f1570 on a Mob (damage, ATTACK_STRONG/NODAMAGE at the AABB centre, crits, sprint push, weapon wear, exhaustion 0.3) | `Server::attack_mob` (from `use_on_actor`) |
| Mob::_hurt 0x22b37f0 (hurt time / last hurt, setLastHurtByPlayer 0x22bf2f0: 400 ticks, setLastHurtByMob 0x22bf120: 60 ticks) | `Server::hurt_mob` |
| Mob::actuallyHurt 0x22b44c0 (no armour for animals) | `Server::actually_hurt_mob` |
| Mob::hurtEffects 0x22b3b60, Mob::knockback 0x22b4880 (the mob's own Random; motion sent by sendMotionPacketIfNeeded) | `Server::hurt_effects_mob`, `Server::send_mob_motion` |
| Mob::die 0x22b4a00: resolveDeathLoot 0x22b4da0 on a copy of the actor Random, Actor::spawnAtLocation(item, -0.05) 0x21b8e40, ActorEvent DEATH, EventPacket MobKilled to the killer | `Server::die_mob` |
| LootTable::getRandomItems 0x1d0faa0, LootPool::addRandomItems/addRandomItem, LootItem::_createItem, set_count / looting_enchant / furnace_smelt / set_data_from_color_index | `death_loot` (tables of the vanilla behavior pack, adults only) |
| Mob::baseTick 0x22b1770 (hurt time, last attacker timers, death) and Mob::tickDeath 0x22b2970 (removal after 20 ticks, Animal::getExperienceReward level-Random draw, death particles on the actor Random) | `Server::base_tick_mob` |
| Mob::isImmobile in Mob::aiStep 0x22b9f85: a dead mob skips its AI and stops | `Server::tick_mob` |
| PanicGoal 0x23c8740..0x23c8d30 with RandomPos::getPosAvoid 0x231bbc0 (direction filter 0x231b7ca) | `GoalKind::Panic`, `random_stroll_position(direction)` |

Evidence: `tools/re/scenarios.py mob_combat` drives a client against the
original (summoned chicken and cow, punched once a second);
`mob_combat_reference` repeats it on BDS-RE and compares the hit sound,
HURT event, health attribute (2/4), DEATH event, MobKilled EventPacket and
zero health byte for byte, the loot (one raw chicken, 0..2 feathers) and the
RemoveActor after the death time.

Not yet translated: ExperienceOrb::spawnOrbs and the orb entity (the reward
count is drawn from the level Random, but no orb appears), fire and its
smelted loot, Looting, the unique-id high half for dropped items (the
original's item unique ids also carry 0xffffffff).
