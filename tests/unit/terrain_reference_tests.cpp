// Overworld terrain generation compared with the original
// (tests/reference/terrain_bds_1.6.0.15.json, recorded with
// tools/re/capture_terrain.py, seed 123): the raw 10 x 10 biome grids
// prepareHeights reads, and the 16 x 16 chunk biomes of the FullChunkData
// packets.
#include "bds/world/biome.hpp"
#include "bds/world/overworld.hpp"
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace bds;

namespace {
std::vector<std::uint8_t> base64(const std::string &text) {
    std::vector<std::uint8_t> out;
    int value = 0, bits = -8;
    for (char c : text) {
        const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const char *p = std::strchr(table, c);
        if (!p || c == 0)
            continue;
        value = (value << 6) + static_cast<int>(p - table);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<std::uint8_t>((value >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}
// The oracle's BlockVolume: palette of "name:data" and uint16 indices.
std::vector<std::string> volume(const nlohmann::json &stage) {
    const auto packed = base64(stage.at("cells").get<std::string>());
    std::vector<std::uint16_t> cells(32768);
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        throw std::runtime_error("inflateInit");
    stream.next_in = const_cast<Bytef *>(packed.data());
    stream.avail_in = static_cast<uInt>(packed.size());
    stream.next_out = reinterpret_cast<Bytef *>(cells.data());
    stream.avail_out = static_cast<uInt>(sizeof(std::uint16_t) * cells.size());
    const int status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (status != Z_STREAM_END)
        throw std::runtime_error("bad volume");
    std::vector<std::string> out;
    for (auto c : cells)
        out.push_back(stage.at("palette")[c].get<std::string>());
    return out;
}
std::string name(const TerrainBlock &b) { return std::string(b.name) + ":" + std::to_string(b.data); }
} // namespace

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
    // prepareHeights: the height field (stone / water / air) per chunk.
    const OverworldGenerator generator(reference.at("seed").get<std::uint32_t>());
    int height_failures = 0, surface_failures = 0, carve_failures = 0, volumes = 0;
    for (const auto &sample : reference.at("volumes")) {
        const int x = sample.at("x"), z = sample.at("z");
        const auto expected = volume(sample.at("heights"));
        TerrainVolume ours;
        generator.prepare_heights(ours, x, z);
        ++volumes;
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (name(ours.blocks[i]) != expected[i] && ++height_failures < 10)
                std::cerr << "heights chunk " << x << "," << z << " cell " << i << ": " << name(ours.blocks[i])
                          << " != " << expected[i] << '\n';
        // buildSurfaces with the loadChunk Random.
        const auto surface = volume(sample.at("surface"));
        Random random(0);
        const auto generated = generator.generate_surface(x, z, random);
        for (std::size_t i = 0; i < surface.size(); ++i)
            if (name(generated.blocks[i]) != surface[i] && ++surface_failures < 10)
                std::cerr << "surface chunk " << x << "," << z << " cell " << i << " (x " << i / 2048 << " z "
                          << i / 128 % 16 << " y " << i % 128 << "): " << name(generated.blocks[i])
                          << " != " << surface[i] << '\n';
        // The four carvers on the same volume and Random.
        const auto carved = volume(sample.at("carved"));
        auto ours_carved = generated;
        generator.carve(ours_carved, x, z, random);
        for (std::size_t i = 0; i < carved.size(); ++i)
            if (name(ours_carved.blocks[i]) != carved[i] && ++carve_failures < 20)
                std::cerr << "carved chunk " << x << "," << z << " cell " << i << " (x " << i / 2048 << " z "
                          << i / 128 % 16 << " y " << i % 128 << "): " << name(ours_carved.blocks[i])
                          << " != " << carved[i] << '\n';
    }
    if (failures || chunk_failures || height_failures || surface_failures || carve_failures) {
        std::cerr << height_failures << " height-field, " << surface_failures << " surface and " << carve_failures
                  << " carver mismatches\n";
        std::cerr << failures << " raw and " << chunk_failures << " chunk biome mismatches\n";
        return 1;
    }
    std::cout << cells << " biome cells and " << volumes << " height fields, surfaces and carved volumes match the original\n";
    return 0;
}
