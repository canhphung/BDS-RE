#pragma once
// OverworldGenerator::postProcess 0x1b38950 (BDS 1.6.0.15, Linux) on a
// GenRegion holding the 3 x 3 chunks around the chunk. src/world/chunk_post_process.cpp.
#include "bds/world/gen_region.hpp"
#include <cstdint>

namespace bds {
struct PostProcessReport {
    const char *stage = ""; // the last translated stage that ran
};
// The writes go to region.writes in the original's order.
PostProcessReport run_post_process(GenRegion &region, std::uint32_t seed, int chunk_x, int chunk_z);
} // namespace bds
