// OverworldGenerator::postProcess 0x1b38950 and the features it places,
// translated from BDS 1.6.0.15 (Linux). Addresses are cited per function.
#include "bds/world/chunk_post_process.hpp"
#include "bds/world/clay.hpp"
#include "bds/world/decoration.hpp"
#include "bds/world/decorator_data.hpp"
#include "bds/world/tree.hpp"
#include "bds/world/vegetation.hpp"
#include <algorithm>
#include <cstring>

namespace bds {
namespace {
bool legacy_is(const TerrainBlock &b, const char *name) { return std::strcmp(b.name, name) == 0; }

// Feature::_placeBlock 0x1b6ebd0: BlockSource::setBlock with flags 3 (no
// WorldChangeTransaction during generation).
bool place_block(GenRegion &region, int x, int y, int z, const TerrainBlock &block) {
    return region.set_block(x, y, z, block, 3);
}

// LakeFeature (ctor 0x1b5dbb0: +0x18 the lake block, +0x20 air).
class LakeFeature {
  public:
    explicit LakeFeature(TerrainBlock block) : block_(block) {}

    // LakeFeature::place 0x1b5dc70.
    bool place(GenRegion &region, int px, int py, int pz, Random &random) const {
        int ox = px - 8, oy = py, oz = pz - 8;
        while (oy > 0 && region.is_empty(ox, oy, oz))
            --oy;
        oy -= 4;
        bool grid[2048] = {};
        const int blobs = random.next_int(4) + 4;
        for (int n = 0; n < blobs; ++n) {
            const float sx = random.next_float() * 6.0f + 3.0f;
            const float sy = random.next_float() * 4.0f + 2.0f;
            const float sz = random.next_float() * 6.0f + 3.0f;
            // Vec3(16, 8, 16) - size - (2, 4, 2) + (1, 2, 1) + size / 2, then
            // each component times its own nextFloat (x, y, z).
            float cx = 16.0f - sx, cy = 8.0f - sy, cz = 16.0f - sz;
            cx = cx - 2.0f;
            cy = cy - 4.0f;
            cz = cz - 2.0f;
            cx = cx + 1.0f;
            cy = cy + 2.0f;
            cz = cz + 1.0f;
            cx = cx + sx / 2.0f;
            cy = cy + sy / 2.0f;
            cz = cz + sz / 2.0f;
            cx = random.next_float() * cx;
            cy = random.next_float() * cy;
            cz = random.next_float() * cz;
            for (int x = 1; x < 15; ++x)
                for (int z = 1; z < 15; ++z)
                    for (int y = 1; y < 7; ++y) {
                        const float dx = (static_cast<float>(x) - cx) / (sx / 2.0f);
                        const float dy = (static_cast<float>(y) - cy) / (sy / 2.0f);
                        const float dz = (static_cast<float>(z) - cz) / (sz / 2.0f);
                        if (1.0f > dx * dx + dy * dy + dz * dz)
                            grid[(x * 16 + z) * 8 + y] = true;
                    }
        }
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 8; ++y) {
                    if (!check(x, y, z, grid))
                        continue;
                    const auto &m = region.info(ox + x, oy + y, oz + z);
                    if (y >= 4 && m.liquid)
                        return false;
                    if (y < 4 && !m.material_solid &&
                        std::strcmp(region.block(ox + x, oy + y, oz + z).name, block_.name) != 0)
                        return false;
                }
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 8; ++y)
                    if (grid[(x * 16 + z) * 8 + y])
                        place_block(region, ox + x, oy + y, oz + z, y >= 4 ? terrain_block::air : block_);
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 4; y < 8; ++y)
                    if (grid[(x * 16 + z) * 8 + y] && legacy_is(region.block(ox + x, oy + y - 1, oz + z), "dirt") &&
                        sky_brightness(region, ox + x, oy + y, oz + z) > 0)
                        place_block(region, ox + x, oy + y - 1, oz + z, terrain_block::grass);
        if (block_info(block_.name).material == material_type::Lava)
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z)
                    for (int y = 0; y < 8; ++y) {
                        if (!check(x, y, z, grid))
                            continue;
                        if (y >= 4 && random.next_int(2) == 0)
                            continue;
                        if (region.info(ox + x, oy + y, oz + z).material_solid)
                            place_block(region, ox + x, oy + y, oz + z, terrain_block::stone);
                    }
        return true;
    }

  private:
    // LakeFeature::_check 0x1b5e980: an empty cell with a filled neighbour.
    static bool check(int x, int y, int z, const bool *grid) {
        const auto at = [&](int a, int b, int c) { return grid[(a * 16 + c) * 8 + b]; };
        if (at(x, y, z))
            return false;
        return (x < 15 && at(x + 1, y, z)) || (x > 0 && at(x - 1, y, z)) || (z < 15 && at(x, y, z + 1)) ||
               (z > 0 && at(x, y, z - 1)) || (y < 7 && at(x, y + 1, z)) || (y > 0 && at(x, y - 1, z));
    }
    // BlockSource::getBrightnessPair 0x1c5a560, sky part. INFERRED: 0 while
    // the chunks are post-processed (they are lit later, in the NeedsLighting
    // state); the recorded water lakes place no grass.
    static int sky_brightness(const GenRegion &, int, int, int) { return 0; }

    TerrainBlock block_;
};
// MonsterRoomFeature (the generator's +0x500).
class MonsterRoomFeature {
  public:
    // MonsterRoomFeature::place 0x1b549a0.
    bool place(GenRegion &region, int px, int py, int pz, Random &random) const {
        const int height = 3;
        const int sx = random.next_int(2) + 2;
        const int sz = random.next_int(2) + 2;
        int openings = 0;
        for (int x = px - sx - 1; x <= px + sx + 1; ++x)
            for (int y = py - 1; y <= py + height + 1; ++y)
                for (int z = pz - sz - 1; z <= pz + sz + 1; ++z) {
                    const bool solid = region.info(x, y, z).material_solid;
                    if (y == py - 1 && !solid)
                        return false;
                    if (y == py + height + 1 && !solid)
                        return false;
                    if ((x == px - sx - 1 || x == px + sx + 1 || z == pz - sz - 1 || z == pz + sz + 1) && y == py &&
                        region.is_empty(x, y, z) && region.is_empty(x, y + 1, z))
                        ++openings;
                }
        if (openings < 1 || openings > 5)
            return false;
        // The walls run from py + 3 down to py - 1: the ceiling (py + 4) is
        // only tested above, never written.
        for (int x = px - sx - 1; x <= px + sx + 1; ++x)
            for (int y = py + height; y >= py - 1; --y)
                for (int z = pz - sz - 1; z <= pz + sz + 1; ++z) {
                    const bool wall = x == px - sx - 1 || y == py - 1 || z == pz - sz - 1 || x == px + sx + 1 ||
                                      y == py + height + 1 || z == pz + sz + 1;
                    if (!wall) {
                        region.set_block(x, y, z, terrain_block::air, 3); // BlockSource::removeBlock 0x1c5b840
                        continue;
                    }
                    if (y >= 0 && !region.info(x, y - 1, z).material_solid)
                        region.set_block(x, y, z, terrain_block::air, 3);
                    if (!region.info(x, y, z).material_solid)
                        continue;
                    if (y == py - 1 && random.next_int(4) != 0)
                        place_block(region, x, y, z, terrain_block::mossy_cobblestone);
                    else
                        place_block(region, x, y, z, terrain_block::cobblestone);
                }
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 3; ++j) {
                const int dz = random.next_int(sz * 2 + 1) - sz;
                const int dx = random.next_int(sx * 2 + 1) - sx;
                const int x = px + dx, y = py, z = pz + dz;
                if (!region.is_empty(x, y, z))
                    continue;
                int solid = 0;
                if (region.info(x - 1, y, z).material_solid) // west
                    ++solid;
                if (region.info(x + 1, y, z).material_solid) // east
                    ++solid;
                if (region.info(x, y, z - 1).material_solid) // north
                    ++solid;
                if (region.info(x, y, z + 1).material_solid) // south
                    ++solid;
                if (solid != 1)
                    continue;
                const auto facing = facing_data(region, x, y, z, dx, dz);
                place_block(region, x, y, z, TerrainBlock{"chest", static_cast<std::uint16_t>(facing)});
                // RandomizableBlockActorContainerBase::setLootTable with a nextInt() seed.
                region.block_entities.push_back(
                    {x, y, z, "loot_tables/chests/simple_dungeon.json", random.next_int()});
                break;
            }
        place_block(region, px, py, pz, terrain_block::mob_spawner);
        region.block_entities.push_back({px, py, pz, "spawner", random_entity_id(random)});
        return true;
    }

  private:
    // MonsterRoomFeature::_getFacingDataId 0x1b55340: towards the solid
    // neighbour on the side of the offset, else by the offset's sign.
    static int facing_data(const GenRegion &region, int x, int y, int z, int dx, int dz) {
        if (dz > 0 && !region.is_empty(x, y, z + 1))
            return 2;
        if (dz < 0 && !region.is_empty(x, y, z - 1))
            return 3;
        if (dx > 0 && !region.is_empty(x + 1, y, z))
            return 4;
        if (dx < 0 && !region.is_empty(x - 1, y, z))
            return 5;
        if (dz > 0)
            return 2;
        if (dz < 0)
            return 3;
        if (dx > 0)
            return 4;
        if (dx < 0)
            return 5;
        return 2;
    }
    // MonsterRoomFeature::_getRandomEntityId 0x1b55590 (ActorType values).
    static int random_entity_id(Random &random) {
        switch (random.next_int(4)) {
        case 0:
            return 0x2110b22; // skeleton
        case 1:
        case 2:
            return 0x2030b20; // zombie
        case 3:
            return 0x2040b23; // spider
        default:
            return 0x130c;
        }
    }
};
// ---- Block virtuals used by the decoration features ----------------------

// BushBlock::mayPlaceOn 0x1e80360 (TallGrass, FlowerBlock, DoublePlantBlock).
bool bush_may_place_on(const GenRegion &region, int x, int y, int z) {
    const auto &b = region.block(x, y, z);
    return legacy_is(b, "grass") || legacy_is(b, "dirt") || legacy_is(b, "farmland") || legacy_is(b, "podzol");
}

// BlockLegacy::mayPlace 0x1e56480 with the class's mayPlaceOn (vtable
// +0x248): inside the height, a replaceable material, and mayPlaceOn below.
template <class MayPlaceOn>
bool legacy_may_place(const GenRegion &region, int x, int y, int z, MayPlaceOn may_place_on) {
    if (y >= GenChunk::height || y < 0) // BlockSource::getMaxHeight
        return false;
    if (!region.info(x, y, z).replaceable)
        return false;
    return may_place_on(region, x, y - 1, z);
}

// DoublePlantBlock::mayPlace 0x1f134b0 -> 0x1f13390 (face 0): not in water
// or lava, BlockLegacy::mayPlace, and an empty or water cell above.
bool double_plant_may_place(const GenRegion &region, int x, int y, int z) {
    const auto &liquid = block_info(region.liquid_block(x, y, z).name);
    if (liquid.material == material_type::Water || liquid.material == material_type::Lava)
        return false;
    if (!legacy_may_place(region, x, y, z, bush_may_place_on))
        return false;
    return region.is_empty(x, y + 1, z) || region.is_empty_water(x, y + 1, z);
}

// DoublePlantBlock::placeAt 0x1f13c90: the lower block (DoublePlantType) and
// the upper one (UpperBlockBit, data | 8) through a WorldChangeTransaction.
// WorldChangeTransaction::apply 0x198f7e0 walks its unordered_map<BlockPos,
// BlockChange>; libstdc++ links each new node in front, so the upper half is
// written first.
void double_plant_place_at(GenRegion &region, int x, int y, int z, int type, int flags) {
    region.set_block(x, y + 1, z, TerrainBlock{"double_plant", static_cast<std::uint16_t>(type | 8)}, flags);
    region.set_block(x, y, z, TerrainBlock{"double_plant", static_cast<std::uint16_t>(type)}, flags);
}

// DoublePlantFeature::place 0x1b56cc0: 64 tries at nextGaussianInt offsets
// (z 8, y 4, x 8; Random::nextGaussianInt 0x1b52610 = nextInt(n) - nextInt(n)).
bool double_plant_feature_place(GenRegion &region, int px, int py, int pz, Random &random, int type) {
    const auto gaussian = [&](int n) {
        const int a = random.next_int(n);
        return a - random.next_int(n);
    };
    bool placed = false;
    for (int i = 0; i < 64; ++i) {
        const int dz = gaussian(8), dy = gaussian(4), dx = gaussian(8);
        const int x = px + dx, y = py + dy, z = pz + dz;
        if (!region.is_empty(x, y, z) || y >= GenChunk::height - 1 - 1)
            continue;
        if (!double_plant_may_place(region, x, y, z))
            continue;
        double_plant_place_at(region, x, y, z, type, 2);
        placed = true;
    }
    return placed;
}

// SandFeature::place 0x1b6b8e0 (ctor 0x1b6b890: +0x18 block, +0x20 radius):
// only on water; a disk of radius nextInt(radius - 2) + 2, two blocks up and
// down, replacing dirt and grass.
bool sand_feature_place(GenRegion &region, int px, int py, int pz, Random &random, const TerrainBlock &block,
                        int radius) {
    if (region.info(px, py, pz).material != material_type::Water)
        return false;
    const int r = random.next_int(radius - 2) + 2;
    const int h = 2;
    for (int x = px - r; x <= px + r; ++x)
        for (int z = pz - r; z <= pz + r; ++z) {
            const int dx = x - px, dz = z - pz;
            if (dx * dx + dz * dz > r * r)
                continue;
            for (int y = py - h; y <= py + h; ++y) {
                const auto &b = region.block(x, y, z);
                if (legacy_is(b, "dirt") || legacy_is(b, "grass"))
                    place_block(region, x, y, z, block);
            }
        }
    return true;
}

// ---- Biome decorators -------------------------------------------------------

// ForestBiome::Decorator::decorate 0x1dc3ee0 before the base decorate.
// +0x228 is the forest type (ForestBiome subtype: 1 flower forest, 3 roofed).
// NOT TRANSLATED YET: the roofed forest grid (type 3).
void forest_pre_decorate(GenRegion &region, Random &random, int ox, int oz, int type) {
    int count = random.next_int(5) - 3;
    if (type == 1)
        count += 2;
    for (int i = 0; i < count; ++i) {
        const int t = random.next_int(3);
        const int plant = t == 0 ? 1 : (t == 1 ? 4 : 5); // syringa, rose, paeonia
        for (int j = 0; j < 5; ++j) {
            const int x = random.next_int(16), z = random.next_int(16);
            const int y = random.next_int(region.heightmap(ox + x, oz + z) + 32);
            if (double_plant_feature_place(region, ox + x, y, oz + z, random, plant))
                break;
        }
    }
}

// PlainsBiome::Decorator::decorate 0x1dcc850 before the base decorate. The
// decorator noise (OverworldDecorator +0x220, PerlinSimplexNoise(2345, 1))
// at ((x + 8) / 200, (z + 8) / 200) picks the flower (+0x1e8) and grass
// (+0x1ec) counts the base decorate then uses; the plains also scatter tall
// grass, and sunflower plains (+0x228, PlainsBiome(id, true): biome 129, the
// mutated copy) sunflowers.
struct DecoratorCounts {
    int flowers = -1, grass = -1; // -1: the decorator's constructed values
};
DecoratorCounts plains_pre_decorate(GenRegion &region, Random &random, int ox, int oz, bool sunflowers,
                                    const PerlinSimplexNoise &noise, bool legacy) {
    DecoratorCounts counts;
    const float n = noise.value2d(static_cast<float>(ox + 8) / 200.0f, static_cast<float>(oz + 8) / 200.0f);
    if (static_cast<double>(n) < -0.8) {
        counts.flowers = 15;
        counts.grass = 5;
    } else if (!legacy) {
        counts.flowers = 4;
        counts.grass = 10;
        for (int i = 0; i < 7; ++i) {
            const int x = random.next_int(16), z = random.next_int(16);
            const int y = random.next_int(region.heightmap(ox + x, oz + z) + 32);
            double_plant_feature_place(region, ox + x, y, oz + z, random, 2); // grass
        }
    }
    if (sunflowers)
        for (int i = 0; i < 10; ++i) {
            const int x = random.next_int(16), z = random.next_int(16);
            const int y = random.next_int(region.heightmap(ox + x, oz + z) + 32);
            double_plant_feature_place(region, ox + x, y, oz + z, random, 0); // sunflower
        }
    return counts;
}

// BlockLegacy::canProvideSupport 0x1e54bf0 for the upward face (1): BlockProperty
// 0x80000, 0x200000 or 0x80. Properties 1 and 2 (slab / stairs halves) and the
// height-state path read block states and are NOT TRANSLATED: they return
// false here, and the only caller (PumpkinFeature, which first requires grass
// below) never reaches them.
bool can_provide_support_up(const BlockInfo &b) {
    return b.has_property(0x80000) || b.has_property(0x200000) || b.has_property(0x80);
}

// PumpkinBlock::mayPlace 0x1e9a780: air or a replaceable material, and the
// block below provides upward support (BlockSupportType 2).
bool pumpkin_may_place(const GenRegion &region, int x, int y, int z) {
    const auto &here = region.info(x, y, z);
    if (!(here.material == material_type::Air || here.replaceable))
        return false;
    return can_provide_support_up(region.info(x, y - 1, z));
}

// ReedBlock::mayPlace 0x1e361e0 (ReedBlock::canSurvive 0x1e36a70 calls it):
// reeds below; else grass, dirt, sand or podzol below, no liquid here, and
// water (or frosted ice) in the liquid layer of a neighbour of the block below.
bool reed_may_place(const GenRegion &region, int x, int y, int z) {
    const auto &below = region.block(x, y - 1, z);
    if (legacy_is(below, "reeds"))
        return true;
    if (!legacy_is(below, "grass") && !legacy_is(below, "dirt") && !legacy_is(below, "sand") &&
        !legacy_is(below, "podzol"))
        return false;
    if (block_info(region.liquid_block(x, y, z).name).liquid)
        return false;
    for (const auto [dx, dz] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
        const auto &liquid = region.liquid_block(x + dx, y - 1, z + dz);
        if (block_info(liquid.name).material == material_type::Water || legacy_is(liquid, "frosted_ice"))
            return true;
    }
    return false;
}

// The native answers of the BlockSource / Block queries tree.cpp and
// vegetation.cpp ask through their environments.
TreeEnvironment tree_environment(GenRegion &region) {
    TreeEnvironment env;
    env.max_height = [] { return GenChunk::height; }; // BlockSource::getMaxHeight 0x1b50550 (+0x30)
    // _prepareSpawn asks VanillaBlocks::mSapling's mayPlaceOn: BushBlock::mayPlaceOn.
    env.may_place_on = [&region](DecorationRegion::Position p, TreeBlock) {
        return bush_may_place_on(region, std::get<0>(p), std::get<1>(p), std::get<2>(p));
    };
    env.block = [&region](DecorationRegion::Position p) {
        const auto &b = region.block(std::get<0>(p), std::get<1>(p), std::get<2>(p));
        return TreeBlock{b.name, b.data};
    };
    // TreeFeature::_isFree(Block) 0x1b5a190: air or leaves material, or grass / dirt.
    env.is_free = [](TreeBlock b) {
        const auto &info = block_info(b.name);
        return info.material == material_type::Air || info.material == material_type::Leaves ||
               std::strcmp(b.name, "grass") == 0 || std::strcmp(b.name, "dirt") == 0;
    };
    env.place_block = [&region](DecorationRegion::Position p, TreeBlock b) {
        return place_block(region, std::get<0>(p), std::get<1>(p), std::get<2>(p), TerrainBlock{b.name, b.data});
    };
    env.is_empty = [&region](DecorationRegion::Position p) {
        return region.is_empty(std::get<0>(p), std::get<1>(p), std::get<2>(p));
    };
    // BlockSource::isSolidBlockingBlock 0x1c5ae40 -> BlockLegacy 0x1e56e90:
    // Material::isSolidBlocking (not +5, blocks motion) and BlockProperty
    // 0x200000 or 0x100000.
    env.is_solid_blocking = [&region](DecorationRegion::Position p) {
        const auto &b = region.info(std::get<0>(p), std::get<1>(p), std::get<2>(p));
        return !b.never_buildable && b.blocks_motion && (b.has_property(0x200000) || b.has_property(0x100000));
    };
    env.heightmap = [&region](DecorationRegion::Position p) { return region.heightmap(std::get<0>(p), std::get<2>(p)); };
    env.material_is = [&region](DecorationRegion::Position p, int type) {
        return region.info(std::get<0>(p), std::get<1>(p), std::get<2>(p)).material == type;
    };
    return env;
}

VegetationEnvironment vegetation_environment(GenRegion &region) {
    VegetationEnvironment env;
    env.block = [&region](DecorationRegion::Position p) {
        const auto &b = region.block(std::get<0>(p), std::get<1>(p), std::get<2>(p));
        return FeatureBlock{b.name, b.data};
    };
    env.is_empty = [&region](DecorationRegion::Position p) {
        return region.is_empty(std::get<0>(p), std::get<1>(p), std::get<2>(p));
    };
    // TallGrass::canSurvive 0x1e9d340 / BushBlock::canSurvive 0x1e800d0:
    // mayPlaceOn (vtable +0x248) of the block below.
    env.can_survive = [&region](DecorationRegion::Position p, FeatureBlock b) {
        const int x = std::get<0>(p), y = std::get<1>(p), z = std::get<2>(p);
        if (std::strcmp(b.name, "tallgrass") == 0 || std::strcmp(b.name, "red_flower") == 0 ||
            std::strcmp(b.name, "yellow_flower") == 0)
            return bush_may_place_on(region, x, y - 1, z);
        if (std::strcmp(b.name, "reeds") == 0)
            return reed_may_place(region, x, y, z);
        return false; // NOT TRANSLATED YET for the other blocks
    };
    env.material_is = [&region](DecorationRegion::Position p, int type) {
        return region.info(std::get<0>(p), std::get<1>(p), std::get<2>(p)).material == type;
    };
    env.heightmap = [&region](DecorationRegion::Position p) { return region.heightmap(std::get<0>(p), std::get<2>(p)); };
    // Block::mayPlaceOn 0x1e6a540 (vtable +0x248) of the block placed:
    // MushroomBlock::mayPlaceOn 0x1e33360 asks Block::isSolid (BlockLegacy +0xaa).
    env.may_place_on = [&region](DecorationRegion::Position p, FeatureBlock b) {
        const int x = std::get<0>(p), y = std::get<1>(p), z = std::get<2>(p);
        if (std::strcmp(b.name, "brown_mushroom") == 0 || std::strcmp(b.name, "red_mushroom") == 0)
            return region.info(x, y, z).solid;
        return false; // NOT TRANSLATED YET for the other blocks
    };
    // Block::mayPlace 0x1e6a5a0 (vtable +0x240).
    env.may_place = [&region](DecorationRegion::Position p, FeatureBlock b) {
        const int x = std::get<0>(p), y = std::get<1>(p), z = std::get<2>(p);
        if (std::strcmp(b.name, "pumpkin") == 0)
            return pumpkin_may_place(region, x, y, z);
        return false; // NOT TRANSLATED YET for the other blocks
    };
    env.place_block = [&region](DecorationRegion::Position p, FeatureBlock b) {
        place_block(region, std::get<0>(p), std::get<1>(p), std::get<2>(p), TerrainBlock{b.name, b.data});
    };
    return env;
}

// OverworldDecorator::_getRandomHeight 0x1da4f10: nextInt(2 x heightmap), or 0.
int random_height(const GenRegion &region, Random &random, int x, int z) {
    const int h = region.heightmap(x, z) * 2;
    return h != 0 ? random.next_int(h) : 0;
}

// The tree features the decorators hand out (decorator members, dumped).
enum class TreeKind { Oak, Birch, TallBirch, Unsupported };
// OverworldDecorator::getTreeFeature 0x1da4e70: FancyTree (+0x140) one in 10,
// else Oak (+0xe0). ForestBiome::Decorator::getTreeFeature 0x1dc3cc0 and
// PlainsBiome::Decorator::getTreeFeature 0x1dccbe0 override it.
TreeKind pick_tree(const BiomeData &b, Random &random) {
    if (b.kind == BiomeClass::Plains)
        return random.next_int(3) == 0 ? TreeKind::Unsupported /* FancyTree */ : TreeKind::Oak;
    if (b.kind == BiomeClass::Forest) {
        if (b.subtype == 3 && random.next_int(3) == 0)
            return TreeKind::Unsupported; // RoofTreeFeature (+0xf8)
        if (b.subtype == 2)
            return TreeKind::Birch; // +0xf0
        if (random.next_int(5) == 0)
            return TreeKind::Birch;
        if (b.subtype == 4)
            return random.next_boolean() ? TreeKind::TallBirch /* +0x198 */ : TreeKind::Birch;
    } else if (b.kind != BiomeClass::Plains && b.kind != BiomeClass::Biome && b.kind != BiomeClass::River &&
               b.kind != BiomeClass::Ocean) {
        return TreeKind::Unsupported; // the other decorators' getTreeFeature: NOT TRANSLATED YET
    }
    return random.next_int(10) == 0 ? TreeKind::Unsupported /* FancyTree */ : TreeKind::Oak;
}

// OakFeature::place 0x1dae2f0 (height nextInt(3) + 4, leaf and trunk type 0;
// the base TreeFeature::place 0x1b5a230 would use + 5) and
// BirchFeature::place 0x1b59c20 (nextInt(3) + 5, + nextInt(7) for the tall
// one at +0x21; types 2), both into TreeFeature::place 0x1b5a290.
bool place_tree(DecorationRegion &view, TreeKind kind, DecorationRegion::Position at, Random &random,
                const TreeEnvironment &env) {
    switch (kind) {
    case TreeKind::Oak: {
        const int height = random.next_int(3) + 4;
        return tree_place(view, at, height, random, env, false, false, {"log", 0}, {"leaves", 0}, {"dirt", 0}, 0);
    }
    case TreeKind::Birch:
    case TreeKind::TallBirch: {
        int height = random.next_int(3) + 5;
        if (kind == TreeKind::TallBirch)
            height += random.next_int(7);
        return tree_place(view, at, height, random, env, false, false, {"log", 2}, {"leaves", 2}, {"dirt", 0}, 2);
    }
    default:
        return false;
    }
}

// FlowerFeature::placeFlower 0x1b5f230: 64 tries at gaussian (z 8, y 4,
// x 8) offsets, an empty cell where the flower's canSurvive (vtable +0x300,
// BushBlock::canSurvive: mayPlaceOn below) holds.
void flower_feature_place(GenRegion &region, int px, int py, int pz, const TerrainBlock &flower, Random &random) {
    for (int i = 0; i < 64; ++i) {
        const int dz = next_gaussian_int(random, 8), dy = next_gaussian_int(random, 4),
                  dx = next_gaussian_int(random, 8);
        const int x = px + dx, y = py + dy, z = pz + dz;
        if (!region.is_empty(x, y, z))
            continue;
        if (!bush_may_place_on(region, x, y - 1, z))
            continue;
        place_block(region, x, y, z, flower);
    }
}

// The decorator's getRandomFlowerBlock (vtable +0x20).
TerrainBlock random_flower(const BiomeData &b, Random &random, int x, int z, const PerlinSimplexNoise &noise) {
    if (b.kind == BiomeClass::Forest && b.subtype == 1) {
        // ForestBiome::Decorator::getRandomFlowerBlock 0x1dc3dc0 (flower forest).
        const float n = noise.value2d(static_cast<float>(x) / 48.0f, static_cast<float>(z) / 48.0f);
        float v = (n + 1.0f) / 2.0f;
        v = v < 0.0f ? 0.0f : (v > 0.9999f ? 0.9999f : v); // mce::Math::clamp
        const int data = static_cast<int>(v * 9.0f);
        return TerrainBlock{"red_flower", static_cast<std::uint16_t>(data == 1 ? 0 : data)};
    }
    if (b.kind == BiomeClass::Plains) {
        // PlainsBiome::Decorator::getRandomFlowerBlock 0x1dcc6f0.
        const float n = noise.value2d(static_cast<float>(x) / 200.0f, static_cast<float>(z) / 200.0f);
        if (static_cast<double>(n) < -0.8)
            return TerrainBlock{"red_flower", static_cast<std::uint16_t>(random.next_int(4) + 4)};
        if (random.next_int(3) > 0) {
            const int t = random.next_int(3);
            return TerrainBlock{"red_flower", static_cast<std::uint16_t>(t == 0 ? 0 : (t == 1 ? 3 : 8))};
        }
    }
    return TerrainBlock{"yellow_flower", 0}; // OverworldDecorator::getRandomFlowerBlock 0x1da4ef0
}

// The verified features of decoration.cpp / tree.cpp / vegetation.cpp work on
// a DecorationRegion; this one reads and writes the GenRegion, so every call
// is a LevelChunk::setBlock as in the original.
class GenRegionView : public DecorationRegion {
  public:
    explicit GenRegionView(GenRegion &region) : region_(region) {}
    TerrainBlock block(Position p) const override {
        const auto [x, y, z] = p;
        return region_.block(x, y, z);
    }
    bool set_block(Position p, TerrainBlock b, int flags) override {
        const auto [x, y, z] = p;
        return region_.set_block(x, y, z, b, flags);
    }

  private:
    GenRegion &region_;
};

// The decorator a biome uses: OverworldBiome +0xc0, a MutatedBiome's parent's.
const BiomeData &decorating_biome(int id) {
    const auto *b = biome(id);
    if (b && b->kind == BiomeClass::Mutated)
        b = biome(b->parent);
    return *b;
}
} // namespace

PostProcessReport run_post_process(GenRegion &region, std::uint32_t seed, int chunk_x, int chunk_z,
                                   PostProcessState &state) {
    PostProcessReport report;
    Random random(0); // ThreadData +0x41810, reseeded below
    const auto reseed = [&] {
        random.set_seed(seed);
        const int a = random.next_int() / 2 * 2 + 1;
        const int b = random.next_int() / 2 * 2 + 1;
        const auto mixed = static_cast<std::uint32_t>(chunk_x) * static_cast<std::uint32_t>(a) +
                           static_cast<std::uint32_t>(chunk_z) * static_cast<std::uint32_t>(b);
        random.set_seed(mixed ^ seed);
    };
    const int ox = chunk_x * 16, oz = chunk_z * 16;
    const int biome_id = region.biome(ox + 15, oz + 15); // BlockSource::getBiome(LevelChunk::getMax)
    reseed();
    // Water lake: not in Desert / DesertHills, 1 in 4.
    bool lake = false;
    if (biome_id != biome_id::Desert && biome_id != biome_id::DesertHills && random.next_int(4) == 0) {
        const int x = random.next_int(3, 12);
        const int y = random.next_int(128);
        const int z = random.next_int(3, 12);
        // `y < 60` only matters with OverworldGenerator +0x48 set (false here).
        LakeFeature(terrain_block::water).place(region, ox + x, y, oz + z, random);
        lake = true;
    }
    // Lava lake: 1 in 8 when no water lake was attempted, at y 60..63, or
    // higher 1 in 10 (the original skips y < 60).
    if (!lake && random.next_int(8) == 0) {
        const int x = random.next_int(3, 12);
        const int y = random.next_int(random.next_int(120) + 8);
        const int z = random.next_int(3, 12);
        if (y >= 60 && (y < 64 || random.next_int(10) == 0))
            LakeFeature(terrain_block::lava).place(region, ox + x, y, oz + z, random);
    }
    report.stage = "lakes";
    reseed(); // 0x1b38ea5: the same seed again before the structures
    // NOT TRANSLATED YET (phase 4): the structure features' postProcess
    // (vtable +0x20 of the generator's +0x158, then +0x440, +0x238, +0x360,
    // +0x518, +0x6f8). They write and draw only where a structure crosses the
    // chunk, so the dungeons below differ there.
    // Dungeons: 8 attempts at the chunk origin + nextInt(16) + 8, y nextInt(128).
    for (int i = 0; i < 8; ++i) {
        const int x = ox + random.next_int(16) + 8;
        const int y = random.next_int(128);
        const int z = oz + random.next_int(16) + 8;
        MonsterRoomFeature().place(region, x, y, z, random);
    }
    report.stage = "dungeons";
    // NOT TRANSLATED YET (phase 4): the structure features +0x7e0, +0x8c8, +0x610.
    // The generator's vcall +0xc8 (LevelChunk) and the bonus chest (off by
    // default, LevelData::hasBonusChestEnabled) come next.
    reseed(); // 0x1b3949f: once more before Biome::decorate
    // Biome::decorate (vtable +0x88) of the biome at the chunk's max corner:
    // OverworldBiome::decorate 0x1db5a70 calls its decorator (+0xc0, vtable
    // +0); MutatedBiome::decorate 0x1dba8e0 its parent's.
    const auto &decorating = decorating_biome(biome_id);
    GenRegionView view(region);
    // OverworldDecorator +0x220 (OverworldDecorator::OverworldDecorator 0x1da31e0).
    static const PerlinSimplexNoise decorator_noise = [] {
        Random noise_random(2345);
        return PerlinSimplexNoise(noise_random, 1);
    }();
    DecoratorCounts counts;
    if (decorating.kind == BiomeClass::Forest)
        forest_pre_decorate(region, random, ox, oz, decorating.subtype);
    else if (decorating.kind == BiomeClass::Plains)
        counts = plains_pre_decorate(region, random, ox, oz, decorating.id == 129, decorator_noise, false);
    if (counts.flowers >= 0) { // the plains decorator's fields, shared by every chunk
        state.plains_flowers = counts.flowers;
        state.plains_grass = counts.grass;
    }
    report.stage = "biome decorator";
    // OverworldDecorator::decorate 0x1da5250 starts with decorateOres
    // (decorator vtable +8; MesaBiome's decorator adds gold).
    decorate_ores(view, {ox, 0, oz}, random, decorating.kind == BiomeClass::Mesa);
    report.stage = "ores";
    const auto *data = decorator_data(decorating.id);
    if (!data)
        return report;
    // Sand disks (+0x204, feature +0x10), clay (+0x208, +0x8), gravel disks
    // (+0x200, +0x18): x and z from nextInt(16) (x first), y from
    // getAboveTopSolidBlock(x, z, false, false).
    const auto surface_column = [&](int &x, int &y, int &z) {
        x = ox + random.next_int(16);
        z = oz + random.next_int(16);
        y = region.above_top_solid(x, z, false, false);
    };
    for (int i = 0; i < data->sand; ++i) {
        int x, y, z;
        surface_column(x, y, z);
        sand_feature_place(region, x, y, z, random, terrain_block::sand, data->sand_radius);
    }
    ClayEnvironment clay;
    clay.block = [&](DecorationRegion::Position p) {
        const auto &b = region.block(std::get<0>(p), std::get<1>(p), std::get<2>(p));
        return ClayBlock{b.name, b.data};
    };
    clay.is_water = [&](DecorationRegion::Position p) {
        return region.info(std::get<0>(p), std::get<1>(p), std::get<2>(p)).material == material_type::Water;
    };
    clay.place_block = [&](DecorationRegion::Position p, ClayBlock b) {
        place_block(region, std::get<0>(p), std::get<1>(p), std::get<2>(p), TerrainBlock{b.name, b.data});
    };
    for (int i = 0; i < data->clay; ++i) {
        int x, y, z;
        surface_column(x, y, z);
        place_clay(view, {x, y, z}, data->clay_size, random, clay);
    }
    for (int i = 0; i < data->gravel; ++i) {
        int x, y, z;
        surface_column(x, y, z);
        sand_feature_place(region, x, y, z, random, terrain_block::gravel, data->gravel_radius);
    }
    report.stage = "disks";
    // Tree and grass counts 0x1da559b..0x1da5709. With nextInt(10) == 0 the
    // trees are nextFloat(trees) and the grass count is the plains
    // decorator's (VanillaBiomes::mPlains, decorator +0x1ec).
    int grass = decorating.kind == BiomeClass::Plains ? state.plains_grass : data->grass;
    const int flowers = decorating.kind == BiomeClass::Plains ? state.plains_flowers : data->flowers;
    int trees = 0;
    if (data->trees > 0.0f) {
        if (random.next_int(10) == 0) {
            trees = static_cast<int>(random.next_float(data->trees));
            grass = state.plains_grass;
        } else if (data->trees >= 1.0f) {
            const int base = static_cast<int>(data->trees);
            trees = base + std::max(0, random.next_int(10) - 7);
        } else if (data->trees > 0.0f) {
            const float f = random.next_float();
            trees = data->trees > f ? 1 : 0;
        }
    }
    if (data->field_218 > 0) {
        // NOT TRANSLATED YET: the +0x218 loop of SeagrassFeature (+0x1d0) at
        // (x, 54, z) 0x1da5709..0x1da5838.
        report.stage = "tree counts (seagrass loop not translated)";
        return report;
    }
    // The tree loop 0x1da5838..0x1da595b: the decorator's getTreeFeature
    // (vtable +0x10), _getRandomTreePosition 0x1da50c0 (z then x, 1 +
    // nextInt(14), y = heightmap), _placeFeature, then (unless the legacy
    // flag) TallGrassFeature(tallgrass:1)::place(pos, random, 23, 3).
    const auto tree_env = tree_environment(region);
    const auto plant_env = vegetation_environment(region);
    for (int i = 0; i < trees; ++i) {
        const TreeKind kind = pick_tree(decorating, random);
        const int z = oz + 1 + random.next_int(14);
        const int x = ox + 1 + random.next_int(14);
        const int y = region.heightmap(x, z);
        if (!place_tree(view, kind, {x, y, z}, random, tree_env)) {
            if (kind == TreeKind::Unsupported) {
                report.stage = "trees (an untranslated tree feature was picked)";
                return report;
            }
        }
        place_tall_grass(view, {x, y, z}, FeatureBlock{"tallgrass", 1}, random, plant_env, 23, 3);
    }
    report.stage = "trees";
    if (data->huge_mushrooms > 0) {
        report.stage = "trees (huge mushrooms not translated)";
        return report; // NOT TRANSLATED YET: 0x1da595b..0x1da5a79, HugeMushroomFeature (+0x90)
    }
    // Flowers 0x1da5a79..0x1da5c20: x, z nextInt(16), y = nextInt(heightmap
    // + 32), the decorator's flower, then FlowerFeature (+0x78) placeFlower
    // only when that position's material is NOT air (Material::operator!= of
    // Material::getMaterial(0)).
    for (int i = 0; i < flowers; ++i) {
        const int dx = random.next_int(16), dz = random.next_int(16);
        const int y = random.next_int(region.heightmap(ox + dx, oz + dz) + 32);
        const auto flower = random_flower(decorating, random, ox + dx, oz + dz, decorator_noise);
        if (region.info(ox + dx, y, oz + dz).material != material_type::Air)
            flower_feature_place(region, ox + dx, y, oz + dz, flower, random);
    }
    report.stage = "flowers";
    // Grass 0x1da5c20..0x1da5d31 (skipped with the legacy flag): x, z
    // nextInt(16), y = _getRandomHeight, the decorator's getGrassFeature
    // (vtable +0x18; OverworldDecorator::getGrassFeature 0x1da4ed0: +0x130,
    // INFERRED tallgrass:1) placed through its 3-argument place (90, 8).
    for (int i = 0; i < grass; ++i) {
        const int dx = random.next_int(16), dz = random.next_int(16);
        const int y = random_height(region, random, ox + dx, oz + dz);
        if (decorating.kind != BiomeClass::Forest && decorating.kind != BiomeClass::Plains &&
            decorating.kind != BiomeClass::Biome && decorating.kind != BiomeClass::River) {
            report.stage = "flowers (this decorator's grass feature is not translated)";
            return report;
        }
        place_tall_grass(view, {ox + dx, y, oz + dz}, FeatureBlock{"tallgrass", 1}, random, plant_env);
    }
    report.stage = "grass";
    // Dead bushes (+0x1f0, DeadBushFeature +0xc8) 0x1da5d31..0x1da5e6e,
    // waterlilies (+0x1e0) and mushrooms (+0x1f4): NOT TRANSLATED YET for a
    // non-zero count.
    if (data->dead_bushes > 0 || data->waterlily > 0 || data->mushrooms > 0) {
        report.stage = "grass (dead bush / waterlily / mushroom counts not translated)";
        return report;
    }
    // Always: one in 4 a brown mushroom (FlowerFeature +0x80) at the heightmap,
    // one in 8 a red one (+0x88) at _getRandomHeight 0x1da6202..0x1da638e.
    if (random.next_int(4) == 0) {
        const int dx = random.next_int(16), dz = random.next_int(16);
        const int y = random_height(region, random, ox + dx, oz + dz);
        place_flower(view, {ox + dx, y, oz + dz}, FeatureBlock{"brown_mushroom", 0}, random, plant_env);
    }
    if (random.next_int(8) == 0) {
        const int dx = random.next_int(16), dz = random.next_int(16);
        const int y = random_height(region, random, ox + dx, oz + dz);
        place_flower(view, {ox + dx, y, oz + dz}, FeatureBlock{"red_mushroom", 0}, random, plant_env);
    }
    // One in 18: +0x20c kelp (+0x1a8) at (nextInt(35), 43, nextInt(35)).
    if (random.next_int(18) == 0 && data->field_20c > 0) {
        report.stage = "mushrooms (kelp not translated)";
        return report;
    }
    report.stage = "mushrooms";
    // Reeds: +0x1f8 and 10 more, at _getRandomHeight (ReedsFeature +0x98).
    for (int i = 0; i < data->reeds + 10; ++i) {
        const int dx = random.next_int(16), dz = random.next_int(16);
        const int y = random_height(region, random, ox + dx, oz + dz);
        place_reed(view, {ox + dx, y, oz + dz}, FeatureBlock{"reeds", 0}, random, plant_env);
    }
    report.stage = "reeds";
    // One in 32 a pumpkin patch at _getRandomSurfacePosition (PumpkinFeature +0xb0).
    if (random.next_int(32) == 0) {
        const int z = oz + random.next_int(16), x = ox + random.next_int(16);
        const int h = region.heightmap(x, z);
        const int y = h != 0 ? random.next_int(h) : 0;
        place_pumpkin(view, {x, y, z}, FeatureBlock{"pumpkin", 0}, random, plant_env, FeatureBlock{"grass", 0});
    }
    report.stage = "pumpkins";
    if (data->cactus > 0) {
        report.stage = "pumpkins (cactus not translated)";
        return report;
    }
    return report;
}
} // namespace bds
