// ClayFeature, translated from BDS 1.6.0.15 (Linux).
//
//   ClayFeature::ClayFeature(int)  0x1b557f0
//   ClayFeature::place             0x1b55840
//   Feature::_placeBlock           0x1b6ebd0
//
// Verified against tests/reference/clay_bds_1.6.0.15.json.

#include "bds/world/clay.hpp"

namespace bds {
bool place_clay(DecorationRegion &region, DecorationRegion::Position at, int size, Random &random,
                const ClayEnvironment &environment, ClayBlock clay) {
    (void)region;
    // 0x1b5586e..0x1b558b7: the target must have WATER material, or already be
    // clay. Nothing else proceeds; in particular stone and sand return false.
    const bool water = environment.is_water(at);
    if (!water && std::strcmp(environment.block(at).name, clay.name) != 0)
        return false;
    // 0x1b558cf..0x1b558e6: radius = nextInt(size - 2) + 2. The argument is the
    // constructor's size read back from the feature object at +0x18.
    const int radius = random.next_int(size - 2) + 2;
    // 0x1b558e9: `1` is stored at -0x40 and later subtracted/added around the
    // origin's Y, i.e. the vertical half-extent is always 1.
    const int vertical = 1;
    const int ox = std::get<0>(at), oy = std::get<1>(at), oz = std::get<2>(at);

    // 0x1b558f4..0x1b5593e: the outer range spans origin.y - 1 .. origin.y + 1
    // (the iterator at -0x4c is the Y loop variable).
    for (int y = oy - vertical; y <= oy + vertical; ++y) {
        // 0x1b5594d..0x1b55a0c: the horizontal scan is a CIRCLE, not a square.
        // For each X in [ox - radius, ox + radius] it computes
        //   dx = x - ox, dz = z - oz  (with z from the inner iterator)
        // and skips the cell when dx*dx + dz*dz > radius*radius.
        for (int x = ox - radius; x <= ox + radius; ++x) {
            for (int z = oz - radius; z <= oz + radius; ++z) {
                const int dx = x - ox, dz = z - oz;
                if (dx * dx + dz * dz > radius * radius)
                    continue;
                const DecorationRegion::Position p{x, y, z};
                // 0x1b55ad8..0x1b55af1: only a DIRT cell becomes clay.
                if (std::strcmp(environment.block(p).name, "dirt") != 0)
                    continue;
                environment.place_block(p, clay);
            }
        }
    }
    return true;
}
} // namespace bds
