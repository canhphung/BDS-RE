#include "bds/world/decoration.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using Json = nlohmann::json;
static DecorationRegion::Position pos(const Json &j) { return {j[0], j[1], j[2]}; }
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected native decoration corpus");
        std::ifstream in(argv[1]);
        const auto ref = Json::parse(in);
        if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad")
            throw std::runtime_error("unexpected reference binary");
        unsigned cases = 0, writes = 0;
        const auto &bounds = ref.at("bounds");
        for (const auto &c : ref.at("cases")) {
            DecorationRegion region;
            for (int x = -2; x <= 1; ++x)
                for (int z = -2; z <= 1; ++z)
                    region.load_empty_chunk(x, z);
            for (int x = bounds[0]; x <= bounds[3]; ++x)
                for (int y = bounds[1]; y <= bounds[4]; ++y)
                    for (int z = bounds[2]; z <= bounds[5]; ++z)
                        region.set_block({x, y, z}, terrain_block::stone, 0);
            for (const auto &e : c.at("edits"))
                region.set_block(pos(e), parse_terrain_block(e[3].get<std::string>().c_str()), 0);
            region.take_mutations();
            Random random(c.at("seed").get<std::uint32_t>());
            std::vector<std::uint32_t> words;
            random.observe_draws([&](auto w) { words.push_back(w); });
            const bool result = place_ore(region, pos(c.at("at")),
                                          parse_terrain_block(c.at("target").get<std::string>().c_str()),
                                          c.at("size"), random);
            random.observe_draws({});
            if (result != c.at("result").get<bool>() ||
                words != c.at("words").get<std::vector<std::uint32_t>>())
                throw std::runtime_error("ore result/Random mismatch in case " + std::to_string(cases));
            const auto changes = region.take_mutations();
            if (changes.size() != c.at("writes").size())
                throw std::runtime_error("ore write count case " + std::to_string(cases) + ": " +
                                         std::to_string(changes.size()) +
                                         " != " + std::to_string(c.at("writes").size()));
            for (std::size_t i = 0; i < changes.size(); ++i) {
                const auto &a = changes[i];
                const auto &b = c.at("writes")[i];
                if (a.position != pos(b) ||
                    std::string(a.block.name) + ":" + std::to_string(a.block.data) !=
                        b[3].get<std::string>() ||
                    a.flags != b[4].get<int>())
                    throw std::runtime_error("ore ordered mutation mismatch case " + std::to_string(cases) +
                                             " index " + std::to_string(i));
            }
            for (const auto &tail : c.at("tail"))
                if (random.next_int() != tail.get<int>())
                    throw std::runtime_error("ore RNG continuation mismatch");
            ++cases;
            writes += static_cast<unsigned>(changes.size());
        }
        unsigned batches = 0;
        if (ref.contains("batches"))
            for (const auto &c : ref.at("batches")) {
                DecorationRegion region;
                for (int x = -2; x <= 1; ++x)
                    for (int z = -2; z <= 1; ++z)
                        region.load_empty_chunk(x, z);
                for (int x = bounds[0]; x <= bounds[3]; ++x)
                    for (int y = bounds[1]; y <= bounds[4]; ++y)
                        for (int z = bounds[2]; z <= bounds[5]; ++z)
                            region.set_block({x, y, z}, terrain_block::stone, 0);
                region.take_mutations();
                Random random(c.at("seed").get<std::uint32_t>());
                std::vector<std::uint32_t> words;
                random.observe_draws([&](auto w) { words.push_back(w); });
                Json calls = Json::array();
                decorate_ores(region, pos(c.at("at")), random, c.at("biome").get<int>() == 37,
                              [&](const OrePlacement &p) {
                                  const auto [x, y, z] = p.position;
                                  calls.push_back(
                                      {x, y, z,
                                       std::string(p.target.name) + ":" + std::to_string(p.target.data),
                                       p.size});
                              });
                random.observe_draws({});
                if (calls != c.at("calls") || words != c.at("words").get<std::vector<std::uint32_t>>())
                    throw std::runtime_error("ore decorator call/Random mismatch biome " +
                                             std::to_string(c.at("biome").get<int>()) + " seed " +
                                             std::to_string(c.at("seed").get<std::uint32_t>()));
                const auto changes = region.take_mutations();
                if (changes.size() != c.at("writes").size())
                    throw std::runtime_error("decorator write count mismatch");
                for (std::size_t i = 0; i < changes.size(); ++i) {
                    const auto &a = changes[i];
                    const auto &b = c.at("writes")[i];
                    if (a.position != pos(b) ||
                        std::string(a.block.name) + ":" + std::to_string(a.block.data) !=
                            b[3].get<std::string>() ||
                        a.flags != b[4].get<int>())
                        throw std::runtime_error("decorator mutation order mismatch");
                }
                for (const auto &tail : c.at("tail"))
                    if (random.next_int() != tail.get<int>())
                        throw std::runtime_error("decorator RNG continuation mismatch");
                writes += static_cast<unsigned>(changes.size());
                ++batches;
            }
        std::cout << cases << " native ore features, " << batches << " decorator batches and " << writes
                  << " ordered block mutations match\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
