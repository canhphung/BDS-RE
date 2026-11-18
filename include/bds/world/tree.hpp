#pragma once
#include "bds/world/decoration.hpp"
#include "bds/world/random.hpp"
#include "bds/world/overworld.hpp"
#include <cstdint>
#include <cstring>
#include <functional>

namespace bds {
// TreeFeature components translated from BDS 1.6.0.15 (Linux).
//
//   TreeFeature::TreeFeature(Actor*, int leafType, int trunkType, bool)
//                                          0x1b59cf0
//   TreeFeature::_prepareSpawn             0x1b59e30
//   TreeFeature::_isFree(Block const&)     0x1b5a190
//   Feature::_getBlock                     0x1b6ec50
//   Feature::_placeBlock                   0x1b6ebd0
//
// `_prepareSpawn` is verified against
// tests/reference/tree_prepare_bds_1.6.0.15.json (16 native cases). The rest of
// the family is not translated yet: calling `_placeTrunk` aborts inside the
// original when invoked from a paused world. See reverse/reports/TREES.md.

struct TreeBlock {
    const char *name = "air";
    std::uint16_t data = 0;
    bool operator==(const TreeBlock &other) const {
        return data == other.data && std::strcmp(name, other.name) == 0;
    }
};

struct TreeEnvironment {
    // BlockSource::getMaxHeight 0x1b50550.
    std::function<int()> max_height;
    // Block::mayPlaceOn(BlockSource&, BlockPos const&) 0x1e6a540.
    std::function<bool(DecorationRegion::Position, TreeBlock)> may_place_on;
    // Feature::_getBlock 0x1b6ec50 == BlockSource::getBlock 0x1c58ad0.
    std::function<TreeBlock(DecorationRegion::Position)> block;
    // TreeFeature::_isFree(Block const&) 0x1b5a190.
    std::function<bool(TreeBlock)> is_free;
    // Feature::_placeBlock 0x1b6ebd0. Returns the native bool: false when the
    // write did not change the cell (for example placing leaves on leaves).
    std::function<bool(DecorationRegion::Position, TreeBlock)> place_block;
    // Feature::_isEmptyBlock 0x1b6ece0, used by the vine pass.
    std::function<bool(DecorationRegion::Position)> is_empty;
    // Block::getMaterial 0x1e68cf0 through Material::isType 0x1c56be0, as
    // _placeLeaf 0x1b59d60 asks it: the feature calls isType(9), isType(0) and
    // isType(7) in that order and stops at the first true answer.
    std::function<bool(DecorationRegion::Position, int material_type)> material_is;
};

// TreeFeature::_prepareSpawn 0x1b59e30. `height` is the tree height the
// four-argument `place` receives. A false return aborts the whole placement.
bool tree_prepare_spawn(DecorationRegion &, DecorationRegion::Position, int height,
                        const TreeEnvironment &, TreeBlock dirt = {"dirt", 0});

// TreeFeature::_placeTrunk 0x1b5ac20. `manually_placed` is Feature::+0x8
// (0x1b6eb90); `type_flags` is the TreeFeature constructor's bool at +0x20.
// `trunk` / `leaves` are the blocks the mapped-type lookups produce.
bool tree_place_trunk(DecorationRegion &, DecorationRegion::Position, int height, Random &,
                      const TreeEnvironment &, bool manually_placed, bool type_flags,
                      TreeBlock trunk = {"log", 0}, TreeBlock leaves = {"leaves", 0});

// The leaf-volume pass of place(...,int) 0x1b5a313..0x1b5a55d. `height` is the
// same value the trunk stage received. Verified against
// tests/reference/tree_place_bds_1.6.0.15.json.
void tree_place_leaves(DecorationRegion &, DecorationRegion::Position origin, int height,
                       Random &, const TreeEnvironment &, TreeBlock leaves = {"leaves", 0});

// TreeFeature::_placeLeaf 0x1b59d60. Places leaves at `at` when the cell's
// material is type 9, 0 or 7 (the native order); otherwise it does nothing.
bool tree_place_leaf(DecorationRegion &, DecorationRegion::Position at,
                     const TreeEnvironment &, TreeBlock leaves = {"leaves", 0});

// The three trunk-height scale constants the native code selects between, from
// rodata 0x2828668 (0.75), 0x28338cc (1/3) and 0x281c5c8 (1.0).
float tree_trunk_height_scale(bool fallen, bool type_flags, Random &);
} // namespace bds
