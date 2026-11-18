#pragma once
#include "bds/world/decoration.hpp"
#include "bds/world/random.hpp"
#include <cstdint>
#include <cstring>
#include <functional>

namespace bds {
// KelpFeature translated from BDS 1.6.0.15 (Linux).
//   KelpFeature::KelpFeature()  0x1db2de0
//   KelpFeature::place          0x1b53de0
struct MarineBlock {
    const char *name = "air";
    std::uint16_t data = 0;
    bool operator==(const MarineBlock &o) const {
        return o.data == data && std::strcmp(o.name, name) == 0;
    }
};

struct MarineEnvironment {
    std::function<bool()> is_client_side;                              // 0x1c85750
    std::function<bool(DecorationRegion::Position)> is_empty_water;     // 0x1c60d00
    std::function<bool(DecorationRegion::Position)> is_solid_blocking;  // 0x1c5aea0
    std::function<MarineBlock(DecorationRegion::Position)> block;       // 0x1c58ad0
    std::function<bool(DecorationRegion::Position, MarineBlock)> may_place; // 0x1e6a5a0
    std::function<void(DecorationRegion::Position, MarineBlock)> place_block; // 0x1b6ebd0
};

bool place_kelp(DecorationRegion &, DecorationRegion::Position, Random &, const MarineEnvironment &,
                MarineBlock kelp = {"kelp", 0});
} // namespace bds
