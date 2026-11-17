#include "bds/game/world_tick.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace bds::game {
void tick_player_worlds(std::span<const std::uint64_t> players,
                        std::span<const TickChunkPosition> offsets,
                        const PlayerWorldTickCallbacks &callbacks) {
    std::set<TickChunkPosition> ticked;
    for (const auto id : players) {
        const auto center = callbacks.center(id);
        if (!center)
            continue;
        for (const auto &[dx, dz] : offsets) {
            const TickChunkPosition chunk{center->first + dx, center->second + dz};
            if (!ticked.contains(chunk) && callbacks.ready(chunk)) {
                callbacks.chunk(chunk);
                ticked.insert(chunk);
            }
        }
        callbacks.player(id);
    }
}
void ChunkActorIndex::add(TickChunkPosition chunk, std::uint64_t actor) {
    auto &members = chunks_[chunk];
    if (std::find(members.begin(), members.end(), actor) != members.end())
        throw std::logic_error("actor already owned by chunk");
    members.push_back(actor);
}
bool ChunkActorIndex::erase(TickChunkPosition chunk, std::uint64_t actor) {
    auto entry = chunks_.find(chunk);
    if (entry == chunks_.end())
        return false;
    auto &members = entry->second;
    const auto found = std::find(members.begin(), members.end(), actor);
    if (found == members.end())
        return false;
    *found = members.back(); // SmallSet::erase, 0x1deb841..0x1deb86a
    members.pop_back();
    if (members.empty())
        chunks_.erase(entry);
    return true;
}
void ChunkActorIndex::move(TickChunkPosition from, TickChunkPosition to, std::uint64_t actor) {
    if (from == to) // Actor::moveChunks, 0x21aa5a8
        return;
    if (!erase(from, actor))
        throw std::logic_error("actor absent from owning chunk");
    add(to, actor);
}
const std::vector<std::uint64_t> &ChunkActorIndex::members(TickChunkPosition chunk) const {
    static const std::vector<std::uint64_t> empty;
    const auto found = chunks_.find(chunk);
    return found == chunks_.end() ? empty : found->second;
}
} // namespace bds::game
