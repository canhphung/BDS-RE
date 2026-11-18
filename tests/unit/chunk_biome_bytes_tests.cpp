// FullChunkData biome bytes against the original's captured chunk biomes.
//
// The terrain fixture records the 256 biome bytes the original sent for 113
// chunks of seed 123 (LevelChunk::serialize2DMaps 0x1de6280 writes 512 height
// bytes then 256 biome bytes). terrain_reference already checks the generator's
// layer output; this checks the RUNTIME path: NetworkChunk::biomes for a
// kind-3 World must equal those bytes, so the client receives the real biomes
// rather than a constant Plains.
#include "bds/network/chunk.hpp"
#include "bds/world/world.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const std::string &what) {
    if (!condition)
        throw std::runtime_error("check failed: " + what);
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "usage: chunk_biome_bytes_tests <terrain fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open terrain fixture");
        const auto reference = nlohmann::json::parse(input);
        check(reference.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        const auto seed = reference.at("seed").get<std::uint32_t>();

        bds::World world;
        world.configure_terrain({3, seed});

        std::size_t checked = 0, varied = 0;
        for (const auto &expected : reference.at("chunk_biomes")) {
            const auto x = expected.at("x").get<int>();
            const auto z = expected.at("z").get<int>();
            const auto recorded = expected.at("biomes").get<std::vector<int>>();
            check(recorded.size() == 256, "fixture biome count");

            world.ensure_chunk(x, z);
            const auto chunk = bds::protocol::network_chunk(world, x, z);
            for (std::size_t i = 0; i < 256; ++i) {
                const auto want = static_cast<std::uint8_t>(recorded[i] & 0xFF);
                if (chunk.biomes[i] != want)
                    throw std::runtime_error("chunk (" + std::to_string(x) + "," +
                                             std::to_string(z) + ") biome byte " +
                                             std::to_string(i) + " is " +
                                             std::to_string(chunk.biomes[i]) + ", expected " +
                                             std::to_string(want));
            }
            ++checked;
            std::size_t distinct = 0;
            std::array<bool, 256> seen{};
            for (const auto value : chunk.biomes)
                if (!seen[value]) {
                    seen[value] = true;
                    ++distinct;
                }
            if (distinct > 1)
                ++varied;
        }
        check(checked == reference.at("chunk_biomes").size(), "every fixture chunk checked");
        std::cout << checked << " original FullChunkData biome arrays match (" << varied
                  << " with more than one biome)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
