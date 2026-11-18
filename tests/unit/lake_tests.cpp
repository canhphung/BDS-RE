#include "bds/world/decoration.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using Json = nlohmann::json;
static DecorationRegion::Position pos(const Json &j) { return {j[0], j[1], j[2]}; }
static std::string name(TerrainBlock b) { return std::string(b.name) + ":" + std::to_string(b.data); }
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected original LakeFeature corpus");
        std::ifstream in(argv[1]);
        const auto ref = Json::parse(in);
        if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad")
            throw std::runtime_error("unexpected reference binary");
        unsigned cases = 0, writes = 0, accepted = 0, draws = 0, light_queries = 0;
        for (const auto &c : ref.at("cases")) {
            const auto label = c.at("scene").get<std::string>() + " seed " +
                               std::to_string(c.at("seed").get<std::uint32_t>());
            DecorationRegion region;
            for (int x = -2; x <= 1; ++x)
                for (int z = -2; z <= 1; ++z)
                    region.load_empty_chunk(x, z);
            for (const auto &p : c.at("initial"))
                region.set_block(pos(p), parse_terrain_block(p[3].get<std::string>().c_str()), 0);
            region.take_mutations();
            Random random(c.at("seed").get<std::uint32_t>());
            std::vector<std::uint32_t> words;
            random.observe_draws([&](auto w) { words.push_back(w); });
            std::size_t placement = 0, lighting = 0;
            LakeEnvironment env;
            env.material = [&](TerrainBlock b) {
                const auto &m = ref.at("materials").at(name(b));
                return LakeMaterial{m.at("solid"), m.at("liquid"), m.at("lava")};
            };
            env.sky_brightness = [&](auto p) -> std::uint8_t {
                if (lighting >= c.at("light").size() || p != pos(c.at("light")[lighting]))
                    throw std::runtime_error("lake brightness query mismatch: " + label);
                return c.at("light")[lighting++][3].template get<std::uint8_t>();
            };
            env.placement_observer = [&](const auto &w) {
                if (placement >= c.at("writes").size())
                    throw std::runtime_error("extra lake placement: " + label);
                const auto &expected = c.at("writes")[placement++];
                if (w.position != pos(expected) || name(w.block) != expected[3].template get<std::string>() ||
                    w.flags != expected[4].template get<int>())
                    throw std::runtime_error("lake placement order mismatch actual " +
                                             Json{std::get<0>(w.position), std::get<1>(w.position),
                                                  std::get<2>(w.position), name(w.block), w.flags}
                                                 .dump() +
                                             " expected " + expected.dump() + ": " + label + " index " +
                                             std::to_string(placement - 1));
            };
            const bool result = place_lake(
                region, pos(c.at("at")), parse_terrain_block(c.at("liquid").get<std::string>().c_str()),
                random, env, parse_terrain_block(c.at("upper").get<std::string>().c_str()));
            random.observe_draws({});
            if (result != c.at("result").get<bool>() ||
                words != c.at("words").get<std::vector<std::uint32_t>>())
                throw std::runtime_error("lake result/Random mismatch: " + label);
            if (placement != c.at("writes").size() || lighting != c.at("light").size())
                throw std::runtime_error("lake incomplete placements/brightness queries: " + label);
            for (std::size_t i = 0; i < c.at("initial").size(); ++i)
                if (name(region.block(pos(c.at("initial")[i]))) != c.at("final")[i].get<std::string>())
                    throw std::runtime_error("lake final volume mismatch at " + c.at("initial")[i].dump() +
                                             " actual " + name(region.block(pos(c.at("initial")[i]))) +
                                             " expected " + c.at("final")[i].get<std::string>() + ": " +
                                             label);
            for (const auto &tail : c.at("tail"))
                if (random.next_int() != tail.get<int>())
                    throw std::runtime_error("lake Random continuation mismatch: " + label);
            // Rejections must be atomic: no attempted or changed block writes.
            if (!result && !region.take_mutations().empty())
                throw std::runtime_error("rejected lake changed blocks: " + label);
            ++cases;
            accepted += result;
            writes += static_cast<unsigned>(placement);
            light_queries += static_cast<unsigned>(lighting);
            draws += static_cast<unsigned>(words.size());
        }
        std::cout << cases << " native lakes (" << accepted << " accepted), " << draws << " Random words, "
                  << writes << " ordered placements, " << light_queries << " sky brightness queries match\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
