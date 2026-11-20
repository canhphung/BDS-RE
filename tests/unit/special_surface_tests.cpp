#include "bds/world/overworld.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>

using namespace bds;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected native special-surface corpus");
        std::ifstream in(argv[1]);
        const auto ref = nlohmann::json::parse(in);
        if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad")
            throw std::runtime_error("unexpected original binary");
        std::map<std::uint32_t, std::unique_ptr<OverworldGenerator>> generators;
        unsigned failures = 0, columns = 0, draws = 0;
        for (const auto &c : ref.at("cases")) {
            const auto seed = c.at("seed").get<std::uint32_t>();
            if (!generators.contains(seed))
                generators[seed] = std::make_unique<OverworldGenerator>(seed);
            const int x = c.at("x"), z = c.at("z"), biome_id = c.at("biome");
            TerrainVolume v;
            for (int y = 0; y < 128; ++y)
                v.at(x & 15, y, z & 15) = parse_terrain_block(c.at("initial")[y].get<std::string>().c_str());
            Random random(c.at("random_seed").get<std::uint32_t>());
            std::vector<std::uint32_t> words;
            random.observe_draws([&](auto word) { words.push_back(word); });
            generators.at(seed)->build_surface_at(v, biome_id, x, z, c.at("noise").get<float>(), random);
            random.observe_draws({});
            for (int y = 0; y < 128; ++y) {
                const auto &block = v.at(x & 15, y, z & 15);
                const auto name = std::string(block.name) + ":" + std::to_string(block.data);
                if (name != c.at("after")[y].get<std::string>()) {
                    if (++failures < 12)
                        std::cerr << "seed " << seed << " biome " << biome_id << " at " << x << "," << y
                                  << "," << z << " noise " << c.at("noise") << ": " << name
                                  << " != " << c.at("after")[y] << '\n';
                }
            }
            if (words != c.at("words").get<std::vector<std::uint32_t>>()) {
                if (++failures < 12)
                    std::cerr << "Random stream mismatch: seed " << seed << " biome " << biome_id << " at "
                              << x << "," << z << " draws " << words.size() << " != " << c.at("words").size()
                              << '\n';
            }
            for (const auto &tail : c.at("tail"))
                if (random.next_int() != tail.get<int>())
                    ++failures;
            ++columns;
            draws += static_cast<unsigned>(words.size());
        }
        if (failures)
            throw std::runtime_error(std::to_string(failures) + " native surface/RNG mismatches");
        std::cout << columns << " native columns and " << draws << " Random words match exactly\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
