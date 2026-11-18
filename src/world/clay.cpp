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
    // 0x1b5586e..0x1b558b7 calls getBlock FIRST, then getMaterial/isType on that
    // block. The anchor must have WATER material or already be clay; anything
    // else returns false immediately, so stone and sand never reach the scan.
    const auto anchor = environment.block(at);
    const bool water = environment.is_water(at);
    if (!water && std::strcmp(anchor.name, clay.name) != 0)
        return false;
    // 0x1b558cf..0x1b558e6: radius = nextInt(size - 2) + 2. The argument is the
    // constructor's size read back from the feature object at +0x18.
    const int radius = random.next_int(size - 2) + 2;
    // 0x1b558e9: `1` is stored at -0x40 and later subtracted/added around the
    // origin's Y, i.e. the vertical half-extent is always 1.
    const int vertical = 1;
    const int ox = std::get<0>(at), oy = std::get<1>(at), oz = std::get<2>(at);

    // The native loop order is X outermost, then Y, then Z innermost — visible
    // in the corpus as (-10,5,-8), (-10,6,-8), (-10,7,-8), (-9,5,-9), ...
    //
    // 0x1b558f4..0x1b5593e builds the X range [ox - radius, ox + radius].
    for (int x = ox - radius; x <= ox + radius; ++x) {
        // 0x1b559ed..0x1b55a0c computes dx = x - ox and the circle test against
        // radius*radius, so an X outside the circle short-circuits the whole
        // column.
        const int dx = x - ox;
        // 0x1b55a17..0x1b55a2f builds the Z range [oz - radius, oz + radius] and
        // 0x1b55a3e..0x1b55a50 the Y range [oy - 1, oy + 1]. The corpus fixes the
        // nesting: for one X the sequence is (-9,5,-9), (-9,6,-9), (-9,7,-9),
        // (-9,5,-8), ... so Z is the middle loop and Y the innermost.
        for (int z = oz - radius; z <= oz + radius; ++z) {
            for (int y = oy - vertical; y <= oy + vertical; ++y) {
                const int dz = z - oz;
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
