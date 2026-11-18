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

    // Descent 0x1b53e2d..0x1b53f35: per step is_solid_blocking(below),
    // get_block(below); a false test descends and also reads the new anchor,
    // a true one repeats the pair once and stops.
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
        // 0x1b53f9b..0x1b53fe3: gz is the first gaussian, gx the second, y pinned.
        const int gz = next_gaussian_int(random, 8);
        const int gx = next_gaussian_int(random, 8);
        const DecorationRegion::Position candidate{ox + gx, y, oz + gz};
        const int cx = std::get<0>(candidate), cz = std::get<2>(candidate);
        if (!environment.is_empty_water(candidate)) {
            // Rejected candidate: two reads of the candidate.
            (void)environment.block(candidate);
            (void)environment.block(candidate);
            continue;
        }
        // Accepted: two reads of the candidate, then the cell below is tested
        // and both cells are read (#19..#24 for the first candidate).
        (void)environment.block(candidate);
        (void)environment.block(candidate);
        (void)environment.is_empty_water({cx, y - 1, cz});
        (void)environment.block({cx, y - 1, cz});
        (void)environment.block({cx, y, cz});
        // Walk: climb from the candidate's own Y, no cap.
        int iterations = 0;
        while (true) {
            const bool water = environment.is_empty_water({cx, y + iterations, cz});
            (void)environment.block({cx, y + iterations, cz});
            ++iterations;
            if (!water)
                break;
        }
        const int column = iterations + 1;
        const int height = random.next_int(3, column + 2);
        // 0x1b54208: one read of the cell below the candidate.
        (void)environment.block({cx, y - 1, cz});
        // 0x1b5424c..0x1b54330: a single mayPlace, the nine-read fan-out, then
        // the first write.
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
        // 0x1b54330..: each remaining block is a (is_empty_water, get_block)
        // pair followed by a write, climbing one cell at a time.
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
