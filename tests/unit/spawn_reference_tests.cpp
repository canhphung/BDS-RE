// Replays the original's natural spawn events (Spawner::postProcessSpawnMobs
// and Spawner::tick past its nextInt(2000) gate) from a captured level-Random
// trace: the level Random is re-seeded and advanced to each event's draw
// index, the translated spawner runs on the same chunk, and the words it draws
// and the mobs it spawns must equal the original's.
#include "bds/game/spawner.hpp"
#include "bds/world/lighting.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace bds;
using namespace bds::game;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Mob {
    std::string identifier;
    float x, y, z;
    std::uint32_t type;
    bool surface;
};
class FixtureRegion final : public SpawnRegion {
  public:
    FixtureRegion(World &world, WorldLighting &lighting) : world_(world), lighting_(lighting) {}
    std::vector<std::optional<float>> nearest; // the original's answers, in call order
    std::size_t next_nearest = 0;
    std::vector<Mob> mobs;
    // The entities each original _updateBaseTypeCount call counted (mobs
    // wander between spawning and later spawn attempts): one snapshot per
    // call; a call visits the 81 chunks of its 9x9 area.
    std::vector<std::vector<Mob>> counts;
    std::size_t chunk_visits = 0;
    bool has_chunk(int x, int z) const override {
        world_.ensure_chunk(x, z); // every chunk the original reached was loaded
        return true;
    }
    BlockId block(World::Position at) const override {
        const auto [x, y, z] = at;
        if (y < 0 || y >= static_cast<int>(Chunk::height))
            return 0;
        world_.ensure_chunk(x >> 4, z >> 4);
        return world_.block(at);
    }
    unsigned raw_brightness(World::Position at) const override {
        const auto [x, y, z] = at;
        world_.ensure_chunk(x >> 4, z >> 4);
        return lighting_.raw(at, clear_sky_darken(0)); // early morning of a new world
    }
    bool sky(World::Position at) const override { return can_see_sky(world_, at); }
    std::optional<float> nearest_player_distance_sqr(float, float, float, float) const override {
        auto &self = const_cast<FixtureRegion &>(*this);
        check(self.next_nearest < nearest.size(), "more isSpawnPositionOk calls than the original");
        return nearest[self.next_nearest++];
    }
    int difficulty() const override { return 2; }
    int ticked_mob_count_previous() const override { return static_cast<int>(mobs.size()); }
    void chunk_mobs(int x, int z, const std::function<void(std::uint32_t, bool)> &visit) const override {
        auto &self = const_cast<FixtureRegion &>(*this);
        const auto snapshot = self.chunk_visits++ / 81;
        check(snapshot < counts.size(), "more _updateBaseTypeCount calls than the original");
        for (const auto &m : counts[snapshot])
            if ((static_cast<int>(std::floor(m.x)) >> 4) == x && (static_cast<int>(std::floor(m.z)) >> 4) == z)
                visit(m.type, m.surface);
    }
    std::optional<std::uint64_t> spawn_mob(const MobSpawnerData &data, float x, float y, float z,
                                           bool surface) override {
        mobs.push_back({data.identifier, x, y, z, data.type, surface});
        return mobs.size();
    }
    void set_variant(std::uint64_t, int, int) override {}

  private:
    World &world_;
    WorldLighting &lighting_;
};
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "expected spawn reference path");
        std::ifstream input(argv[1]);
        const auto reference = nlohmann::json::parse(input);
        check(reference.at("binary_sha256") == "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        // The plains Biome::getMobs() list read from the original's memory.
        const auto &plains = plains_mobs();
        check(reference.at("plains").size() == plains.size(), "plains spawn list size differs");
        for (std::size_t i = 0; i < plains.size(); ++i) {
            const auto &e = reference.at("plains")[i];
            const auto &d = plains[i];
            const auto &r = d.rules;
            check(e.at("weight") == d.weight && e.at("type") == d.type && e.at("min") == d.min_count &&
                      e.at("max") == d.max_count && e.at("brightness")[0] == r.min_brightness &&
                      e.at("brightness")[1] == r.max_brightness && e.at("height")[0] == r.min_height &&
                      e.at("height")[1] == r.max_height && e.at("rarity") == r.rarity &&
                      e.at("population_cap")[0] == r.surface_cap && e.at("population_cap")[1] == r.underground_cap &&
                      e.at("difficulty")[0] == r.min_difficulty && e.at("difficulty")[1] == r.max_difficulty &&
                      e.at("spawn_distance")[0] == r.min_spawn_distance &&
                      e.at("spawn_distance")[1] == r.max_spawn_distance && e.at("surface") == r.surface &&
                      e.at("underground") == r.underground && e.at("underwater") == r.underwater,
                  "plains spawn entry " + std::to_string(i) + " differs from the original");
        }
        World world;
        world.configure_terrain({1, reference.at("seed").get<std::uint32_t>()});
        WorldLighting lighting(world);
        FixtureRegion region(world, lighting);
        Spawner spawner;
        std::size_t events = 0, words_checked = 0;
        for (const auto &event : reference.at("events")) {
            Random random(reference.at("seed").get<std::uint32_t>());
            const auto skip = event.at("draw").get<std::size_t>();
            for (std::size_t i = 0; i < skip; ++i)
                random.next_uint();
            std::vector<std::uint32_t> words;
            random.observe_draws([&](std::uint32_t word) { words.push_back(word); });
            region.nearest.clear();
            for (const auto &n : event.at("nearest"))
                region.nearest.push_back(n.is_null() ? std::nullopt : std::optional<float>(n.get<float>()));
            region.next_nearest = 0;
            region.counts.clear();
            for (const auto &snapshot : event.at("counts")) {
                std::vector<Mob> list;
                for (const auto &m : snapshot)
                    list.push_back({"", m[1].get<float>(), m[2].get<float>(), m[3].get<float>(),
                                    m[0].get<std::uint32_t>(), true});
                region.counts.push_back(list);
            }
            region.chunk_visits = 0;
            const auto chunk = event.at("chunk").get<std::array<int, 2>>();
            if (event.at("kind") == "post_process")
                spawner.post_process_spawn_mobs(region, chunk[0], chunk[1], random);
            else
                spawner.tick_spawn(region, chunk[0], chunk[1], random);
            const auto expected = event.at("words").get<std::vector<std::uint32_t>>();
            check(words == expected, "level Random words differ from the original at " +
                                         event.at("kind").get<std::string>() + " tick " +
                                         std::to_string(event.at("tick").get<int>()) + " draw " +
                                         std::to_string(skip) + " (" + std::to_string(words.size()) + " vs " +
                                         std::to_string(expected.size()) + ")");
            check(region.next_nearest == region.nearest.size(), "fewer isSpawnPositionOk calls than the original");
            check(region.chunk_visits == region.counts.size() * 81, "fewer _updateBaseTypeCount calls than the original");
            ++events;
            words_checked += words.size();
        }
        const auto &spawns = reference.at("spawns");
        check(region.mobs.size() == spawns.size(), "spawned mob count differs from the original");
        for (std::size_t i = 0; i < spawns.size(); ++i) {
            const auto position = spawns[i].at("position").get<std::array<float, 3>>();
            check(region.mobs[i].identifier == spawns[i].at("identifier").get<std::string>() &&
                      region.mobs[i].x == position[0] && region.mobs[i].y == position[1] &&
                      region.mobs[i].z == position[2],
                  "spawned mob " + std::to_string(i) + " differs from the original");
        }
        std::cout << events << " original spawn events, " << words_checked << " level-Random words and "
                  << spawns.size() << " spawned mobs match\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
