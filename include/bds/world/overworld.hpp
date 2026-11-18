#pragma once
// OverworldGenerator translated from BDS 1.6.0.15 (Linux): the noises of the
// constructor 0x1b36090, prepareHeights 0x1b36ac0 / getHeights 0x1b373d0 and
// the later stages of loadChunk 0x1b38610. Addresses are cited in overworld.cpp.
#include "bds/world/biome.hpp"
#include "bds/world/noise.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bds {
// Blocks the generator writes, as (BlockLegacy raw name, data) like the
// original's Block objects.
struct TerrainBlock {
    const char *name;
    std::uint16_t data = 0;
    bool operator==(const TerrainBlock &o) const;
};
namespace terrain_block {
inline const TerrainBlock air{"air"}, stone{"stone"}, water{"water"}, bedrock{"bedrock"}, grass{"grass"},
    dirt{"dirt"}, coarse_dirt{"dirt", 1}, podzol{"podzol"}, gravel{"gravel"}, sand{"sand"}, sandstone{"sandstone"},
    ice{"ice"}, waterlily{"waterlily"}, lava{"lava"}, flowing_water{"flowing_water"},
    flowing_lava{"flowing_lava"}, magma{"magma"}, obsidian{"obsidian"}, red_sandstone{"red_sandstone"},
    hardened_clay{"hardened_clay"}, stained_hardened_clay{"stained_hardened_clay"}, mycelium{"mycelium"};
}
// "name:data" (the biome table's surface blocks) as a TerrainBlock.
TerrainBlock parse_terrain_block(const char *text);

// BlockVolume 16 x 128 x 16, index (x * 16 + z) * 128 + y (BlockVolume::index 0x1b30380).
struct TerrainVolume {
    static constexpr int height = 128;
    std::vector<TerrainBlock> blocks = std::vector<TerrainBlock>(16 * 16 * height, terrain_block::air);
    TerrainBlock &at(int x, int y, int z) { return blocks[static_cast<std::size_t>((x * 16 + z) * height + y)]; }
    const TerrainBlock &at(int x, int y, int z) const {
        return blocks[static_cast<std::size_t>((x * 16 + z) * height + y)];
    }
};

class OverworldGenerator {
  public:
    explicit OverworldGenerator(std::uint32_t seed);
    // OverworldGenerator::prepareHeights 0x1b36ac0.
    void prepare_heights(TerrainVolume &volume, int chunk_x, int chunk_z) const;
    // OverworldGenerator::getHeights 0x1b373d0: the 5 x 17 x 5 density grid.
    void heights(float *out, const std::array<int, 100> &biomes, int x, int y, int z) const;
    // OverworldGenerator::buildSurfaces 0x1b37f80 with the chunk's biomes
    // (fillBiomeData) and the loadChunk Random.
    void build_surfaces(TerrainVolume &volume, int chunk_x, int chunk_z, Random &random) const;
    // The same per-column dispatch used by build_surfaces; coordinates are
    // world block coordinates, and noise is the caller's surface-noise value.
    void build_surface_at(TerrainVolume &, int biome_id, int x, int z, float noise, Random &) const;
    // The loadChunk carvers (0x1b387a1..0x1b388c7, skipped when generator
    // +0x48 is set): UnderwaterLargeCaveFeature, UnderwaterCanyonFeature,
    // LargeCaveFeature, CanyonFeature, each ::apply on the chunk with the
    // loadChunk Random (src/world/caves.cpp).
    void carve(TerrainVolume &volume, int chunk_x, int chunk_z, Random &random) const;
    // loadChunk 0x1b38610 up to and including buildSurfaces: Random seeded
    // with x * 0x14609048 + z * 0x7ebe2d5, prepareHeights, buildSurfaces.
    TerrainVolume generate_surface(int chunk_x, int chunk_z, Random &random) const;
    // loadChunk up to and including the carvers.
    TerrainVolume generate(int chunk_x, int chunk_z) const;
    const BiomeLayers &layers() const { return layers_; }

  private:
    std::uint32_t seed_;
    BiomeLayers layers_;
    std::unique_ptr<PerlinNoise> min_limit_, max_limit_, main_; // +0x50, +0x58, +0x60
    std::unique_ptr<PerlinSimplexNoise> surface_;               // +0x68
    std::unique_ptr<PerlinNoise> scale_, depth_, forest_;       // +0x70, +0x78, +0x80
    std::array<float, 25> biome_weights_{};                     // +0x88
    std::unique_ptr<PerlinSimplexNoise> swamp_noise_; // OverworldDecorator +0x220: (Random(2345), 1)
    std::array<std::uint8_t,64> mesa_bands_{};
    std::unique_ptr<PerlinSimplexNoise> mesa_band_noise_, pillar_noise_, pillar_roof_noise_, frozen_temperature_noise_;
    void mesa_surface(const BiomeData &, TerrainVolume &, int x, int z, float noise, Random &) const;
    void frozen_surface(const BiomeData &, TerrainVolume &, int x, int z, float noise, Random &) const;
    int mesa_band(int x,int y,int z) const;
    float frozen_temperature(const BiomeData &,int x,int z) const;
};
} // namespace bds
