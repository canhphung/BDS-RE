#include "bds/game/server.hpp"
#include "bds/world/block_ticks.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "expected world tick reference path");
        std::ifstream input(argv[1]);
        const auto reference = nlohmann::json::parse(input);
        check(reference.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        bds::World world;
        world.configure_terrain({1, reference.at("seed").get<std::uint32_t>()});
        bds::Properties properties;
        bds::AccessLists access(".");
        std::vector<std::uint32_t> words;
        bds::game::Server server(world, properties, access,
                                 [&](std::uint32_t word) { words.push_back(word); });
        check(words == reference.at("startup_words").get<std::vector<std::uint32_t>>(),
              "startup draw order/values differ from original");
        words.clear();
        server.tick();
        check(words == reference.at("first_empty_tick_words").get<std::vector<std::uint32_t>>(),
              "empty first tick draw order/values differ from original");
        // Ticks 2..11 of a two-player capture: the players enter the Level list
        // before ticks 9 and 10; seasons and chunk-save draws follow.
        world.ensure_chunk(0, 0);
        const auto &prefix = reference.at("player_join_prefix");
        unsigned joined = 0;
        for (int tick = 2; tick <= 11; ++tick) {
            const auto key = std::to_string(tick);
            for (int n = prefix.at("joins").value(key, 0); n > 0; --n) {
                ++joined;
                bds::auth::Identity id;
                id.identity = "00000000-0000-4000-8000-00000000000" + std::to_string(joined);
                id.display_name = "Player" + std::to_string(joined);
                id.client_data_json = "{}";
                bds::protocol::StartGame g;
                g.runtime_id = joined;
                g.unique_id = static_cast<std::int64_t>(joined);
                g.x = 0.5f;
                g.y = 5.62f;
                g.z = 0.5f;
                server.prepare(id, g);
            }
            words.clear();
            server.tick();
            check(words == prefix.at("tick_words").at(key).get<std::vector<std::uint32_t>>(),
                  ("level Random draws differ from original at tick " + key).c_str());
        }
        check(reference.at("block_samples").size() == 32, "missing block sample groups");
        bool negative_y = false;
        for (const auto &group : reference.at("block_samples")) {
            const auto height = group.at("height").get<int>();
            const auto expected_count = group.at("sample_count").get<unsigned>();
            check(bds::BlockTickRandom::sample_count(static_cast<unsigned>(height / 16)) == expected_count,
                  "sample count differs from original");
            bds::BlockTickRandom weather(group.at("weather_initial_lcg").get<std::uint32_t>());
            check(weather.next_column() == group.at("weather_column").get<std::array<int, 2>>(),
                  "weather column differs from original");
            check(weather.state() == group.at("weather_lcg").get<std::uint32_t>(),
                  "weather LCG transition differs from original");
            check(weather.state() == group.at("initial_lcg").get<std::uint32_t>(),
                  "unexpected clear-weather step before block sampling");
            bds::BlockTickRandom random(group.at("initial_lcg").get<std::uint32_t>());
            for (const auto &sample : group.at("samples")) {
                const auto position = random.next_position(height);
                check(position == sample.at("position").get<std::array<int, 3>>(),
                      "block tick coordinate differs from original");
                check(random.state() == sample.at("lcg").get<std::uint32_t>(),
                      "LCG word differs from original");
                negative_y |= position[1] < 0;
            }
            check(random.state() == group.at("final_lcg").get<std::uint32_t>(),
                  "final LCG state differs from original");
        }
        check(negative_y, "corpus must retain the original signed negative-Y samples");
        std::cout << "Original startup/join-prefix RNG and 32 block-selection groups match\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
