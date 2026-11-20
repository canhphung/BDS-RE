#pragma once
#include "bds/world/decoration.hpp"
#include "bds/world/overworld.hpp"
#include "bds/world/random.hpp"
#include <cstdint>
#include <functional>

namespace bds {
// The runtime-facing decoration stage: a bounded, honest subset of
// OverworldGenerator::postProcess 0x1b38950.
//
// The native postProcess is a 0xf19-byte function that locks the chunk, builds
// a BlockSource with a ticking queue, derives a decoration origin from the
// biome's ThreadData, reseeds twice, places lakes (with a Desert/DesertHills
// skip and separate water/lava gates), dispatches the biome decorator, then
// runs MonsterRoomFeature, BonusChestFeature and a pending-tick pass. See
// reverse/reports/POSTPROCESS.md for the full trace and DECORATE.md for why the
// decorator dispatch cannot be called yet.
//
// What this stage does NOT do, deliberately:
//   * no decorator dispatch (trees, plants, marine, snow/ice are not wired);
//   * no MonsterRoomFeature (needs the block-entity/spawner surface);
//   * no bonus chest, which belongs to the level's spawn chunk rather than to
//     every decorated column;
//   * no neighbour events, pending ticks or lighting jobs.
//
// It DOES reproduce the two seed derivations and the lake selection draws in
// the native order, so a later wiring of the missing features sees the same
// Random state. Every draw it makes is documented against its address.

struct PostProcessSettings {
    // Level::getSeed (LevelSettings::getSeed 0x1c69170). Both derivations and
    // the carvers read it.
    std::uint32_t level_seed = 0;
    // The biome at the chunk's maximum corner (postProcess 0x1b38ad8 ->
    // BlockSource::getBiome). Only the Desert/DesertHills test uses it.
    bool desert_like = false;
    // Generator +0x48: when set, the native code skips both lake stages
    // (0x1b38cca and 0x1b38d6f).
    bool superflat_or_no_lakes = false;
};

struct PostProcessResult {
    int water_lake_placed = 0; // 1 when the water lake returned true
    int ore_batches = 0;       // default ore decorator invocations
    std::vector<DecorationRegion::Mutation> mutations;
};

// OverworldGenerator::postProcess 0x1b38950, bounded as described above.
// `origin` is the decoration origin the native code derives from the biome's
// ThreadData +0x41810 pair; the caller supplies it because the ThreadData
// layout is not modelled.
PostProcessResult post_process_chunk(DecorationRegion &region, DecorationRegion::Position origin,
                                     Random &level_random, const PostProcessSettings &settings);

// The two seed derivations postProcess performs, exposed for a direct test:
//   0x1b38b5e and 0x1b38ea5.
// Each draws two nextInt() values from a Random seeded with the level seed and
// returns ((origin.x * a) + (origin.z * b)) ^ level_seed.
std::uint32_t post_process_seed_for(DecorationRegion::Position origin, std::uint32_t level_seed,
                                    Random &scratch);
} // namespace bds
