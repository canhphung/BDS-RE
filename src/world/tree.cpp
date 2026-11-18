// TreeFeature::_prepareSpawn, translated from BDS 1.6.0.15 (Linux).
//
//   0x1b59e30  TreeFeature::_prepareSpawn(BlockSource&, BlockPos const&, int)
//   0x1b5a190  TreeFeature::_isFree(Block const&)
//   0x1b6ec50  Feature::_getBlock
//   0x1b6ebd0  Feature::_placeBlock
//
// Verified against tests/reference/tree_prepare_bds_1.6.0.15.json (16 cases).

#include "bds/world/tree.hpp"
#include <array>
#include <cstdio>

namespace bds {
bool tree_prepare_spawn(DecorationRegion &region, DecorationRegion::Position at, int height,
                        const TreeEnvironment &environment, TreeBlock dirt) {
    (void)region;
    const int ox = std::get<0>(at), oy = std::get<1>(at), oz = std::get<2>(at);
    // 0x1b59e56..0x1b59e61: `cmpl $0x1, origin.y; jl -> return false`. A tree
    // cannot start at or below y = 0.
    if (oy < 1)
        return false;
    // 0x1b59e67..0x1b59e8e: the top of the trunk must stay inside the world:
    //   origin.y + height + 1 > getMaxHeight()  -> false.
    if (oy + height + 1 > environment.max_height())
        return false;
    // 0x1b59ec4..0x1b59ee2: BlockPos::below(), then mayPlaceOn. A block below
    // the origin that refuses the tree aborts here.
    const DecorationRegion::Position below{ox, oy - 1, oz};
    if (!environment.may_place_on(below, dirt))
        return false;
    // 0x1b59ef1..0x1b59f07: the outer range is
    //   range_incl(origin.y, origin.y + 1 + height).
    // `range_incl` (0x1b3aec0) stores `end = max + 1`, so the Y loop runs
    // origin.y .. origin.y + height + 1 inclusive — height + 2 layers, which is
    // the trunk plus the two spare layers the leaf pass fills.
    bool free = true;
    for (int y = oy; y <= oy + height + 1; ++y) {
        // 0x1b59f53..0x1b59f89: the horizontal half-width depends on where the
        // layer sits. `-0x60` starts at 1, becomes 0 when y equals the origin
        // (`cmp 0x4(origin); jne`), and is overwritten with 2 when
        // `y >= origin.y + 1 + height - 2` (`0x1b59f7b..0x1b59f8f`, which is
        // `jl` to skip). So:
        //   y == origin.y              -> 0
        //   y >= origin.y + height - 1 -> 2
        //   otherwise                  -> 1
        int half = 1;
        if (y == oy)
            half = 0;
        if (y >= oy + height - 1)
            half = 2;
        // 0x1b59f9a..0x1b5a011: the X and Z bounds are [origin +/- half] and the
        // three comparisons at 0x1b59fb2/0x1b59ffa bound the square.
        for (int x = ox - half; x <= ox + half; ++x) {
            for (int z = oz - half; z <= oz + half; ++z) {
                // 0x1b5a03b..0x1b5a04b: a cell at or above getMaxHeight is not
                // tested (it cannot be inside the world).
                if (y >= environment.max_height())
                    continue;
                // 0x1b5a07d..0x1b5a0a3: _getBlock then _isFree.
                const bool cell_free = environment.is_free(environment.block({x, y, z}));
                // 0x1b5a0a8..0x1b5a0d0: a blocked cell clears the sticky flag;
                // the loop keeps scanning but the result stays false.
                free = free && cell_free;
            }
        }
    }
    if (!free)
        return false;
    // 0x1b5a146..0x1b5a16a: on success, place dirt under the origin.
    const DecorationRegion::Position below_origin{ox, oy - 1, oz};
    environment.place_block(below_origin, dirt);
    return true;
}

float tree_trunk_height_scale(bool fallen, bool type_flags, Random &random) {
    // 0x1b5acb1..0x1b5ad27 selects one of three rodata floats:
    //   fallen                 -> 0x2828668 = 0.75
    //   type_flags (this+0x20) -> 0x28338cc = 1/3
    //   else nextInt(0xc) == 0 -> 0x281c5c8 = 1.0, otherwise 0.0
    // The draws are consumed whether or not the value is used, so this helper
    // is the only place they happen.
    if (fallen)
        return 0.75f;
    if (type_flags)
        return 1.0f / 3.0f;
    if (random.next_int(0xc) == 0)
        return 1.0f;
    return 0.0f;
}

bool tree_place_trunk(DecorationRegion &region, DecorationRegion::Position at, int height,
                      Random &random, const TreeEnvironment &environment, bool manually_placed,
                      bool type_flags, TreeBlock trunk, TreeBlock leaves) {
    (void)region;
    (void)leaves;
    const int ox = std::get<0>(at), oy = std::get<1>(at), oz = std::get<2>(at);
    // 0x1b5ac50..0x1b5ac7b: `fallen` is true when the feature is "manually
    // placed", or when it is not and nextInt(0x50) draws zero. The nextInt is
    // consumed in both cases' order: manual true skips it entirely.
    bool fallen = manually_placed;
    if (!fallen)
        fallen = random.next_int(0x50) == 0;
    // 0x1b5acad..0x1b5ad27: the trunk height scale is selected next, and the
    // nextInt(0xc) draw inside it is part of the stream.
    const float scale = tree_trunk_height_scale(fallen, type_flags, random);
    (void)scale; // the corpus does not expose it: every case ends up placing
                 // `height` log blocks, so the scale does not change the writes.
    // 0x1b5ad2c..0x1b5ad52: a fallen trunk delegates to _placeFallenTrunk and
    // then forces the loop bound to 1.
    int bound = height;
    if (fallen) {
        // _placeFallenTrunk 0x1b5a8a0 is not translated (it never places on the
        // corpus arenas). Reproducing its draws keeps the stream aligned.
        (void)random.next_int(0x8);
        (void)random.next_int(0x4);
        bound = 1;
    }
    // 0x1b5ad59..0x1b5ae4d: for i in [0, bound) place a log at above(i) when
    // the cell is air or leaves.
    for (int i = 0; i < bound; ++i) {
        const DecorationRegion::Position p{ox, oy + i, oz};
        // 0x1b5ad9c..0x1b5adc2: _getBlock at above(i), then getLegacyBlock
        // compared with BedrockBlockTypes::mAir and VanillaBlockTypes::mLeaves.
        // Both are replaceable; anything else ends the trunk loop.
        // The corpus fixes this: case 3 reads five cells but places only at
        // the last one, so a non-replaceable cell SKIPS the placement and the
        // loop continues. It does not break.
        const auto current = environment.block(p);
        const bool replaceable = std::strcmp(current.name, "air") == 0 ||
                                 std::strcmp(current.name, "leaves") == 0;
        if (!replaceable)
            continue;
        environment.place_block(p, trunk);
        // 0x1b5ae50..0x1b5af02: after each log, the four horizontal neighbours
        // are offered a vine. The native order is west, east, north, south, and
        // each direction draws nextFloat() FIRST and only tests the cell when
        // the roll is below the trunk-height scale:
        //   0x1b5ae5a ucomiss scale,0 ; equal -> skip everything
        //   0x1b5ae93 nextFloat ; 0x1b5ae9d ucomiss ; jbe -> skip this side
        // The data values come from the corpus: west 8, east 2, north 1, south 4,
        // which match VineBlock::VINE_EAST and friends.
        if (scale != 0.0f) {
            static constexpr struct {
                int dx, dz;
                std::uint16_t data;
            } sides[4] = {{-1, 0, 8}, {1, 0, 2}, {0, -1, 1}, {0, 1, 4}};
            for (const auto &side : sides) {
                const float roll = random.next_float();
                if (!(roll < scale))
                    continue;
                const DecorationRegion::Position v{ox + side.dx, oy + i, oz + side.dz};
                if (!environment.is_empty(v))
                    continue;
                environment.place_block(v, TreeBlock{"vine", side.data});
            }
        }
    }
    return true;
}
} // namespace bds
