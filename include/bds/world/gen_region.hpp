#pragma once
// The BlockSource OverworldGenerator::postProcess 0x1b38950 works on: the
// LevelChunks of the 3 x 3 ChunkViewSource around the chunk, as loadChunk
// left them (the carved BlockVolume, setAllBlocks 128 high) plus the writes
// made since. Translated parts of BlockSource / LevelChunk (BDS 1.6.0.15,
// Linux) are cited in gen_region.cpp.
#include "bds/world/block_info.hpp"
#include "bds/world/overworld.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bds {
// One LevelChunk: layer 0 and the extra (liquid) layer, the heightmap
// (LevelChunk +0xd18, index z * 16 + x), the biome ids (fillBiomeData,
// index x + z * 16) and the ChunkState (+0x68).
struct GenChunk {
    static constexpr int height = 256; // Dimension::getHeight
    int x = 0, z = 0;
    std::vector<TerrainBlock> blocks = std::vector<TerrainBlock>(16 * 16 * height, terrain_block::air);
    std::unordered_map<int, TerrainBlock> extra;
    std::array<std::int16_t, 256> heightmap{};
    std::array<int, 256> biomes{};
    int allocated_height = 128; // LevelChunk::getAllocatedHeight: 16 x the SubChunk count
    int state = 2;              // ChunkState; 8 = loaded (BlockSource events fire)
    static int index(int x, int y, int z) { return (x * 16 + z) * height + y; }
};

// A LevelChunk::setBlock (layer 0) or setExtraBlock (layer 1) call, in the
// order the original makes them (the post-process oracle records the same).
struct GenWrite {
    int x, y, z;
    TerrainBlock block;
    int layer;
    bool operator==(const GenWrite &) const = default;
};

class GenRegion {
  public:
    // The chunk as loadChunk 0x1b38610 leaves it: the carved volume and
    // recomputeHeightMap(false) 0x1de9520.
    static GenChunk loaded_chunk(const OverworldGenerator &generator, int chunk_x, int chunk_z);
    void add(GenChunk chunk);
    GenChunk *chunk(int chunk_x, int chunk_z);
    const GenChunk *chunk(int chunk_x, int chunk_z) const;

    // BlockSource::getBlock 0x1c58ad0: air outside 0..height and outside the area.
    const TerrainBlock &block(int x, int y, int z) const;
    const TerrainBlock &extra_block(int x, int y, int z) const; // LevelChunk::getExtraBlock 0x1de2b60
    const BlockInfo &info(int x, int y, int z) const { return block_info(block(x, y, z).name); }
    bool is_empty(int x, int y, int z) const; // BlockSource::isEmptyBlock 0x1c60cb0
    // BlockSource::getLiquidBlock 0x1c5a380: the extra block unless it is air.
    const TerrainBlock &liquid_block(int x, int y, int z) const;
    // BlockSource::isEmptyWaterBlock 0x1c60d00: the block's material is Water.
    bool is_empty_water(int x, int y, int z) const;
    // BlockSource::setBlock 0x1c59bb0 (false outside the height or the area)
    // and LevelChunk::setBlock 0x1de2560 with its extra-layer handling.
    bool set_block(int x, int y, int z, const TerrainBlock &block, int flags);
    void set_extra_block(int x, int y, int z, const TerrainBlock &block); // LevelChunk::setExtraBlock
    // BlockSource::getHeightmap 0x1c5ad20: LevelChunk +0xd18, 0 outside the area.
    int heightmap(int x, int z) const;
    // BlockSource::getAboveTopSolidBlock 0x1c5ab40 / LevelChunk 0x1de8120.
    int above_top_solid(int x, int y, int z, bool water, bool leaves) const;
    int above_top_solid(int x, int z, bool water, bool leaves) const {
        return above_top_solid(x, GenChunk::height - 1, z, water, leaves);
    }
    // BlockSource::getBiome: the chunk's biome id.
    int biome(int x, int z) const;

    std::vector<GenWrite> writes;
    // Block entity data the features set: chest loot tables with their seed,
    // spawner actor types ("spawner", ActorType).
    struct BlockEntityData {
        int x, y, z;
        const char *kind;
        int value;
    };
    std::vector<BlockEntityData> block_entities;

  private:
    std::map<std::pair<int, int>, GenChunk> chunks_;
};

// isTopSolid 0x1de8070.
bool is_top_solid(const BlockInfo &material, bool water, bool leaves);
// MaterialType values (Material +0), from the registry dump.
namespace material_type {
constexpr int Air = 0, Dirt = 1, Wood = 2, Stone = 3, Metal = 4, Water = 5, Lava = 6, Leaves = 7, Plant = 8,
              ReplaceablePlant = 9, Sand = 14, Ice = 18, TopSnow = 20, Snow = 21, Cactus = 22, Clay = 23,
              Vegetable = 24;
}
} // namespace bds
