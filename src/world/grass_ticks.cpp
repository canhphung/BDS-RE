#include "bds/world/grass_ticks.hpp"

namespace bds {
void tick_grass(GrassTickSource &source, World::Position position, Random &random,
                bool client_side) {
    if (client_side) // Level::isClientSide, 0x1e716c7
        return;
    auto [x, y, z] = position;
    const World::Position above{x, y + 1, z};
    const auto brightness = source.brightness(above); // getRawBrightness(..., true, true)
    // GrassBlock::MIN_BRIGHTNESS = 4 at 0x3498560; the path check is
    // short-circuited when brightness is already below that minimum.
    if (brightness < 4 || source.block(above).kind == GrassTickBlockKind::GrassPath) {
        const auto material = source.material(above);
        // Float 0.2 (0x3e4ccccd) at 0x2828638, then nextInt(4),
        // 0x1e7176f..0x1e717b3.
        if (material.translucency < 0.2f && random.next_int(4) == 0)
            source.set_block(position, GrassTickBlockKind::Dirt, 3);
        return;
    }
    // Brightness::MAX = 15 at 0x3497680, subtract 6 (0x1e717e7).
    if (brightness < 9)
        return;
    // Draw order is Z, Y, X; Y is initially the current layer or one above.
    const int target_z = z + random.next_int(3) - 1;
    int target_y = y + random.next_int(2);
    const int target_x = x + random.next_int(3) - 1;
    World::Position target{target_x, target_y, target_z};
    auto block = source.block(target);
    for (int descended = 0;
         block.kind == GrassTickBlockKind::Air && target_y > 0 && descended < 5;
         ++descended) { // 0x1e71899..0x1e7193a
        --target_y;
        target = {target_x, target_y, target_z};
        block = source.block(target);
    }
    if (block.kind != GrassTickBlockKind::Dirt || source.dirt_variant(target) != 0)
        return;
    const World::Position target_above{target_x, target_y + 1, target_z};
    const auto cover = source.block(target_above);
    const auto material = source.material(target_above);
    const auto light = source.brightness(target_above);
    if (light >= 4 && !cover.solid && !material.liquid) // 0x1e71a63..0x1e71a8e
        source.set_block(target, GrassTickBlockKind::Grass, 3);
}
} // namespace bds
