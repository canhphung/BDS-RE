#pragma once

#include "bds/world/chunk.hpp"
#include <cstdint>
#include <map>
#include <limits>
#include <tuple>
#include <utility>

namespace bds {
// The simulation thread is the sole owner. Missing columns/sections are air.
class World {
public:
    using Position = std::tuple<int, int, int>;
    using Block = BlockId;

    Block block(Position position) const {
        const auto [x, y, z] = position;
        if (y < 0 || y >= static_cast<int>(Chunk::height)) return 0;
        auto column = columns_.find({chunk_coordinate(x), chunk_coordinate(z)});
        if (column == columns_.end()) return 0;
        return column->second.block(local_coordinate(x), static_cast<unsigned>(y), local_coordinate(z));
    }

    void set_block(Position position, Block value) {
        const auto [x, y, z] = position;
        if (y < 0 || y >= static_cast<int>(Chunk::height))
            throw std::out_of_range("block outside world height");
        const auto key = std::pair{chunk_coordinate(x), chunk_coordinate(z)};
        auto column = columns_.find(key);
        if (column == columns_.end()) {
            if (value == 0) return;
            column = columns_.try_emplace(key).first;
        }
        const auto before = column->second.stored_blocks();
        column->second.set_block(local_coordinate(x), static_cast<unsigned>(y), local_coordinate(z), value);
        count_ = count_ - before + column->second.stored_blocks();
        if (column->second.stored_blocks() == 0) columns_.erase(column);
    }

    void tick() {
        if (age_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("world age exhausted");
        ++age_;
    }
    // Read-only view for encoding on the owning simulation thread. Do not
    // retain this pointer across world mutations.
    const Chunk* chunk(int x, int z) const {
        auto column = columns_.find({x,z});
        return column == columns_.end() ? nullptr : &column->second;
    }
    std::uint64_t age() const { return age_; }
    std::size_t stored_blocks() const { return count_; }
    std::size_t loaded_chunks() const { return columns_.size(); }

    std::size_t allocated_sections() const {
        std::size_t result = 0;
        for (const auto& [key, chunk] : columns_) result += chunk.allocated_sections();
        return result;
    }

    template<class Visitor> void for_each_block(Visitor visit) const {
        for (const auto& [key, chunk] : columns_)
            chunk.for_each_block([&](unsigned x, unsigned y, unsigned z, Block value) {
                visit(Position{static_cast<int>(static_cast<std::int64_t>(key.first) * 16 + x),
                               static_cast<int>(y),
                               static_cast<int>(static_cast<std::int64_t>(key.second) * 16 + z)}, value);
            });
    }

private:
    friend class WorldStore;
    std::map<std::pair<int, int>, Chunk> columns_;
    std::size_t count_ = 0;
    std::uint64_t age_ = 0;
};
}
