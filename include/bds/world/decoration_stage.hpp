#pragma once
#include "bds/world/clay.hpp"
#include "bds/world/decoration.hpp"
#include "bds/world/overworld.hpp"
#include "bds/world/random.hpp"
#include "bds/world/vegetation.hpp"
#include <cstdint>
#include <vector>

namespace bds {
// Runtime decoration stage: the translated features that are verified and need
// no missing infrastructure, driven over a DecorationRegion built from a
// generated TerrainVolume.
//
// This is NOT OverworldGenerator::postProcess 0x1b38950. The native decorator
// dispatch is a table of calls into trees, plants, marine and snow features
// that are not all translated (DECORATE.md), so calling it is not possible.
// What this stage does instead is run the features that ARE verified, in an
// order documented against the native sources, and report every changed cell.
// The runtime uses the result to fill the chunk; a later wiring replaces this
// with the real dispatcher.
struct DecorationStageResult {
    // Cells the features changed, in the order they were written.
    std::vector<DecorationRegion::Mutation> mutations;
    std::size_t ore_attempts = 0;
};

struct DecorationStageSettings {
    std::uint32_t seed = 0;
    // TerrainBlock name for the biome's surface, used by features that look at
    // the block below (clay needs dirt, ores need stone).
    bool mesa = false;
};

// Build a DecorationRegion from a generated TerrainVolume, run the verified
// features, and return the changed cells. `volume` is the generator output for
// chunk (chunk_x, chunk_z); the region covers that column plus the eight
// neighbours the cross-column features can reach, left empty so a write outside
// the owning chunk is reported and discarded by the caller.
DecorationStageResult decorate_generated_chunk(const TerrainVolume &volume, int chunk_x,
                                               int chunk_z, const DecorationStageSettings &);
} // namespace bds
