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

bool tree_place_leaf(DecorationRegion &region, DecorationRegion::Position at,
                     const TreeEnvironment &environment, TreeBlock leaves) {
    (void)region;
    // 0x1b59d60: _getBlock, then getMaterial once, then three isType tests in
    // the native order 9, 0, 7. The first true answer places the leaf; all
    // false leaves the cell untouched. The corpus records which of the three
    // calls the original actually reached, so the short circuit is observable.
    (void)environment.block(at);
    // The corpus records the return value: false when no isType matched, so the
    // caller can tell "nothing to do" from "placed".
    // Short circuit exactly as the original: test 9, then 0, then 7, and place
    // on the first true answer. The corpus stores the reachable answers with -1
    // for calls the original never made, so the order is observable.
    // 0x1b59e1e..0x1b59e26: the return value of _placeLeaf is exactly what
    // _placeBlock returned, so placing leaves on leaves reports false even
    // though the attempt happened.
    for (const int type : {9, 0, 7}) {
        if (environment.material_is(at, type))
            return environment.place_block(at, leaves);
    }
    return false;
}

void tree_place_leaves(DecorationRegion &region, DecorationRegion::Position origin, int height,
                       Random &random, const TreeEnvironment &environment, TreeBlock leaves) {
    (void)region;
    const int ox = std::get<0>(origin), oy = std::get<1>(origin), oz = std::get<2>(origin);
    // 0x1b5a313..0x1b5a326 builds range_incl(origin.y - 3 + height,
    // origin.y + height): the band is rel-Y 0..3 from height-3 up to height.
    const int top = oy + height;
    for (int y = oy - 3 + height; y <= top; ++y) {
        const int rel = y - (oy - 3 + height);
        // Measured from the native leaf sets: the half-width is 2, 2, 1, 1 for
        // every height. In rel terms that is 2 - trunc(rel / 2).
        const int half = 2 - rel / 2;
        for (int x = ox - half; x <= ox + half; ++x) {
            for (int z = oz - half; z <= oz + half; ++z) {
                // The trunk column is never a leaf, EXCEPT on the top layer,
                // where the native set keeps (0,0). The corpus fixes both halves
                // of that: rel-Y 2,3,4 omit it and rel-Y h keeps it.
                // The trunk column is never a leaf except on the top layer,
                // where the native set keeps (0,0). The corpus fixes both: rel-Y
                // 2,3,4 omit it and rel-Y h keeps it.
                const bool corner = std::abs(x - ox) == half && std::abs(z - oz) == half;
                if (x == ox && z == oz) {
                    // (0,0) is never a corner for these heights, so it takes the
                    // non-corner path: the native code still calls _getBlock and
                    // then _placeLeaf for it, and _placeLeaf's material test is
                    // what rejects the log. Only the top layer keeps a leaf here.
                    if (y == top)
                        tree_place_leaf(region, {x, y, z}, environment, leaves);
                    else
                        (void)environment.block({x, y, z});
                    continue;
                }
                // 0x1b5a4cf / 0x1b5a4e3: a cell that is not a corner jumps
                // straight to 0x1b5a50f, which is the _placeLeaf call.
                if (!corner) {
                    tree_place_leaf(region, {x, y, z}, environment, leaves);
                    continue;
                }
                // 0x1b5a4e9..0x1b5a50f: corners draw nextInt(2). Zero skips
                // (0x1b5a4fa jumps to the loop increment); a nonzero draw skips
                // only the top layer, where y - (origin.y + height) is zero.
                const int draw = random.next_int(2);
                if (draw == 0 || y == top)
                    continue;
                tree_place_leaf(region, {x, y, z}, environment, leaves);
            }
        }
    }
}

int jungle_tree_height(Random &random) {
    // 0x1b65ddb..0x1b65de0: nextInt(7) then + 4.
    return random.next_int(0x7) + 4;
}

int mega_tree_height(Random &random) {
    // MegaTreeFeature::place 0x1b55c9b..0x1b55ca0: nextInt(3) then + 5. This is
    // the height `place` passes to the base, and it does NOT read this+0x24.
    return random.next_int(0x3) + 5;
}

int mega_tree_calc_height(Random &random, int base_height, int height_interval) {
    // MegaTreeFeature::_calcTreeHeigth 0x1b55ce0: nextInt(3) + this+0x24, and
    // when this+0x28 > 1 another nextInt(this+0x28) is added. Used by the
    // SUBCLASSES (MegaPine etc.), not by MegaTreeFeature::place.
    int height = random.next_int(0x3) + base_height;
    if (height_interval > 1)
        height += random.next_int(height_interval);
    return height;
}

bool mega_tree_place(DecorationRegion &region, DecorationRegion::Position origin,
                     Random &random, const TreeEnvironment &environment, bool manually_placed,
                     bool type_flags, TreeBlock trunk, TreeBlock leaves, TreeBlock dirt) {
    // 0x1b55c70: nextInt(3) + 5 for the height, then delegate to the base place
    // 0x1b5a290 with the constructor's type id 2 blocks.
    const int height = mega_tree_height(random);
    return tree_place(region, origin, height, random, environment, manually_placed, type_flags,
                      trunk, leaves, dirt);
}

bool jungle_tree_place(DecorationRegion &region, DecorationRegion::Position origin,
                       Random &random, const TreeEnvironment &environment, bool manually_placed,
                       bool type_flags, TreeBlock trunk, TreeBlock leaves, TreeBlock dirt) {
    // 0x1b65db0: draw the height from the feature's own Random, then delegate to
    // the base place 0x1b5a290. The constructor 0x1b65d60 sets leafType 3 and
    // trunkType 1, which the base class only uses for its block identity, so no
    // extra state is needed here.
    const int height = jungle_tree_height(random);
    return tree_place(region, origin, height, random, environment, manually_placed, type_flags,
                      trunk, leaves, dirt);
}

bool tree_place(DecorationRegion &region, DecorationRegion::Position origin, int height,
                Random &random, const TreeEnvironment &environment, bool manually_placed,
                bool type_flags, TreeBlock trunk, TreeBlock leaves, TreeBlock dirt) {
    // 0x1b5a2cc..0x1b5a2dd: _prepareSpawn first; false returns false.
    if (!tree_prepare_spawn(region, origin, height, environment, dirt))
        return false;
    // 0x1b5a2f9..0x1b5a30a: _placeTrunk next. A FALSE return stores 1 and
    // returns immediately, so a failed trunk placement makes the feature
    // SUCCEED early; only a TRUE return reaches the leaf pass.
    if (!tree_place_trunk(region, origin, height, random, environment, manually_placed,
                          type_flags, trunk, leaves))
        return true;
    // 0x1b5a30f onward: the leaf volume, whose band starts at
    // origin.y - 3 + height (0x1b5a313..0x1b5a319), then the jungle and cocoa
    // decoration stage.
    tree_place_leaves(region, origin, height, random, environment, leaves);
    // 0x1b5a834..0x1b5a884: the native tail places DIRT at origin.below() and
    // returns true. _prepareSpawn already wrote dirt there, so the corpus
    // records the same cell twice; that duplicate is deliberate and observable.
    const DecorationRegion::Position below{std::get<0>(origin), std::get<1>(origin) - 1,
                                           std::get<2>(origin)};
    environment.place_block(below, dirt);
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
