// TreeFeature::_prepareSpawn, translated from BDS 1.6.0.15 (Linux).
//
//   0x1b59e30  TreeFeature::_prepareSpawn(BlockSource&, BlockPos const&, int)
//   0x1b5a190  TreeFeature::_isFree(Block const&)
//   0x1b6ec50  Feature::_getBlock
//   0x1b6ebd0  Feature::_placeBlock
//
// Verified against tests/reference/tree_prepare_bds_1.6.0.15.json (16 cases).

#include "bds/world/tree.hpp"

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
} // namespace bds
