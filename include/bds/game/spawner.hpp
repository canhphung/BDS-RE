#pragma once
// Natural mob spawning translated from BDS 1.6.0.15 (Linux): Spawner,
// MobSpawnRules, MobSpawnerData, BlockLegacy/GrassBlock::getMobToSpawn and
// BlockSource::findNextSpawnBlockUnder. Every function cites its original
// address in spawner.cpp. The plains spawn list was read from the original's
// memory (tools/re/gdb_spawn_oracle.py; tests/reference/spawn_cluster_bds_1.6.0.15.json "plains").
#include "bds/world/random.hpp"
#include "bds/world/world.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bds::game {
// ActorType values (ActorClassTree: high bits are inherited categories).
namespace actor_type {
inline constexpr std::uint32_t Mob = 0x100, Animal = 0x1300, Monster = 0xb00, WaterAnimal = 0x2300,
                               Chicken = 0x130a, Cow = 0x130b, Pig = 0x130c, Sheep = 0x130d,
                               Horse = 0x205317, Donkey = 0x205318;
} // namespace actor_type
// ActorClassTree::isTypeInstanceOf 0x230a140.
bool is_type_instance_of(std::uint32_t type, std::uint32_t base);
// ActorClassTree::getMobCategory 0x230a040.
std::uint32_t mob_category(std::uint32_t type);
// MobSpawnInfo::categoryID 0x1c50d20: 0 animal, 1 monster, 2 water animal,
// 3 villager (0x30f), 4 ambient (0x8100), otherwise -1.
int category_id(std::uint32_t category);

// MobSpawnRules (ctor 0x1dca400, setters 0x1dca490..0x1dca6ac).
struct MobSpawnRules {
    int min_brightness = 0, max_brightness = 15; // +0x0 +0x4
    bool adjust_for_weather = false;             // +0x8
    int min_height = 0, max_height = 255;        // +0xc +0x10
    int rarity = 0;                              // +0x14
    int surface_cap = -1, underground_cap = -1;  // +0x18 +0x1c
    int min_difficulty = 0, max_difficulty = 3;  // +0x20 +0x24
    int min_spawn_distance = 24, max_spawn_distance = 54; // +0x28 +0x2c
    bool surface = false, underground = false, underwater = false, lava = false, experimental = false; // +0x30..+0x34
    // +0x38 std::function spawn count callback: getDefaultEnemyCountCallback
    // (0x1db5530) for the overworld monsters; empty for animals.
    bool default_enemy_count = false;
};
// MobSpawnerData (0x110 bytes, ctor 0x1bd0310).
struct MobSpawnerData {
    int weight;            // WeighedRandomItem +0x0
    std::uint32_t type;    // +0x68
    const char *identifier; // EntityTypeIdWithoutCategories(type) (+0x8 is empty for these)
    int min_count, max_count; // +0x6c +0x70
    MobSpawnRules rules;   // +0x78
    bool horse_herd = false; // +0xd0: MobSpawnUtils::spawnHorseHerd
    float width, height;   // behavior pack minecraft:collision_box
};
// Biome::getMobs() (Biome +0xa0) of the plains biome used by the flat
// generator, in the original's vector order.
const std::vector<MobSpawnerData> &plains_mobs();

// SpawnConditions (ctor 0x1c6cae0).
struct SpawnConditions {
    bool surface = false, water = false, lava = false, underground = false; // +0..+3
    int brightness = 0;                                                       // +4
    World::Position pos{};                                                    // +8
};

// The BlockSource/Level services the spawner reads.
class SpawnRegion {
  public:
    virtual ~SpawnRegion() = default;
    // BlockSource::hasBlock 0x1c59440: a loaded, writable chunk at the column.
    virtual bool has_chunk(int chunk_x, int chunk_z) const = 0;
    // Block id at a position; air outside loaded chunks or Y range.
    virtual BlockId block(World::Position at) const = 0;
    // BlockSource::getRawBrightness(pos, true, true) 0x1c5a670.
    virtual unsigned raw_brightness(World::Position at) const = 0;
    // BlockSource::canSeeSky 0x1c5ac30.
    virtual bool sky(World::Position at) const = 0;
    // Dimension::fetchNearestPlayer(x, y, z, distance) 0x1d66f80, then the
    // squared distance from that player's position (Actor::getPos) to `at`.
    virtual std::optional<float> nearest_player_distance_sqr(float x, float y, float z, float max_distance) const = 0;
    virtual int difficulty() const = 0; // Level::getDifficulty
    // Level::getTickedMobCountPrevious 0x1c8f9e0.
    virtual int ticked_mob_count_previous() const = 0;
    // LevelChunk::getEntities of a chunk: (ActorType, Mob::isSurfaceMob).
    virtual void chunk_mobs(int chunk_x, int chunk_z,
                            const std::function<void(std::uint32_t, bool)> &visit) const = 0;
    // Spawner::spawnMob 0x1c6a610 (ActorFactory::createSpawnedEntity,
    // Mob::setSurfaceMob, checkSpawnRules, Level::addEntity). Returns the
    // new mob's runtime id, or nothing when checkSpawnRules failed.
    virtual std::optional<std::uint64_t> spawn_mob(const MobSpawnerData &data, float x, float y, float z,
                                                   bool surface) = 0;
    // Actor::setVariant / setMarkVariant (spawnHorseHerd).
    virtual void set_variant(std::uint64_t mob, int variant, int mark_variant) = 0;
};

// Static block facts the spawn code queries (Material::isType, getBlocksMotion,
// Block::isSolid) for the blocks this world can hold.
bool spawn_block_is_air(BlockId block);
bool spawn_block_solid_blocking(BlockId block); // BlockSource::isSolidBlockingBlock
// BlockSource::canSeeSky 0x1c5ac30 / LevelChunk::isSkyLit 0x1de3480.
bool can_see_sky(const World &world, World::Position at);
// Dimension::updateLightRamp 0x1d65400 (overworld ambient 0).
float brightness_ramp(unsigned raw);

class Spawner {
  public:
    // Spawner::postProcessSpawnMobs 0x1c6c5d0 (chunk post-processing).
    void post_process_spawn_mobs(SpawnRegion &region, int chunk_x, int chunk_z, Random &random);
    // Spawner::tick 0x1c6c020 without the gamerule check and the
    // nextInt(2000) gate (done by the caller); `random` is the level Random.
    void tick_spawn(SpawnRegion &region, int chunk_x, int chunk_z, Random &random);
    // Spawner::_updateBaseTypeCount 0x1c6bd40.
    void update_base_type_count(SpawnRegion &region, int chunk_x, int chunk_z);

  private:
    void spawn_mob_cluster(SpawnRegion &region, World::Position &pos, SpawnConditions &conditions, Random &random);
    bool pop_cap_allows(const MobSpawnerData &data, bool surface, bool ignore, Random &random) const;
    int handle_population_cap(const MobSpawnerData &data, const SpawnConditions &conditions, int count) const;
    std::array<std::array<int, 5>, 2> counts_{};        // Spawner +0x8: [surface][categoryID]
    std::array<std::array<int, 113>, 2> type_counts_{}; // Spawner +0x30: [surface][legacy type id]
    int ticked_previous_ = 0;                           // Spawner +0x3b8
};

// BlockSource::findNextSpawnBlockUnder 0x1c61560 (MaterialType Any = 0x28).
bool find_next_spawn_block_under(const SpawnRegion &region, World::Position &pos);
// MobSpawnRules::getSpawnCount 0x1dca6c0.
int spawn_count(const SpawnConditions &conditions, const SpawnRegion &region, Random &random,
                const MobSpawnerData &data);
// Spawner::isSpawnPositionOk 0x1c6b390.
bool is_spawn_position_ok(const MobSpawnRules &rules, std::uint32_t type, const SpawnRegion &region,
                          World::Position pos);
// GrassBlock::getMobToSpawn 0x1e71fb0 / BlockLegacy::getMobToSpawn 0x1e56cd0
// -> BlockLegacy::getTypeToSpawn 0x1e56ad0 (WeighedRandom::getRandomItem).
const MobSpawnerData *mob_to_spawn(const SpawnRegion &region, World::Position pos, Random &random);
} // namespace bds::game
