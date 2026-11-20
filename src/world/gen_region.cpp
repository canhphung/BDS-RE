#include "bds/world/gen_region.hpp"
#include <algorithm>
#include <cstring>
#include <string>

namespace bds {
const BlockInfo &block_info(std::string_view name) {
    static const auto index = [] {
        std::unordered_map<std::string, std::size_t> map;
        const auto &table = block_infos();
        for (std::size_t i = 0; i < table.size(); ++i)
            map.emplace(table[i].name, i);
        return map;
    }();
    static const BlockInfo unknown{"", "", 0, false, 0, 0, material_type::Air, true, false, false, false, false,
                                   0.0f, false};
    const auto it = index.find(std::string(name));
    return it == index.end() ? unknown : block_infos()[it->second];
}

namespace {
bool same_legacy(const TerrainBlock &a, const TerrainBlock &b) { return std::strcmp(a.name, b.name) == 0; }
bool is_air(const TerrainBlock &b) { return std::strcmp(b.name, "air") == 0; }
constexpr std::uint64_t kProperty2 = 2, kProperty8 = 8; // BlockProperty bits tested by the chunk code

// recomputeHeightMap 0x1de9520 per column: from the allocated height down,
// the first block whose light block (Block::getLight, BlockLegacy +0xb4) or
// whose extra block's is not 0, or that has BlockProperty 2; the height is
// one above it (0 for an empty column).
int column_height(const GenChunk &c, int x, int z) {
    for (int y = c.allocated_height - 1; y >= 0; --y) {
        const int i = GenChunk::index(x, y, z);
        const auto &b = block_info(c.blocks[static_cast<std::size_t>(i)].name);
        const auto extra = c.extra.find(i);
        const auto &e = block_info(extra == c.extra.end() ? "air" : extra->second.name);
        if (b.light_block != 0 || e.light_block != 0 || b.has_property(kProperty2))
            return y + 1;
    }
    return 0;
}
} // namespace

bool is_top_solid(const BlockInfo &m, bool water, bool leaves) {
    if (m.material == material_type::Leaves)
        return leaves;
    if (water && m.material == material_type::Water)
        return true;
    return m.blocks_motion;
}

GenChunk GenRegion::loaded_chunk(const OverworldGenerator &generator, int chunk_x, int chunk_z) {
    GenChunk c;
    c.x = chunk_x;
    c.z = chunk_z;
    const auto volume = generator.generate(chunk_x, chunk_z);
    // LevelChunk::setAllBlocks 0x1de3da0 with the BlockVolume height 128.
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < TerrainVolume::height; ++y)
                c.blocks[static_cast<std::size_t>(GenChunk::index(x, y, z))] = volume.at(x, y, z);
    c.allocated_height = TerrainVolume::height;
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            c.heightmap[static_cast<std::size_t>(z * 16 + x)] = static_cast<std::int16_t>(column_height(c, x, z));
    c.biomes = chunk_biomes(generator.layers(), chunk_x, chunk_z);
    return c;
}

void GenRegion::add(GenChunk chunk) {
    const auto key = std::make_pair(chunk.x, chunk.z);
    chunks_.insert_or_assign(key, std::move(chunk));
}

GenChunk *GenRegion::chunk(int chunk_x, int chunk_z) {
    const auto it = chunks_.find({chunk_x, chunk_z});
    return it == chunks_.end() ? nullptr : &it->second;
}
const GenChunk *GenRegion::chunk(int chunk_x, int chunk_z) const {
    const auto it = chunks_.find({chunk_x, chunk_z});
    return it == chunks_.end() ? nullptr : &it->second;
}

const TerrainBlock &GenRegion::block(int x, int y, int z) const {
    if (y < 0 || y >= GenChunk::height)
        return terrain_block::air;
    const auto *c = chunk(x >> 4, z >> 4);
    if (!c)
        return terrain_block::air;
    return c->blocks[static_cast<std::size_t>(GenChunk::index(x & 15, y, z & 15))];
}

const TerrainBlock &GenRegion::extra_block(int x, int y, int z) const {
    if (y < 0 || y >= GenChunk::height)
        return terrain_block::air;
    const auto *c = chunk(x >> 4, z >> 4);
    if (!c)
        return terrain_block::air;
    const auto it = c->extra.find(GenChunk::index(x & 15, y, z & 15));
    return it == c->extra.end() ? terrain_block::air : it->second;
}

bool GenRegion::is_empty(int x, int y, int z) const { return is_air(block(x, y, z)); }

const TerrainBlock &GenRegion::liquid_block(int x, int y, int z) const {
    const auto &extra = extra_block(x, y, z);
    return is_air(extra) ? block(x, y, z) : extra;
}

bool GenRegion::is_empty_water(int x, int y, int z) const {
    return info(x, y, z).material == material_type::Water;
}

void GenRegion::set_extra_block(int x, int y, int z, const TerrainBlock &b) {
    auto *c = chunk(x >> 4, z >> 4);
    if (!c || y < 0 || y >= GenChunk::height)
        return;
    writes.push_back({x, y, z, b, 1});
    const int i = GenChunk::index(x & 15, y, z & 15);
    if (extra_block(x, y, z) == b)
        return;
    if (is_air(b))
        c->extra.erase(i);
    else {
        c->extra[i] = b;
        c->allocated_height = std::max(c->allocated_height, ((y >> 4) + 1) * 16);
    }
}

bool GenRegion::set_block(int x, int y, int z, const TerrainBlock &b, int /*flags*/) {
    if (y < 0 || y >= GenChunk::height)
        return false;
    auto *c = chunk(x >> 4, z >> 4);
    if (!c)
        return false;
    // LevelChunk::setBlock: the oracle hooks its entry, so every call is a write.
    writes.push_back({x, y, z, b, 0});
    const TerrainBlock old = block(x, y, z);
    if (old == b)
        return false;
    c->allocated_height = std::max(c->allocated_height, ((y >> 4) + 1) * 16); // _createSubChunk
    TerrainBlock written = b;
    if (!same_legacy(old, b)) {
        const auto &oi = block_info(old.name), &ni = block_info(b.name);
        if (!ni.can_contain_liquid && !ni.has_property(kProperty8))
            set_extra_block(x, y, z, terrain_block::air);
        else if (oi.liquid && !ni.liquid && !is_air(b) && oi.material == material_type::Water && old.data == 0)
            set_extra_block(x, y, z, old);
        else if (is_air(b)) {
            const TerrainBlock extra = extra_block(x, y, z);
            if (!is_air(extra)) {
                written = extra;
                set_extra_block(x, y, z, terrain_block::air);
            }
        }
    }
    c->blocks[static_cast<std::size_t>(GenChunk::index(x & 15, y, z & 15))] = written;
    // NOT TRANSLATED YET: _lightingCallbacks / placeCallbacks and the
    // BlockSource events (_blockChanged), which only run for chunks in state 8.
    return true;
}

int GenRegion::heightmap(int x, int z) const {
    const auto *c = chunk(x >> 4, z >> 4);
    return c ? c->heightmap[static_cast<std::size_t>((z & 15) * 16 + (x & 15))] : 0;
}

int GenRegion::above_top_solid(int x, int y, int z, bool water, bool leaves) const {
    const auto *c = chunk(x >> 4, z >> 4);
    if (!c)
        return GenChunk::height; // Dimension::getHeight
    // The overworld has no ceiling: scan down from min(y, allocated height).
    int h = std::min(y, c->allocated_height);
    while (h > 0 && !is_top_solid(info(x, h, z), water, leaves))
        --h;
    while (h >= 0 && !is_top_solid(info(x, h, z), water, leaves))
        --h;
    return h >= 0 ? h + 1 : 0;
}

int GenRegion::biome(int x, int z) const {
    const auto *c = chunk(x >> 4, z >> 4);
    return c ? c->biomes[static_cast<std::size_t>((x & 15) + (z & 15) * 16)] & 255 : 0;
}
} // namespace bds
