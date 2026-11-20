// OverworldGenerator::postProcess 0x1b38950 and the features it places,
// translated from BDS 1.6.0.15 (Linux). Addresses are cited per function.
#include "bds/world/chunk_post_process.hpp"
#include "bds/world/decoration.hpp"
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

PostProcessReport run_post_process(GenRegion &region, std::uint32_t seed, int chunk_x, int chunk_z) {
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
    (void)counts;
    report.stage = "biome decorator";
    // OverworldDecorator::decorate 0x1da5250 starts with decorateOres
    // (decorator vtable +8; MesaBiome's decorator adds gold).
    decorate_ores(view, {ox, 0, oz}, random, decorating.kind == BiomeClass::Mesa);
    report.stage = "ores";
    return report;
}
} // namespace bds
