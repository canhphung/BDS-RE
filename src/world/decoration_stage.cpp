// Runtime decoration stage, wiring the verified features over a generated chunk.
//
// Sources, all translated and verified elsewhere:
//   0x1da6de0  OverworldDecorator::decorateOres   -> decorate_ores
//   0x1b557f0  ClayFeature::place                 -> place_clay
//   0x1b66570  TallGrassFeature::place            -> place_tall_grass
//   0x1b5ef70  FlowerFeature::place               -> place_flower
//   0x1b52430  PumpkinFeature::place              -> place_pumpkin
//   0x1b5d3b0  CactusFeature::place               -> place_cactus
//   0x1b53000  WaterlilyFeature::place            -> place_waterlily
//   0x1b5d070  ReedsFeature::place                -> place_reed
//   0x1b531a0  VinesFeature::place                -> place_vine
//
// The native ORDER is the OverworldDecorator::decorate body (0x1da5250), which
// the project has not translated (DECORATE.md). This stage therefore does not
// claim original ordering: it groups the verified features and documents the
// grouping. When the dispatcher lands, this function is replaced.

#include "bds/world/decoration_stage.hpp"
#include <cstring>

namespace bds {
namespace {
const char *sealed(const TerrainBlock &b) { return b.name; }
FeatureBlock feature_block(const TerrainBlock &b) {
    return FeatureBlock{b.name, b.data};
}
} // namespace

DecorationStageResult decorate_generated_chunk(const TerrainVolume &volume, int chunk_x,
                                               int chunk_z, const DecorationStageSettings &settings) {
    DecorationStageResult result;
    DecorationRegion region;
    region.load_chunk(chunk_x, chunk_z, volume);
    // Cross-column features can reach one chunk out; an empty neighbour reports
    // reads as air and writes as failures, which is the honest boundary.
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            if (dx || dz)
                region.load_empty_chunk(chunk_x + dx, chunk_z + dz);

    const int ox = chunk_x * 16, oz = chunk_z * 16;
    Random ore_random(settings.seed ^ static_cast<std::uint32_t>(chunk_x * 0x14609048 + chunk_z * 0x7ebe2d5));

    // OverworldDecorator::decorateOres 0x1da6de0, called on the chunk's origin.
    // Every attempt uses the chunk's own Random, like the generator's.
    decorate_ores(region, {ox, 0, oz}, ore_random, settings.mesa,
                  [&](const OrePlacement &) { ++result.ore_attempts; });

    // ClayFeature needs a water/clay anchor and dirt below; the generated
    // volume rarely has that, so it is attempted at the surface column only.
    ClayEnvironment clay_environment;
    clay_environment.block = [&](DecorationRegion::Position p) {
        const auto b = region.block(p);
        return ClayBlock{sealed(b), b.data};
    };
    clay_environment.is_water = [&](DecorationRegion::Position p) {
        return std::strcmp(region.block(p).name, "water") == 0;
    };
    clay_environment.place_block = [&](DecorationRegion::Position p, ClayBlock b) {
        (void)region.set_block(p, TerrainBlock{b.name, b.data}, 3);
    };
    (void)place_clay(region, {ox + 8, 4, oz + 8}, 5, ore_random, clay_environment);

    // The small vegetation features, each needing a survival rule the host
    // derives from the same block identity the corpus recorded.
    VegetationEnvironment vegetation_environment;
    vegetation_environment.block = [&](DecorationRegion::Position p) {
        const auto b = region.block(p);
        return feature_block(b);
    };
    vegetation_environment.is_empty = [&](DecorationRegion::Position p) {
        return std::strcmp(region.block(p).name, "air") == 0;
    };
    vegetation_environment.can_survive = [&](DecorationRegion::Position p, FeatureBlock) {
        const auto below = region.block({std::get<0>(p), std::get<1>(p) - 1, std::get<2>(p)});
        return std::strcmp(below.name, "grass") == 0 || std::strcmp(below.name, "dirt") == 0;
    };
    vegetation_environment.may_place = vegetation_environment.can_survive;
    vegetation_environment.may_place_face = [](DecorationRegion::Position, FeatureBlock, int) {
        return false;
    };
    vegetation_environment.heightmap = [&](DecorationRegion::Position p) {
        for (int y = 255; y >= 0; --y)
            if (std::strcmp(region.block({std::get<0>(p), y, std::get<2>(p)}).name, "air") != 0)
                return y;
        return 0;
    };
    vegetation_environment.may_place_on = vegetation_environment.can_survive;
    vegetation_environment.material_is = [&](DecorationRegion::Position p, int type) {
        const auto name = region.block(p).name;
        if (type == 5)
            return std::strcmp(name, "water") == 0 || std::strcmp(name, "lava") == 0;
        return false;
    };
    vegetation_environment.has_property = [](DecorationRegion::Position, int) { return false; };
    vegetation_environment.place_block = [&](DecorationRegion::Position p, FeatureBlock b) {
        (void)region.set_block(p, TerrainBlock{b.name, b.data}, 3);
    };
    // Grass spread on the surface, then a flower and a reed attempt, matching
    // the feature set the vegetation corpus covers.
    for (int i = 0; i < 4; ++i) {
        const auto at = DecorationRegion::Position{ox + 2 + i * 4, 5, oz + 2 + i * 4};
        (void)place_tall_grass(region, at, feature_block(terrain_block::grass), ore_random,
                              vegetation_environment);
        (void)place_flower(region, at, feature_block(terrain_block::grass), ore_random,
                           vegetation_environment);
        (void)place_reed(region, at, FeatureBlock{"reeds", 0}, ore_random, vegetation_environment);
    }

    // TreeFeature::place 0x1b5a290 over the translated base class. The native
    // caller is OverworldDecorator::decorate's tree stage, which is not
    // translated (DECORATE.md), so the sites here are chosen from the surface
    // the generator produced: a grass cell with room above. Height 5 is the
    // value TreeFeature's own corpus uses; a real decorator derives it from the
    // biome's tree density and the feature's trunk-height scale.
    TreeEnvironment tree_environment;
    tree_environment.max_height = [] { return 256; };
    tree_environment.block = [&](DecorationRegion::Position p) {
        const auto b = region.block(p);
        return TreeBlock{b.name, b.data};
    };
    tree_environment.is_free = [](TreeBlock b) {
        return std::strcmp(b.name, "air") == 0 || std::strcmp(b.name, "leaves") == 0;
    };
    tree_environment.is_empty = [](DecorationRegion::Position) { return true; };
    tree_environment.may_place_on = [&](DecorationRegion::Position p, TreeBlock) {
        const auto below = region.block({std::get<0>(p), std::get<1>(p) - 1, std::get<2>(p)});
        return std::strcmp(below.name, "grass") == 0 || std::strcmp(below.name, "dirt") == 0;
    };
    tree_environment.place_block = [&](DecorationRegion::Position p, TreeBlock b) {
        return region.set_block(p, TerrainBlock{b.name, b.data}, 3);
    };
    tree_environment.material_is = [&](DecorationRegion::Position p, int type) {
        const auto name = region.block(p).name;
        if (type == 0)
            return std::strcmp(name, "air") == 0;
        if (type == 7)
            return std::strcmp(name, "leaves") == 0;
        if (type == 9)
            return std::strcmp(name, "grass") == 0;
        return false;
    };
    // Four tree attempts on the chunk's surface, at fixed offsets so the stage
    // stays deterministic and reproducible.
    for (int i = 0; i < 4; ++i) {
        const int tx = ox + 3 + i * 3, tz = oz + 4 + i * 2;
        int surface_y = 0;
        for (int y = 100; y >= 1; --y) {
            const auto b = region.block({tx, y, tz});
            if (std::strcmp(b.name, "grass") == 0 || std::strcmp(b.name, "dirt") == 0) {
                surface_y = y + 1;
                break;
            }
        }
        if (surface_y == 0)
            continue;
        if (bds::tree_place(region, {tx, surface_y, tz}, 5, ore_random, tree_environment,
                            /*manually_placed=*/false, /*type_flags=*/false,
                            TreeBlock{"log", 0}, TreeBlock{"leaves", 0}, TreeBlock{"dirt", 0}))
            ++result.trees_placed;
    }

    result.mutations = region.take_mutations();
    return result;
}
} // namespace bds
