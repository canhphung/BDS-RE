#include "bds/world/world.hpp"
#include <algorithm>
#include <cmath>
namespace bds {
namespace {
std::uint64_t mix(std::uint64_t n) {
    n += 0x9e3779b97f4a7c15ULL;
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
int divide(int n) { return n / 32 - (n % 32 < 0); }
} // namespace
int World::surface(int x, int z) const {
    if (terrain_.kind == 1)
        return 3;
    if (!terrain_.kind) {
        for (int y = 255; y >= 0; --y)
            if (block({x, y, z}))
                return y;
        return 63;
    }
    const int gx = divide(x), gz = divide(z), fx = x - gx * 32, fz = z - gz * 32;
    const auto sample = [&](int a, int b) {
        return static_cast<int>(mix(terrain_.seed ^ mix(static_cast<std::uint64_t>(a)) ^
                                    (mix(static_cast<std::uint64_t>(b)) << 1)) %
                                17) -
               8;
    };
    const int near = sample(gx, gz) * (32 - fx) + sample(gx + 1, gz) * fx;
    const int far = sample(gx, gz + 1) * (32 - fx) + sample(gx + 1, gz + 1) * fx;
    return 62 + (near * (32 - fz) + far * fz) / 1024;
}
World::Block World::generated_block(Position p) const {
    const auto [x, y, z] = p;
    if (y < 0 || y > 255)
        return 0;
    const auto height = surface(x, z);
    if (y > height)
        return 0;
    if (y == 0)
        return 4; // internal bedrock
    if (y == height)
        return 2;                   // grass
    return y >= height - 3 ? 3 : 1; // dirt / stone
}
World::Block World::block(Position p) const {
    const auto [x, y, z] = p;
    if (y < 0 || y > 255)
        return 0;
    const auto key = std::pair{chunk_coordinate(x), chunk_coordinate(z)};
    if (terrain_.kind && !legacy_columns_.contains(key)) {
        if (auto edit = edits_.find(p); edit != edits_.end())
            return edit->second;
        if (auto column = columns_.find(key); column != columns_.end())
            return column->second.block(local_coordinate(x), static_cast<unsigned>(y),
                                        local_coordinate(z));
        return generated_block(p);
    }
    auto column = columns_.find(key);
    return column == columns_.end()
               ? 0
               : column->second.block(local_coordinate(x), static_cast<unsigned>(y),
                                      local_coordinate(z));
}
void World::configure_terrain(TerrainConfig config) {
    if (config.kind > 2)
        throw std::invalid_argument("invalid terrain generator");
    if (terrain_.kind)
        return; // persisted generation settings win on restart
    for (const auto &[key, column] : columns_)
        legacy_columns_.insert(key);
    terrain_ = config;
}
void World::ensure_chunk(int x, int z) {
    if (!terrain_.kind || legacy_columns_.contains({x, z}) || columns_.contains({x, z}))
        return;
    if (x < std::numeric_limits<int>::min() / 16 || x > std::numeric_limits<int>::max() / 16 ||
        z < std::numeric_limits<int>::min() / 16 || z > std::numeric_limits<int>::max() / 16)
        throw std::out_of_range("terrain outside block coordinates");
    Chunk chunk;
    for (unsigned bx = 0; bx < 16; ++bx)
        for (unsigned bz = 0; bz < 16; ++bz) {
            const int px = x * 16 + static_cast<int>(bx), pz = z * 16 + static_cast<int>(bz),
                      height = surface(px, pz);
            for (int y = 0; y <= height; ++y)
                chunk.set_block(bx, static_cast<unsigned>(y), bz,
                                y == 0            ? 4
                                : y == height     ? 2
                                : y >= height - 3 ? 3
                                                  : 1);
        }
    for (const auto &[position, value] : edits_) {
        const auto [px, y, pz] = position;
        if (chunk_coordinate(px) == x && chunk_coordinate(pz) == z)
            chunk.set_block(local_coordinate(px), static_cast<unsigned>(y), local_coordinate(pz),
                            value);
    }
    count_ += chunk.stored_blocks();
    columns_.emplace(std::pair{x, z}, std::move(chunk));
    columns_.at({x,z}).set_light_revision(++light_revision_);
    std::set<Position> scheduled_water;
    if (const auto queue=water_ticks_.find({x,z});queue!=water_ticks_.end())
        for (const auto &tick : queue->second.heap) scheduled_water.insert(tick.position);
    columns_.at({x,z}).for_each_block([&](unsigned bx, unsigned y, unsigned bz, BlockId id) {
        if (is_flowing_water(id)) {
            const Position at{static_cast<int>(std::int64_t(x)*16+bx), static_cast<int>(y),
                              static_cast<int>(std::int64_t(z)*16+bz)};
            if (!scheduled_water.contains(at))
                schedule_water(at);
        }
    });
    terrain_cache_.push_back({x, z});
    while (terrain_cache_.size() > 256) {
        const auto old = terrain_cache_.front();
        terrain_cache_.pop_front();
        if (auto i = columns_.find(old); i != columns_.end()) {
            count_ -= i->second.stored_blocks();
            columns_.erase(i);
            ++light_revision_;
        }
    }
}
void World::set_block(Position p, Block value, int flags) {
    const auto [x, y, z] = p;
    if (y < 0 || y >= 256)
        throw std::out_of_range("block outside world height");
    const auto key = std::pair{chunk_coordinate(x), chunk_coordinate(z)};
    if (terrain_.kind && !legacy_columns_.contains(key)) {
        ensure_chunk(key.first, key.second);
        if (!edits_.contains(p) && edits_.size() >= 100'000)
            throw std::length_error("terrain edit limit exceeded");
        if (value == generated_block(p))
            edits_.erase(p);
        else
            edits_.insert_or_assign(p, value);
    }
    auto i = columns_.find(key);
    if (i == columns_.end()) {
        if (!value)
            return;
        i = columns_.try_emplace(key).first;
    }
    const auto old = i->second.block(local_coordinate(x), static_cast<unsigned>(y), local_coordinate(z));
    if (block_opacity(old) != block_opacity(value) || (old == kStillLava) != (value == kStillLava))
        i->second.set_light_revision(++light_revision_);
    const auto before = i->second.stored_blocks();
    i->second.set_block(local_coordinate(x), static_cast<unsigned>(y), local_coordinate(z), value);
    count_ = count_ - before + i->second.stored_blocks();
    if (!i->second.stored_blocks() && (!terrain_.kind || legacy_columns_.contains(key)))
        columns_.erase(i);
    if (old != value) water_changed(p, old, value, flags);
}
} // namespace bds
