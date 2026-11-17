#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace bds::game {
using TickChunkPosition = std::pair<int, int>;
struct PlayerWorldTickCallbacks {
    std::function<std::optional<TickChunkPosition>(std::uint64_t)> center;
    std::function<bool(TickChunkPosition)> ready;
    std::function<void(TickChunkPosition)> chunk;
    // Player::tickWorld's Actor::tick, then ServerPlayer::_updateNearbyEntities.
    std::function<void(std::uint64_t)> player;
};
// Player::tickWorld (0x22f4f4e..0x22f5271), driven by Level::forEachPlayer.
// The supplied order/offsets are the actual current Level lists. A chunk is
// marked ticked only after a ready chunk callback returns, like the original
// LevelChunk::wasTickedThisTick / last-tick assignment.
void tick_player_worlds(std::span<const std::uint64_t> players,
                        std::span<const TickChunkPosition> offsets,
                        const PlayerWorldTickCallbacks &callbacks);

// LevelChunk actor storage is a SmallSet backed by a vector. Add appends;
// erase moves the last element into the vacated slot, not vector::erase.
// Original add/remove: 0x1de31e0 / 0x1de3260; SmallSet::erase: 0x1deb820.
// Stores IDs here so physical actor storage can relocate without stale pointers.
class ChunkActorIndex {
  public:
    void add(TickChunkPosition chunk, std::uint64_t actor);
    bool erase(TickChunkPosition chunk, std::uint64_t actor);
    void move(TickChunkPosition from, TickChunkPosition to, std::uint64_t actor);
    const std::vector<std::uint64_t> &members(TickChunkPosition chunk) const;

  private:
    std::map<TickChunkPosition, std::vector<std::uint64_t>> chunks_;
};
} // namespace bds::game
