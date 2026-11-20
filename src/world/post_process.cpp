// postProcess runtime slice, translated from BDS 1.6.0.15 (Linux).
//
//   0x1b38950  OverworldGenerator::postProcess(ChunkViewSource&)
//   0x1b38b5e  first seed derivation (two nextInt() then the origin mix)
//   0x1b38c54  water-lake selection draws
//   0x1b38d5b  lava-lake selection draws
//   0x1b38ea5  second seed derivation
//   0x1b38f60  decorator dispatch (not wired)
//
// The full trace is in reverse/reports/POSTPROCESS.md. This file reproduces
// only the draws and the lake stages; the decorator dispatch and the
// MonsterRoom/BonusChest tail need components the project does not have yet.

#include "bds/world/post_process.hpp"

namespace bds {
namespace {
// 0x1b38b5e and 0x1b38ea5 are the same code: seed a scratch Random with the
// level seed, draw two nextInt() values, then reseed with
//   ((origin.x * a) + (origin.z * b)) ^ level_seed
// where a and b are made odd via `nextInt() / 2 * 2 + 1`.
std::uint32_t derive(DecorationRegion::Position origin, std::uint32_t level_seed,
                     Random &scratch) {
    scratch.set_seed(level_seed);
    const std::int32_t a = scratch.next_int() / 2 * 2 + 1;
    const std::int32_t b = scratch.next_int() / 2 * 2 + 1;
    // 0x1b38bb9..0x1b38bf4 multiplies in 32-bit and xors the seed as an
    // unsigned value (the same arithmetic Carver::apply uses at
    // src/world/caves.cpp). Signed overflow would be undefined here.
    const auto ox = static_cast<std::uint32_t>(std::get<0>(origin));
    const auto oz = static_cast<std::uint32_t>(std::get<2>(origin));
    return (ox * static_cast<std::uint32_t>(a) + oz * static_cast<std::uint32_t>(b)) ^ level_seed;
}

// The water lake needs a liquid and a sky-brightness reader; the runtime slice
// has no lighting model here, so the lake stages are gated off unless the
// caller supplies them. Keeping the draws is what matters: a later wiring must
// see the same stream.
} // namespace

std::uint32_t post_process_seed_for(DecorationRegion::Position origin, std::uint32_t level_seed,
                                    Random &scratch) {
    return derive(origin, level_seed, scratch);
}

PostProcessResult post_process_chunk(DecorationRegion &region, DecorationRegion::Position origin,
                                     Random &level_random, const PostProcessSettings &settings) {
    PostProcessResult result;
    // The native postProcess reseeds a PRIVATE ThreadData Random, not the level
    // Random, and every draw below comes from that copy. Mirroring it keeps the
    // level stream untouched, which is what the level-Random acceptance trace
    // in tests/reference/world_ticks_bds_1.6.0.15.json depends on.
    Random scratch(settings.level_seed);
    (void)derive(origin, settings.level_seed, scratch);

    // 0x1b38c04..0x1b38c49: Desert and DesertHills skip the water lake.
    // 0x1b38c54..0x1b38d51: water lake.
    if (!settings.desert_like && !settings.superflat_or_no_lakes) {
        // 0x1b38c60: nextInt(4) with a nonzero result skipping the stage.
        if (scratch.next_int(4) == 0) {
            // 0x1b38c7f..0x1b38cb2: three coordinate draws.
            const std::int32_t dz = scratch.next_int(3, 12);
            const std::int32_t dy = scratch.next_int(128);
            const std::int32_t dx = scratch.next_int(3, 12);
            (void)dz;
            (void)dx;
            // 0x1b38cbd..0x1b38cd5: a Y below 0x3c also needs generator +0x48
            // clear; both conditions are already handled by the flags above.
            // The LakeFeature call itself is not made: it needs a material and
            // sky-brightness reader this slice does not have. The draws are what
            // keep the stream aligned.
            if (dy >= 0x3c)
                result.water_lake_placed = 1;
        }
        // 0x1b38d5b..0x1b38e9b: lava lake, only when the water lake did not
        // place and generator +0x48 is clear.
        if (!result.water_lake_placed && !settings.superflat_or_no_lakes) {
            if (scratch.next_int(8) == 0) {
                (void)scratch.next_int(3, 12);        // z
                const std::int32_t w = scratch.next_int(0x78) + 8;
                (void)scratch.next_int(w);            // x, bounded by the height
                (void)scratch.next_int(3, 12);        // width
                // 0x1b38e04..0x1b38e2b: below 0x3c skip; at or above 0x40 the
                // stage also needs nextInt(10) == 0.
                if (w >= 0x3c && (w < 0x40 || scratch.next_int(10) == 0))
                    result.mutations = region.take_mutations();
            }
        }
    }

    // 0x1b38ea5: the same derivation again, from freshly drawn values.
    (void)derive(origin, settings.level_seed, scratch);

    // 0x1b38f60 onward is the biome decorator dispatch, MonsterRoomFeature and
    // BonusChestFeature. None is wired: the decorator object graph is not
    // modelled (DECORATE.md) and the other two need components the project does
    // not have. The ore decorator is the one piece that is fully translated, so
    // it is exposed separately rather than silently called here.
    result.mutations = region.take_mutations();
    return result;
}
} // namespace bds
