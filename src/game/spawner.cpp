#include "bds/game/spawner.hpp"
#include "bds/world/lighting.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace bds::game {
namespace {
// BDS-RE block ids: 0 air, 1 stone, 2 grass, 3 dirt, 4 bedrock (all non-air
// blocks are full, solid, motion-blocking cubes with no liquid material).
constexpr BlockId kGrass = 2, kBedrock = 4;
World::Position above(World::Position p) { return {std::get<0>(p), std::get<1>(p) + 1, std::get<2>(p)}; }
World::Position below(World::Position p) { return {std::get<0>(p), std::get<1>(p) - 1, std::get<2>(p)}; }
int chunk_of(int block) { return block >> 4; }
MobSpawnRules animal_rules() {
    MobSpawnRules r;
    r.min_brightness = 7; // setBrightnessRange(7, 15)
    r.surface = true;
    return r;
}
MobSpawnRules monster_rules(int min_brightness, int max_brightness) {
    MobSpawnRules r;
    r.min_brightness = min_brightness;
    r.max_brightness = max_brightness;
    r.min_difficulty = 1;
    r.surface = r.underground = true;
    r.default_enemy_count = true;
    return r;
}
} // namespace

bool is_type_instance_of(std::uint32_t type, std::uint32_t base) {
    const auto low = base & 0xffu, high = base & 0xffffff00u;
    if (high != 0 && low == 0)
        return ((type & 0xffffff00u) & high) == high;
    return (type & 0xffu) == low;
}
std::uint32_t mob_category(std::uint32_t type) {
    for (const auto category : {0x1300u, 0x2000b00u, 0x1002300u, 0x13fu, 0x30fu, 0x8100u})
        if (is_type_instance_of(type, category))
            return category;
    return 0x40;
}
int category_id(std::uint32_t category) {
    switch (category) {
    case 0x1300: return 0;
    case 0x2000b00: return 1;
    case 0x1002300: return 2;
    case 0x30f: return 3;
    case 0x8100: return 4;
    default: return -1;
    }
}

const std::vector<MobSpawnerData> &plains_mobs() {
    // Dumped from the original's PlainsBiome (OverworldBiome::OverworldBiome
    // adds 14 entries, PlainsBiome::PlainsBiome the horse and donkey). The
    // monster spawn-count callback is getDefaultEnemyCountCallback.
    static const std::vector<MobSpawnerData> mobs = [] {
        std::vector<MobSpawnerData> m;
        m.push_back({12, actor_type::Sheep, "sheep", 2, 3, animal_rules(), false, 0.9f, 1.3f});
        m.push_back({10, actor_type::Pig, "pig", 1, 3, animal_rules(), false, 0.9f, 0.9f});
        m.push_back({10, actor_type::Chicken, "chicken", 2, 4, animal_rules(), false, 0.6f, 0.8f});
        m.push_back({8, actor_type::Cow, "cow", 2, 3, animal_rules(), false, 0.9f, 1.3f});
        auto slime = monster_rules(0, 15);
        slime.default_enemy_count = false; // spawn count callback differs (see oracle raw)
        m.push_back({100, 0x2000b25, "slime", 1, 1, slime, false, 2.08f, 2.08f});
        m.push_back({100, 0x2040b23, "spider", 1, 1, monster_rules(0, 7), false, 1.4f, 0.9f});
        m.push_back({80, 0x2030b20, "zombie", 2, 4, monster_rules(0, 7), false, 0.6f, 1.9f});
        m.push_back({100, 0x2110b22, "skeleton", 1, 2, monster_rules(0, 7), false, 0.6f, 1.9f});
        auto creeper = monster_rules(0, 7);
        creeper.surface_cap = 5;
        m.push_back({100, 0x2000b21, "creeper", 1, 1, creeper, false, 0.6f, 1.8f});
        m.push_back({100, 0x2000b25, "slime", 1, 1, slime, false, 2.08f, 2.08f});
        m.push_back({10, 0x2000b26, "enderman", 1, 2, monster_rules(0, 7), false, 0.6f, 2.9f});
        m.push_back({5, 0x2000b2d, "witch", 1, 1, monster_rules(0, 7), false, 0.6f, 1.9f});
        MobSpawnRules bat;
        bat.max_brightness = 4;
        bat.max_height = 63;
        bat.underground = true;
        m.push_back({10, 0x2008113, "bat", 2, 2, bat, false, 0.5f, 0.9f});
        auto phantom = monster_rules(0, 7);
        phantom.surface_cap = 5;
        phantom.underground_cap = 0;
        phantom.min_spawn_distance = 0;
        phantom.max_spawn_distance = 10;
        m.push_back({100, 0x2010b3a, "phantom", 1, 1, phantom, false, 0.9f, 0.5f});
        m.push_back({4, actor_type::Horse, "horse", 2, 6, animal_rules(), true, 1.4f, 1.6f});
        m.push_back({1, actor_type::Donkey, "donkey", 2, 6, animal_rules(), false, 1.4f, 1.6f});
        return m;
    }();
    return mobs;
}

bool spawn_block_is_air(BlockId block) { return block == 0; }
bool spawn_block_solid_blocking(BlockId block) { return block != 0; }
bool can_see_sky(const World &world, World::Position at) {
    const auto [x, y, z] = at;
    if (y >= static_cast<int>(Chunk::height)) // Dimension height +0x30
        return true;
    if (y < 0)
        return false;
    const auto *chunk = world.chunk(chunk_of(x), chunk_of(z));
    if (!chunk)
        return false;
    // LevelChunk::isSkyLit: y >= (heightmap & 0xff).
    return y >= static_cast<int>(chunk->heightmap(static_cast<unsigned>(x & 15), static_cast<unsigned>(z & 15)) & 0xff);
}
float brightness_ramp(unsigned raw) {
    // ramp[i] = (1 - f) / (f * 3 + 1) * (1 - ambient) + ambient, f = 1 - i/15.
    const float f = 1.0f - static_cast<float>(raw) / 15.0f;
    return (1.0f - f) / (f * 3.0f + 1.0f);
}

bool find_next_spawn_block_under(const SpawnRegion &region, World::Position &pos) {
    auto &[x, y, z] = pos;
    if (!region.has_chunk(chunk_of(x), chunk_of(z)))
        return false;
    // Descend to the first air (or water) block, past it.
    for (;;) {
        const auto block = region.block(pos);
        --y;
        if (spawn_block_is_air(block)) // Material::isType(Air) || isType(Water)
            break;
        if (y < 0)
            return false;
    }
    for (;;) {
        const auto block = region.block(pos);
        // BlockProperty 0x100 and half slabs recurse; no such block here.
        if (!spawn_block_is_air(block)) // Material::getBlocksMotion
            return true;                 // isType(Ice) or solid: found
        // MaterialType Any (0x28) skips the requested-material check.
        if (--y < 0)
            return false;
    }
}

const MobSpawnerData *mob_to_spawn(const SpawnRegion &region, World::Position pos, Random &random) {
    std::uint32_t category;
    if (region.block(pos) == kGrass) {
        // GrassBlock::getMobToSpawn: animals when the block above is brighter
        // than 8 and sees the sky; otherwise BlockLegacy::getMobToSpawn.
        const auto up = above(pos);
        if (region.raw_brightness(up) > 8 && region.sky(up))
            category = actor_type::Animal;
        else
            category = brightness_ramp(region.raw_brightness(up)) <= 8.0f ? 0x2000000u : 0u;
    } else {
        // BlockLegacy::getMobToSpawn: getBrightness(above) (a ramp value in
        // [0, 1]) compared with 8.0f at 0x2827878, then monsters (0x2000000).
        category = brightness_ramp(region.raw_brightness(above(pos))) <= 8.0f ? 0x2000000u : 0u;
    }
    if (category == 0)
        return nullptr;
    // BlockLegacy::getTypeToSpawn: hardcoded structure spawns (none in this
    // world), then the biome entries whose type is an instance of the category.
    std::vector<const MobSpawnerData *> list;
    for (const auto &data : plains_mobs())
        if (is_type_instance_of(data.type, category))
            list.push_back(&data);
    // WeighedRandom::getRandomItem 0x1e5ab10: no draw for an empty list.
    if (list.empty())
        return nullptr;
    int total = 0;
    for (const auto *data : list)
        total += data->weight;
    int pick = random.next_int(total);
    for (const auto *data : list) {
        pick -= data->weight;
        if (pick < 0)
            return data;
    }
    return nullptr;
}

int spawn_count(const SpawnConditions &c, const SpawnRegion &region, Random &random, const MobSpawnerData &data) {
    const auto &r = data.rules;
    const int y = std::get<1>(c.pos);
    if (y < r.min_height || y > r.max_height)
        return 0;
    if ((c.water && !r.underwater) || (c.lava && !r.lava) || (c.surface && !r.surface) ||
        (c.underground && !r.underground))
        return 0;
    if (r.experimental) // Level::hasExperimentalGameplayEnabled: off
        return 0;
    if (r.rarity > 0 && random.next_int(r.rarity) != 0)
        return 0;
    const int difficulty = region.difficulty();
    if (difficulty < r.min_difficulty || difficulty > r.max_difficulty)
        return 0;
    // adjustForWeather only matters during lightning (clear weather here).
    if (c.brightness < r.min_brightness || c.brightness > r.max_brightness)
        return 0;
    if (r.default_enemy_count) {
        // NOT TRANSLATED: getDefaultEnemyCountCallback (0x1db5530). No mob is
        // spawned; the level Random diverges from here at night.
        return 0;
    }
    // nextFloat squared, scaled to the count range; mce::Math::round (libm round).
    float f = random.next_float();
    f *= f;
    return data.min_count +
           static_cast<int>(std::round(static_cast<double>(f * static_cast<float>(data.max_count - data.min_count))));
}

bool is_spawn_position_ok(const MobSpawnRules &rules, std::uint32_t type, const SpawnRegion &region,
                          World::Position pos) {
    const auto [x, y, z] = pos;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
    if (const auto distance = region.nearest_player_distance_sqr(fx, fy, fz, static_cast<float>(rules.max_spawn_distance)))
        if (static_cast<float>(rules.min_spawn_distance * rules.min_spawn_distance) > *distance)
            return false;
    if (is_type_instance_of(type, 0x1002300) || type == 0x2000b31 || type == 0x3030b6e)
        return false; // water spawners need a liquid block; none exists here
    const auto under = region.block(below(pos));
    if (under == kBedrock) // bedrock, invisible bedrock, barrier
        return false;
    return !spawn_block_solid_blocking(region.block(pos)) && !spawn_block_solid_blocking(region.block(above(pos)));
}

bool Spawner::pop_cap_allows(const MobSpawnerData &data, bool surface, bool ignore, Random &random) const {
    // Spawner::popCapAllows 0x1c6acd0 with Dimension::getPopCap 0x1d645c0. The
    // overworld caps (Dimension::Dimension 0x1d63966..0x1d63a6a): surface
    // animal 4, monster 8, water 36, ambient 0; underground monster 8,
    // ambient 2, others 0.
    if (!is_type_instance_of(data.type, actor_type::Mob))
        return true;
    static constexpr float surface_caps[5]{4.0f, 8.0f, 36.0f, 0.0f, 0.0f};
    static constexpr float underground_caps[5]{0.0f, 8.0f, 0.0f, 0.0f, 2.0f};
    const int category = category_id(mob_category(data.type));
    if (category < 0)
        return false; // INFERRED: an out-of-range index reads other Dimension fields
    int cap = static_cast<int>(surface ? surface_caps[category] : underground_caps[category]);
    if (ignore)
        ++cap;
    if (cap <= 0)
        return false;
    const int count = counts_[surface ? 1 : 0][static_cast<std::size_t>(category)];
    if (count >= cap)
        return false;
    if (ignore)
        return true;
    return random.next_int(cap) > count;
}

int Spawner::handle_population_cap(const MobSpawnerData &data, const SpawnConditions &c, int count) const {
    // Spawner::_handlePopulationCap 0x1c6b6c0: 200 ticked mobs overall.
    if (ticked_previous_ + count > 200)
        count = std::max(200 - ticked_previous_, 0);
    const int cap = c.surface ? data.rules.surface_cap : data.rules.underground_cap;
    if (cap >= 0) {
        const auto legacy = data.type & 0xffu; // getEntityTypeIdLegacy
        if (legacy <= 0x70) {
            const int existing = type_counts_[c.surface ? 1 : 0][legacy];
            if (existing + count >= cap)
                count = cap - existing;
        }
    }
    return count;
}

void Spawner::update_base_type_count(SpawnRegion &region, int chunk_x, int chunk_z) {
    counts_ = {};
    type_counts_ = {};
    ticked_previous_ = region.ticked_mob_count_previous();
    static constexpr std::uint32_t base_categories[5]{0x1300, 0x2000b00, 0x1002300, 0x30f, 0x8100}; // 0x28369a0
    for (int z = chunk_z - 4; z <= chunk_z + 4; ++z)
        for (int x = chunk_x - 4; x <= chunk_x + 4; ++x)
            region.chunk_mobs(x, z, [&](std::uint32_t type, bool surface) {
                for (const auto category : base_categories)
                    if (is_type_instance_of(type, category)) // ActorClassTree::isInstanceOf
                        ++counts_[surface ? 1 : 0][static_cast<std::size_t>(category_id(category))];
                const auto legacy = type & 0xffu;
                if (legacy <= 0x70)
                    ++type_counts_[surface ? 1 : 0][legacy];
            });
}

void Spawner::spawn_mob_cluster(SpawnRegion &region, World::Position &pos, SpawnConditions &c, Random &random) {
    // Spawner::_spawnMobCluster 0x1c6ae00.
    if (spawn_block_is_air(region.block(pos)))
        return;
    // c.water would use Block::getTypeToSpawn(WaterAnimal) on the block above;
    // this world has no water.
    const auto *data = c.water ? nullptr : mob_to_spawn(region, pos, random);
    const auto up = above(pos);
    c.brightness = static_cast<int>(region.raw_brightness(up));
    if (!data)
        return;
    int count = spawn_count(c, region, random, *data);
    if (count <= 0 || !is_spawn_position_ok(data->rules, data->type, region, up))
        return;
    count = handle_population_cap(*data, c, count);
    std::vector<std::uint64_t> mobs;
    const auto [x, y, z] = up;
    for (; count > 0; --count) {
        if (!pop_cap_allows(*data, c.surface, false, random))
            continue;
        // Spawner::spawnMob at Vec3(above) with checkSpawnRules.
        const auto mob = region.spawn_mob(*data, static_cast<float>(x), static_cast<float>(y),
                                          static_cast<float>(z), c.surface);
        if (!mob)
            continue;
        mobs.push_back(*mob);
        const int category = category_id(mob_category(data->type));
        if (category >= 0 && category < 5)
            ++counts_[c.surface ? 1 : 0][static_cast<std::size_t>(category)];
        const auto legacy = data->type & 0xffu;
        if (legacy <= 0x70)
            ++type_counts_[c.surface ? 1 : 0][legacy];
    }
    if (data->horse_herd && mobs.size() > 1) {
        // MobSpawnUtils::spawnHorseHerd 0x1dc6640: one colour for the herd,
        // a mark variant per horse.
        const int variant = random.next_int(7);
        for (const auto mob : mobs)
            region.set_variant(mob, variant, random.next_int(5));
    }
}

void Spawner::post_process_spawn_mobs(SpawnRegion &region, int chunk_x, int chunk_z, Random &random) {
    // doMobSpawning (GameRuleId 5) is the default true.
    if (!region.has_chunk(chunk_x, chunk_z))
        return;
    update_base_type_count(region, chunk_x, chunk_z);
    World::Position pos{chunk_x * 16, 0, chunk_z * 16};
    std::get<0>(pos) += random.next_int(0, 16);
    std::get<2>(pos) += random.next_int(0, 16);
    std::get<1>(pos) = static_cast<int>(Chunk::height); // Dimension::getHeight
    find_next_spawn_block_under(region, pos);
    if (!region.has_chunk(chunk_of(std::get<0>(pos)), chunk_of(std::get<2>(pos))))
        return;
    SpawnConditions c;
    c.surface = true;
    c.pos = pos;
    // getLiquidBlock(above).material is water / lava: never in this world.
    spawn_mob_cluster(region, pos, c, random);
}

void Spawner::tick_spawn(SpawnRegion &region, int chunk_x, int chunk_z, Random &random) {
    update_base_type_count(region, chunk_x, chunk_z);
    World::Position pos{chunk_x * 16, 0, chunk_z * 16};
    std::get<0>(pos) += random.next_int(0, 16);
    std::get<2>(pos) += random.next_int(0, 16);
    std::get<1>(pos) = static_cast<int>(Chunk::height);
    find_next_spawn_block_under(region, pos);
    if (!region.has_chunk(chunk_of(std::get<0>(pos)), chunk_of(std::get<2>(pos))))
        return;
    SpawnConditions c;
    c.surface = true;
    c.pos = pos;
    // LevelChunk::getSpawningAreas: no hardcoded structure areas here.
    spawn_mob_cluster(region, pos, c, random);
    // Then every lower spawn block of the column, as underground spawns
    // (Material Leaves keeps the surface flag; no leaves in this world).
    c.surface = false;
    c.underground = true;
    while (find_next_spawn_block_under(region, pos)) {
        update_base_type_count(region, chunk_x, chunk_z);
        c.pos = pos;
        c.surface = false;
        spawn_mob_cluster(region, pos, c, random);
    }
}
} // namespace bds::game
