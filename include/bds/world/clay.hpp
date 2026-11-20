#pragma once
#include "bds/world/decoration.hpp"
#include "bds/world/overworld.hpp"
#include <cstdint>
#include <cstring>
#include <functional>

namespace bds {
// ClayFeature translated from BDS 1.6.0.15 (Linux).
//
//   ClayFeature::ClayFeature(int)  0x1b557f0
//   ClayFeature::place             0x1b55840
//   Feature::_placeBlock           0x1b6ebd0
//
// Verified against tests/reference/clay_bds_1.6.0.15.json.

struct ClayBlock {
    const char *name = "air";
    std::uint16_t data = 0;
    bool operator==(const ClayBlock &other) const {
        return data == other.data && std::strcmp(name, other.name) == 0;
    }
};

struct ClayEnvironment {
    // BlockSource::getBlock 0x1c58ad0.
    std::function<ClayBlock(DecorationRegion::Position)> block;
    // Block::getMaterial 0x1e68cf0 through Material::isType 0x1c56be0: the
    // feature only ever asks whether the material is water (MaterialType 5).
    std::function<bool(DecorationRegion::Position)> is_water;
    // Feature::_placeBlock 0x1b6ebd0.
    std::function<void(DecorationRegion::Position, ClayBlock)> place_block;
};

// ClayFeature::place 0x1b55840. `size` is the constructor's argument and is
// also held in the feature object at +0x18.
bool place_clay(DecorationRegion &, DecorationRegion::Position, int size, Random &,
                const ClayEnvironment &, ClayBlock clay = {"clay", 0});
} // namespace bds
