// Small surface features, translated from BDS 1.6.0.15 (Linux).
//
//   Random::nextGaussianInt            0x1b52610
//   TallGrassFeature::place            0x1b66570 / 0x1b663c0
//   FlowerFeature::place               0x1b5ef70 -> _placeMushroom 0x1b5efc0
//   PumpkinFeature::place              0x1b52430
//   CactusFeature::place               0x1b5d3b0
//   WaterlilyFeature::place            0x1b53000
//   ReedsFeature::place                0x1b5d070
//   VinesFeature::place                0x1b531a0
//   Feature::_placeBlock               0x1b6ebd0
//
// Verified against tests/reference/vegetation_bds_1.6.0.15.json (70 native cases).
//
// BlockPos::offset(int, int, int) takes x in edx, y in ecx and z in r8d. Every
// feature reaches it through that ABI, so the Random draws below are written in
// native register order rather than X/Y/Z order; each call site is cited.

#include "bds/world/vegetation.hpp"

namespace bds {
namespace {
// The native loop draws one gaussian per axis into a slot and then hands the
// slots to BlockPos::offset(x, y, z). This struct keeps the native meaning.
struct Offset {
    int x, y, z;
};
DecorationRegion::Position offset_by(DecorationRegion::Position at, const Offset &o) {
    return {std::get<0>(at) + o.x, std::get<1>(at) + o.y, std::get<2>(at) + o.z};
}
bool is_air(const FeatureBlock &block) { return std::strcmp(block.name, "air") == 0; }
} // namespace

int next_gaussian_int(Random &random, int n) {
    // 0x1b5262a nextInt(n), then 0x1b5263f nextInt(n) subtracted from it.
    const int first = random.next_int(n);
    return first - random.next_int(n);
}

bool place_tall_grass(DecorationRegion &region, DecorationRegion::Position at, FeatureBlock grass,
                      Random &random, const VegetationEnvironment &environment) {
    return place_tall_grass(region, at, grass, random, environment, 0x5a, 0x8);
}

bool place_tall_grass(DecorationRegion &region, DecorationRegion::Position at, FeatureBlock grass,
                      Random &random, const VegetationEnvironment &environment, int count, int spread) {
    (void)region;
    // 0x1b663c0 copies the BlockPos (0x1b663f3) into -0x38 and descends that
    // copy while the cell is air or has BlockProperty 0x20 and its Y is above
    // zero (0x1b66409..0x1b66470). That walk is unobservable here: the only
    // use of the descended Y is the loop's own `cmpl $0x0,-0x34(%rbp)` guard
    // (0x1b66450), while placement calls BlockPos::offset on the ORIGINAL
    // argument at -0x18 (0x1b664be). So it neither draws from the Random nor
    // changes any placement, and the native corpus confirms it: the only
    // predicates recorded for tall grass are 90 can_survive calls.
    // 0x1b66570 passes r8 = 0x5a (90 loop iterations) and r9 = 0x8 (spread).
    for (int i = 0; i < count; ++i) {
        // 0x1b6648f..0x1b664c8: gaussian(spread) -> Z (r8d), gaussian(4) -> Y
        // (ecx), gaussian(spread) -> X (edx).
        const int dz = next_gaussian_int(random, spread);
        const int dy = next_gaussian_int(random, 4);
        const int dx = next_gaussian_int(random, spread);
        const auto p = offset_by(at, Offset{dx, dy, dz});
        // 0x1b664d5..0x1b664f8: getBlock, then legacy identity against
        // BedrockBlockTypes::mAir. This is not BlockSource::isEmptyBlock.
        if (!is_air(environment.block(p)))
            continue;
        if (!environment.can_survive(p, grass))
            continue;
        environment.place_block(p, grass);
    }
    return true;
}

bool place_flower(DecorationRegion &region, DecorationRegion::Position at, FeatureBlock flower,
                  Random &random, const VegetationEnvironment &environment) {
    (void)region;
    // FlowerFeature::_placeMushroom 0x1b5efc0: range(0x40) = 64 iterations at
    // 0x1b5eff1. Draws gaussian(8) -> Z, gaussian(4) -> Y, gaussian(8) -> X at
    // 0x1b5f049..0x1b5f085 and calls BlockPos::offset(x, y, z).
    for (int i = 0; i < 0x40; ++i) {
        const int dz = next_gaussian_int(random, 8);
        const int dy = next_gaussian_int(random, 4);
        const int dx = next_gaussian_int(random, 8);
        const auto p = offset_by(at, Offset{dx, dy, dz});
        // 0x1b5f092: the target must be empty.
        if (!environment.is_empty(p))
            continue;
        // 0x1b5f0a8..0x1b5f0e5: the block below must differ from
        // BedrockBlocks::mAir (Block::operator!=).
        const DecorationRegion::Position below{std::get<0>(p), std::get<1>(p) - 1, std::get<2>(p)};
        if (is_air(environment.block(below)))
            continue;
        // 0x1b5f0f4..0x1b5f148: podzol or mycelium below short-circuits.
        const auto soil = environment.block(below);
        const bool special_soil =
            std::strcmp(soil.name, "podzol") == 0 || std::strcmp(soil.name, "mycelium") == 0;
        if (!special_soil) {
            // 0x1b5f152..0x1b5f169: getHeightmap(p) must be above p.y.
            if (!(environment.heightmap(p) > std::get<1>(p)))
                continue;
            // 0x1b5f18f..0x1b5f1d7: the block below must accept the flower.
            if (!environment.may_place_on(below, flower))
                continue;
        }
        environment.place_block(p, flower);
    }
    return true;
}

bool place_pumpkin(DecorationRegion &region, DecorationRegion::Position at, FeatureBlock pumpkin,
                   Random &random, const VegetationEnvironment &environment, FeatureBlock grass) {
    (void)region;
    // PumpkinFeature::place 0x1b52430: range(0x40) = 64 at 0x1b5243b.
    //
    // 0x1b524a6..0x1b524ee draws, in this order: gaussian(8) into -0x48,
    // gaussian(8) into -0x4c, gaussian(4) into -0x50. BlockPos::offset then
    // receives edx = -0x48 (x), ecx = -0x50 (y) and r8d = -0x4c (z). So the
    // TWO eight-spread draws are X and Z and the four-spread draw is Y — the
    // opposite of the naive X/Y/Z reading, and different from every other
    // feature here. The corpus confirms it: the first candidate of the first
    // case is at anchor + (3, 1, 0).
    for (int i = 0; i < 0x40; ++i) {
        const int dx = next_gaussian_int(random, 8);
        const int dz = next_gaussian_int(random, 8);
        const int dy = next_gaussian_int(random, 4);
        const auto p = offset_by(at, Offset{dx, dy, dz});
        if (!environment.is_empty(p))
            continue;
        // 0x1b5250d..0x1b52551: the block below must be grass.
        const DecorationRegion::Position below{std::get<0>(p), std::get<1>(p) - 1, std::get<2>(p)};
        if (std::strcmp(environment.block(below).name, grass.name) != 0)
            continue;
        // 0x1b5255c..0x1b52575: the pumpkin's own mayPlace.
        if (!environment.may_place(p, pumpkin))
            continue;
        // 0x1b52580..0x1b525b2: nextInt(4) selects the facing state.
        const auto facing = static_cast<std::uint16_t>(random.next_int(4));
        environment.place_block(p, FeatureBlock{pumpkin.name, facing});
    }
    return true;
}

bool place_cactus(DecorationRegion &region, DecorationRegion::Position at, FeatureBlock cactus,
                  Random &random, const VegetationEnvironment &environment) {
    (void)region;
    // CactusFeature::place 0x1b5d3b0: 10 attempts (0x1b5d3da, bound 0xa).
    //
    // 0x1b5d3e4..0x1b5d414 draws gaussian(8) into -0x28, gaussian(4) into
    // -0x2c and gaussian(8) into -0x30; BlockPos::offset then receives
    // edx = -0x30 (x), ecx = -0x2c (y) and r8d = -0x28 (z). So the FIRST draw
    // is Z, the second is Y and the third is X, which is the same
    // Z/Y/X reading as the other features (unlike PumpkinFeature, whose
    // eight-spread draws are X and Z).
    for (int i = 0; i < 10; ++i) {
        const int dz = next_gaussian_int(random, 8);
        const int dy = next_gaussian_int(random, 4);
        const int dx = next_gaussian_int(random, 8);
        const auto p = offset_by(at, Offset{dx, dy, dz});
        if (!environment.is_empty(p))
            continue;
        // 0x1b5d448..0x1b5d468: nextInt(3)+1, then nextInt(width)+1. These are
        // only drawn once the candidate cell is empty, which is why a case
        // consumes between 70 and 80 words rather than a constant 80.
        const int width = random.next_int(3) + 1;
        const int tall = random.next_int(width) + 1;
        for (int k = 0; k < tall; ++k) {
            // 0x1b5d4a2..0x1b5d4b8: canSurvive at above(k); a failure breaks the
            // column but not the attempt loop.
            const DecorationRegion::Position q{std::get<0>(p), std::get<1>(p) + k, std::get<2>(p)};
            // The original's Block::canSurvive for cactus internally queries the
            // four horizontal neighbours' materials (0x1c5acf0 via 0x1c56be0).
            // Those answers are recorded after the canSurvive row, so the host
            // environment exposes them as a nested stream the test replays; the
            // feature itself only consumes the boolean.
            // 0x1b5d4ba..0x1b5d4c0: a false canSurvive skips only this
            // block's placement. It does NOT end the column loop: the k
            // counter still advances (0x1b5d522..0x1b5d52b), which is why the
            // corpus shows consecutive can_survive rows without an is_empty
            // between them.
            if (environment.can_survive(q, cactus))
                environment.place_block(q, cactus);
        }
    }
    return true;
}

bool place_waterlily(DecorationRegion &region, DecorationRegion::Position at, FeatureBlock lily,
                     Random &random, const VegetationEnvironment &environment) {
    (void)region;
    // WaterlilyFeature::place 0x1b53000: range(0xa) = 10 at 0x1b53008, draws
    // gaussian(8) -> Z, gaussian(4) -> Y, gaussian(8) -> X at
    // 0x1b5307f..0x1b530bb. Every case consumes exactly 60 words.
    for (int i = 0; i < 10; ++i) {
        const int dz = next_gaussian_int(random, 8);
        const int dy = next_gaussian_int(random, 4);
        const int dx = next_gaussian_int(random, 8);
        const auto p = offset_by(at, Offset{dx, dy, dz});
        if (!environment.is_empty(p))
            continue;
        // 0x1b530de..0x1b530f3: the lily's own mayPlace.
        if (!environment.may_place(p, lily))
            continue;
        environment.place_block(p, lily);
    }
    return true;
}

bool place_reed(DecorationRegion &region, DecorationRegion::Position at, FeatureBlock reed,
                Random &random, const VegetationEnvironment &environment) {
    (void)region;
    // ReedsFeature::place 0x1b5d070: 20 attempts (0x1b5d09d, bound 0x14).
    // 0x1b5d0a7..0x1b5d0da: gaussian(8) -> -0x28, gaussian(8) -> -0x2c, and
    // BlockPos::offset takes edx = -0x28 (x = the FIRST gaussian), ecx = 0
    // (y pinned) and r8d = -0x2c (z = the second). So the first draw is X and
    // the second is Z, and Y is always zero; each attempt costs two gaussians.
    const int x = std::get<0>(at), y = std::get<1>(at), z = std::get<2>(at);
    for (int i = 0; i < 20; ++i) {
        const int dx = next_gaussian_int(random, 8);
        const int dz = next_gaussian_int(random, 8);
        const auto p = DecorationRegion::Position{x + dx, y, z + dz};
        if (!environment.is_empty(p))
            continue;
        // 0x1b5d0f9..0x1b5d1eb: BlockPos::below(), then west/east/north/south of
        // THAT cell are sampled with BlockSource::getMaterial and the attempt is
        // rejected when Material::isType(5) (water) holds. There is no air/block
        // test here at all; an earlier version invented one and the corpus
        // caught it. Exactly four material answers are recorded per attempt.
        const DecorationRegion::Position below{std::get<0>(p), std::get<1>(p) - 1, std::get<2>(p)};
        const int bx = std::get<0>(below), by = std::get<1>(below), bz = std::get<2>(below);
        const bool wet = environment.material_is({bx - 1, by, bz}, 5) ||
                         environment.material_is({bx + 1, by, bz}, 5) ||
                         environment.material_is({bx, by, bz - 1}, 5) ||
                         environment.material_is({bx, by, bz + 1}, 5);
        // 0x1b5d1f8..0x1b5d200: `test $1,%al; jne 0x1b5d205; jmp 0x1b5d32b`.
        // The 0x1b5d205 continuation (the width draws and the column walk) is
        // taken when the test SUCCEEDS, and the loop skips when it fails. The
        // tests above are Material::isType(5) (water), so the native code
        // continues only for a water neighbour and skips otherwise. That
        // inverted condition is why the corpus records 20 attempts, 80 words and
        // zero placements on dry ground.
        if (!wet)
            continue;
        // 0x1b5d205..0x1b5d326: a height of nextInt(nextInt(3) + 1) + 2, then a
        // reed at each level whose Block::canSurvive 0x1e69f10 holds (a failed
        // level is skipped, not the end of the column).
        const int width = random.next_int(3) + 1;
        const int height = random.next_int(width) + 2;
        for (int k = 0; k < height; ++k) {
            const DecorationRegion::Position q{std::get<0>(p), std::get<1>(p) + k, std::get<2>(p)};
            if (environment.can_survive(q, reed))
                environment.place_block(q, reed);
        }
    }
    return true;
}

bool place_vine(DecorationRegion &region, DecorationRegion::Position at, FeatureBlock vine,
                Random &random, const VegetationEnvironment &environment, int max_height) {
    (void)region;
    (void)random; // VinesFeature draws no Random value (corpus: 0 words).
    // VinesFeature::place 0x1b531a0 copies the BlockPos (0x1b531cb) and walks it
    // upward while its Y is below BlockSource::getMaxHeight() (0x1b531d0..e7).
    // For every Y it requires an empty cell and then tries faces 2..5.
    int x = std::get<0>(at), y = std::get<1>(at), z = std::get<2>(at);
    while (y < max_height) {
        // 0x1b531f5: the cell must be empty, else the walk stops.
        if (!environment.is_empty({x, y, z}))
            break;
        // 0x1b53207..0x1b53212: face starts at 2 and the loop ends at 5.
        for (int face = 2; face <= 5; ++face) {
            // 0x1b5322e: Block::mayPlace(this, BlockSource&, BlockPos, signed char face)
            // 0x1e6a600, a different overload from the 2-argument mayPlace.
            if (!environment.may_place_face({x, y, z}, vine, face))
                continue;
            // 0x1b53240..0x1b5327d: bit = 1 << FACING_DIRECTION[OPPOSITE_FACING[face]]
            // with the real rodata tables
            //   Facing::OPPOSITE_FACING     (0x2830c98) = {1,0,3,2,5,4,6,0}
            //   Direction::FACING_DIRECTION (0x2849d34) = {255,255,2,0,1,3,2,3}
            static constexpr signed char opposite_facing[8] = {1, 0, 3, 2, 5, 4, 6, 0};
            static constexpr signed char facing_direction[8] = {-1, -1, 2, 0, 1, 3, 2, 3};
            const int direction = facing_direction[opposite_facing[face]];
            const auto bits = static_cast<std::uint16_t>(1U << direction);
            environment.place_block({x, y, z}, FeatureBlock{vine.name, bits});
        }
        // 0x1b532b5..0x1b5330c: advance the copied BlockPos upward.
        ++y;
    }
    return true;
}
} // namespace bds
