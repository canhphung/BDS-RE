#pragma once
// OverworldGenerator::postProcess 0x1b38950 (BDS 1.6.0.15, Linux) on a
// GenRegion holding the 3 x 3 chunks around the chunk. src/world/chunk_post_process.cpp.
#include "bds/world/gen_region.hpp"
#include <cstdint>

namespace bds {
struct PostProcessReport {
    const char *stage = ""; // the last translated stage that ran
};
// State the original keeps between chunks in shared objects: the plains
// decorator's flower and grass counts (+0x1e8, +0x1ec), which
// PlainsBiome::Decorator::decorate rewrites per chunk and every biome's
// decorate reads (OverworldDecorator::decorate 0x1da5615). The defaults are
// the decorator's constructed values (dumped from the original).
struct PostProcessState {
    int plains_flowers = 4, plains_grass = 10;
};
// The writes go to region.writes in the original's order.
PostProcessReport run_post_process(GenRegion &region, std::uint32_t seed, int chunk_x, int chunk_z,
                                   PostProcessState &state);
} // namespace bds
