#include "bds/world/decoration.hpp"
#include "bds/world/chunk.hpp"
#include "bds/game/mce_math.hpp"
#include <cmath>
#include <cstring>

namespace bds {
namespace {
std::size_t index(int x, int y, int z) { return (local_coordinate(x) * 16 + local_coordinate(z)) * 256 + y; }
bool ore_diggable(TerrainBlock b) {
    return std::strcmp(b.name, "stone") == 0 || std::strcmp(b.name, "netherrack") == 0;
}
} // namespace
void DecorationRegion::load_empty_chunk(int x, int z) {
    chunks_.try_emplace({x, z}, 16 * 16 * 256, terrain_block::air);
}
void DecorationRegion::load_chunk(int x, int z, const TerrainVolume &volume) {
    load_empty_chunk(x, z);
    auto &chunk = chunks_.at({x, z});
    for (int bx = 0; bx < 16; ++bx)
        for (int bz = 0; bz < 16; ++bz)
            for (int y = 0; y < 128; ++y)
                chunk[(bx * 16 + bz) * 256 + y] = volume.at(bx, y, bz);
}
TerrainBlock DecorationRegion::block(Position p) const {
    const auto [x, y, z] = p;
    if (y < 0 || y >= 256)
        return terrain_block::air;
    const auto chunk = chunks_.find({chunk_coordinate(x), chunk_coordinate(z)});
    return chunk == chunks_.end() ? terrain_block::air : chunk->second[index(x, y, z)];
}
bool DecorationRegion::set_block(Position p, TerrainBlock b, int flags) {
    const auto [x, y, z] = p;
    if (y < 0 || y >= 256)
        return false;
    const auto chunk = chunks_.find({chunk_coordinate(x), chunk_coordinate(z)});
    if (chunk == chunks_.end())
        return false;
    auto &cell = chunk->second[index(x, y, z)];
    if (cell == b)
        return false;
    cell = b;
    mutations_.push_back({p, b, flags});
    return true;
}
bool place_ore(DecorationRegion &region, DecorationRegion::Position at, TerrainBlock target, int size,
               Random &random) {
    if (size <= 0 || size > 64)
        throw std::invalid_argument("ore feature size outside translated range");
    const auto [x, y, z] = at;
    const float amount = static_cast<float>(size), inverse = 1.0f / amount;
    const float angle = random.next_float() * std::bit_cast<float>(0x40490fdbu);
    const float sin = game::mce_sin(angle), cos = game::mce_cos(angle);
    const float dx = sin * (amount / 8.0f), dz = cos * (amount / 8.0f);
    const float ax = static_cast<float>(x) + (8.0f + dx);
    const float ay = static_cast<float>(y) + (static_cast<float>(random.next_int(3)) - 2.0f);
    const float az = static_cast<float>(z) + (8.0f + dz);
    const float bx = static_cast<float>(x) + (8.0f - dx);
    const float by = static_cast<float>(y) + (static_cast<float>(random.next_int(3)) - 2.0f);
    const float bz = static_cast<float>(z) + (8.0f - dz);
    const float vx = bx - ax, vy = by - ay, vz = bz - az;
    const float step = std::bit_cast<float>(0x40490fdbu) * inverse;
    for (int i = 0; i < size; ++i) {
        const float n = static_cast<float>(i);
        const float cx = ax + (vx * n) * inverse, cy = ay + (vy * n) * inverse, cz = az + (vz * n) * inverse;
        const float jitter = random.next_float() * amount / 16.0f;
        const float radius = ((game::mce_sin(n * step) + 1.0f) * jitter + 1.0f) / 2.0f;
        const float radius2 = radius * radius;
        const int x0 = int(std::floor(cx - radius)), x1 = int(std::floor(cx + radius));
        const int y0 = int(std::floor(cy - radius)), y1 = int(std::floor(cy + radius));
        const int z0 = int(std::floor(cz - radius)), z1 = int(std::floor(cz + radius));
        const float ox = .5f - cx, oy = .5f - cy, oz = .5f - cz;
        // Native traversal is X/Z/Y; the order is observable in overlapping
        // placements and in BlockSource notifications.
        for (int xx = x0; xx <= x1; ++xx) {
            const float fx = static_cast<float>(xx) + ox, sx = fx * fx;
            if (!(radius2 > sx))
                continue;
            for (int zz = z0; zz <= z1; ++zz) {
                const float fz = static_cast<float>(zz) + oz, sz = fz * fz;
                if (!(radius2 > sx + sz))
                    continue;
                for (int yy = y0; yy <= y1; ++yy) {
                    const float fy = static_cast<float>(yy) + oy;
                    if (!(radius2 > (sx + sz) + fy * fy))
                        continue;
                    const DecorationRegion::Position pos{xx, yy, zz};
                    const auto old = region.block(pos);
                    if (ore_diggable(old) && !(old == target))
                        region.set_block(pos, target, 3);
                }
            }
        }
    }
    return true; // native returns true even if no block was replaced
}
void decorate_ores(DecorationRegion &region, DecorationRegion::Position origin, Random &random, bool mesa,
                   const OrePlacementObserver &observer) {
    // OverworldDecorator::decorateDepthSpan 0x1da6b50 and
    // decorateDepthAverage 0x1da6c90 draw Z first, Y next, then X.
    const auto [x, y, z] = origin;
    const auto place = [&](int dx, int dy, int dz, TerrainBlock target, int size) {
        const DecorationRegion::Position at{x + dx, y + dy, z + dz};
        if (observer)
            observer({at, target, size});
        place_ore(region, at, target, size, random);
    };
    const auto span = [&](TerrainBlock target, int size, int count, int low, int high) {
        for (int i = 0; i < count; ++i) {
            const int dz = random.next_int(16), dy = random.next_int(high - low) + low,
                      dx = random.next_int(16);
            place(dx, dy, dz, target, size);
        }
    };
    // Registry sizes/targets read from both Plains and Mesa decorator objects.
    // The order is diorite, granite, andesite, rather than member-slot order.
    span(terrain_block::dirt, 33, 10, 0, 128);
    span(terrain_block::gravel, 33, 8, 0, 128);
    if (random.next_int(16) == 0)
        span(terrain_block::gravel, 33, 80, 0, 50);
    span({"stone", 3}, 33, 10, 0, 80);
    span({"stone", 1}, 33, 10, 0, 80);
    span({"stone", 5}, 33, 10, 0, 80);
    span({"coal_ore"}, 17, 20, 0, 128);
    span({"iron_ore"}, 9, 20, 0, 64);
    span({"gold_ore"}, 9, 2, 0, 32);
    span({"redstone_ore"}, 8, 8, 0, 16);
    span({"diamond_ore"}, 8, 1, 0, 16);
    const int dz = random.next_int(16), a = random.next_int(16), b = random.next_int(16),
              dx = random.next_int(16);
    place(dx, a + b, dz, {"lapis_ore"}, 7); // center=spread=16
    if (mesa)
        span({"gold_ore"}, 9, 20, 32, 80); // Mesa::Decorator::decorateOres 0x1dc9ad0
}
} // namespace bds
