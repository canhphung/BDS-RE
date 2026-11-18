// Overworld terrain generation compared with the original
// (tests/reference/terrain_bds_1.6.0.15.json, recorded with
// tools/re/capture_terrain.py, seed 123): the raw 10 x 10 biome grids
// prepareHeights reads, and the 16 x 16 chunk biomes of the FullChunkData
// packets.
#include "bds/world/biome.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace bds;

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "expected terrain reference path\n";
        return 1;
    }
    std::ifstream input(argv[1]);
    const auto reference = nlohmann::json::parse(input);
    const auto layers = overworld_layers(reference.at("seed").get<std::uint32_t>());
    int failures = 0, cells = 0;
    for (const auto &grid : reference.at("raw_biomes")) {
        const int x = grid.at("x"), z = grid.at("z");
        const auto ids = raw_biomes(layers, x * 4 - 2, z * 4 - 2);
        for (int i = 0; i < 100; ++i, ++cells)
            if (ids[static_cast<std::size_t>(i)] != grid.at("ids")[static_cast<std::size_t>(i)].get<int>()) {
                if (++failures < 10)
                    std::cerr << "raw biome chunk " << x << "," << z << " cell " << i << ": "
                              << ids[static_cast<std::size_t>(i)] << " != " << grid.at("ids")[static_cast<std::size_t>(i)]
                              << '\n';
            }
    }
    int chunk_failures = 0;
    for (const auto &chunk : reference.at("chunk_biomes")) {
        const int x = chunk.at("x"), z = chunk.at("z");
        const auto ids = chunk_biomes(layers, x, z);
        for (int i = 0; i < 256; ++i, ++cells)
            if ((ids[static_cast<std::size_t>(i)] & 255) != chunk.at("biomes")[static_cast<std::size_t>(i)].get<int>()) {
                if (++chunk_failures < 10)
                    std::cerr << "chunk biome " << x << "," << z << " cell " << i << ": " << ids[static_cast<std::size_t>(i)]
                              << " != " << chunk.at("biomes")[static_cast<std::size_t>(i)] << '\n';
            }
    }
    if (failures || chunk_failures) {
        std::cerr << failures << " raw and " << chunk_failures << " chunk biome mismatches\n";
        return 1;
    }
    std::cout << cells << " biome cells match the original\n";
    return 0;
}
