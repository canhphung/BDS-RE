#pragma once
#include "bds/world/decoration.hpp"
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
    // Feature::_placeBlock 0x1b6ebd0.
    std::function<void(DecorationRegion::Position, TreeBlock)> place_block;
};

// TreeFeature::_prepareSpawn 0x1b59e30. `height` is the tree height the
// four-argument `place` receives. A false return aborts the whole placement.
bool tree_prepare_spawn(DecorationRegion &, DecorationRegion::Position, int height,
                        const TreeEnvironment &, TreeBlock dirt = {"dirt", 0});
} // namespace bds
