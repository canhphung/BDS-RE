// KelpFeature, translated from BDS 1.6.0.15 (Linux).
//   0x1db2de0 KelpFeature::KelpFeature()
//   0x1b53de0 KelpFeature::place
//   0x1b6ebd0 Feature::_placeBlock
//
// Verified against tests/reference/kelp_bds_1.6.0.15.json.

#include "bds/world/marine.hpp"
#include "bds/world/vegetation.hpp" // next_gaussian_int 0x1b52610

namespace bds {

bool place_kelp(DecorationRegion &region, DecorationRegion::Position at, Random &random,
                const MarineEnvironment &environment, MarineBlock kelp) {
    (void)region;
    const int ox = std::get<0>(at), oz = std::get<2>(at);
    if (environment.is_client_side && environment.is_client_side())
        return true;

    // Descent 0x1b53e2d..0x1b53f35.
    int y = std::get<1>(at);
    while (true) {
        const bool solid = environment.is_solid_blocking({ox, y - 1, oz});
        (void)environment.block({ox, y - 1, oz});
        if (!solid) {
            (void)environment.block({ox, y, oz});
            --y;
            continue;
        }
        if (!environment.is_solid_blocking({ox, y - 1, oz}))
            return false;
        (void)environment.block({ox, y - 1, oz});
        break;
    }

    // Ten candidates 0x1b53f44..
    for (int attempt = 0; attempt < 0xa; ++attempt) {
        // 0x1b53f9b..0x1b53fe3: first gaussian is z, second is x, y pinned.
        const int gz = next_gaussian_int(random, 8);
        const int gx = next_gaussian_int(random, 8);
        const DecorationRegion::Position candidate{ox + gx, y, oz + gz};
        const int cx = std::get<0>(candidate), cz = std::get<2>(candidate);
        if (!environment.is_empty_water(candidate)) {
            (void)environment.block(candidate);
            (void)environment.block(candidate);
            continue;
        }
        // Accepted: two reads of the candidate, then the cell below.
        (void)environment.block(candidate);
        (void)environment.block(candidate);
        (void)environment.is_empty_water({cx, y - 1, cz});
        (void)environment.block({cx, y - 1, cz});
        (void)environment.block({cx, y, cz});
        // 0x1b54167..0x1b541f6: the first column pass starts at the candidate's
        // own Y and stops at the first non-water cell (no 16 cap: the corpus
        // walks to y=22 for a 6-block column).
        int column = 0;
        while (true) {
            const bool water = environment.is_empty_water({cx, y + column, cz});
            (void)environment.block({cx, y + column, cz});
            if (!water)
                break;
            ++column;
        }
        // Height, then the cell below the candidate.
        const int height = random.next_int(3, column + 2);
        (void)environment.block({cx, y - 1, cz});
        // First block: mayPlace, then the nine-read fan-out, then the write.
        if (!environment.may_place(candidate, kelp))
            continue;
        (void)environment.block({cx, y - 1, cz});
        (void)environment.block({cx, y, cz});
        (void)environment.block({cx, y + 1, cz});
        (void)environment.block({cx, y - 1, cz});
        (void)environment.block({cx, y, cz - 1});
        (void)environment.block({cx, y, cz + 1});
        (void)environment.block({cx + 1, y, cz});
        (void)environment.block({cx - 1, y, cz});
        (void)environment.block({cx, y, cz});
        environment.place_block(candidate, kelp);
        // 0x1b54330..: the second column pass starts one above the first and
        // stops at the first non-water cell, placing while it goes.
        for (int k = 1; k < height; ++k) {
            const DecorationRegion::Position p{cx, y + k, cz};
            const bool water = environment.is_empty_water(p);
            (void)environment.block(p);
            if (!water)
                break;
            environment.place_block(p, kelp);
        }
    }
    return true;
}
} // namespace bds
