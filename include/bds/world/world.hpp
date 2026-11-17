#pragma once

#include <vector>
#include "bds/world/chunk.hpp"
#include "bds/game/player.hpp"
#include <deque>
#include <set>
#include <string>
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

    Block block(Position position) const;
    void set_block(Position position, Block value);
    void configure_terrain(TerrainConfig config);
    void ensure_chunk(int x, int z);
    int surface(int x, int z) const;
    TerrainConfig terrain() const { return terrain_; }
    std::map<std::string, PlayerData>& players() { return players_; }
    const std::map<std::string, PlayerData>& players() const { return players_; }
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
    std::vector<std::pair<int, int>> chunk_positions() const {
        std::vector<std::pair<int, int>> result;
        for (const auto& [key, chunk] : columns_) result.push_back(key);
        return result;
    }

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
    std::uint64_t light_revision_ = 0;
    TerrainConfig terrain_;
    std::set<std::pair<int,int>> legacy_columns_;
    std::map<Position, BlockId> edits_; // includes air, so removed terrain never regenerates
    std::deque<std::pair<int,int>> terrain_cache_;
    std::map<std::string, PlayerData> players_;
    Block generated_block(Position position) const;
};
}
