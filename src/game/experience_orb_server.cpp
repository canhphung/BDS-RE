// Experience orbs in the server, translated from BDS 1.6.0.15 (Linux):
// ExperienceOrb::spawnOrbs 0x2405910, ExperienceOrb::normalTick 0x24045d0
// (follow-target step), ExperienceOrb::playerTouch 0x2405150,
// Player::addExperience 0x22f6750, Player::addLevels 0x22f68a0 /
// Player::_addLevels 0x22f48a0. The level-Random draws (reward, four per new
// orb, one nextInt(6) per touch from the orb's tick and the player's aiStep)
// were checked against the original's trace (capture mob-combat-c1).
#include "bds/game/server.hpp"
#include <cmath>
#include <limits>

namespace bds::game {
using namespace protocol;
namespace {
std::pair<int, int> orb_chunk(const ExperienceOrb &orb) {
    return {chunk_coordinate(static_cast<int>(std::floor(orb.pos.x))),
            chunk_coordinate(static_cast<int>(std::floor(orb.pos.z)))};
}
// Actor::getAttachPos(ActorLocation 1) 0x21aaaa0: the AABB centre,
// min + (max - min) * 0.5 (AABB::getCenter 0x2189800); a player's box is
// 0.6 x 1.8 from the feet (eye position - 1.62).
Vec3 player_body(const PlayerData &p) {
    const Vec3 min{p.x - 0.3f, p.y - 1.62f, p.z - 0.3f};
    const Vec3 max{p.x + 0.3f, p.y - 1.62f + 1.8f, p.z + 0.3f};
    return {min.x + (max.x - min.x) * 0.5f, min.y + (max.y - min.y) * 0.5f, min.z + (max.z - min.z) * 0.5f};
}
GamePacket level_event(int event, Vec3 at, int data) {
    ByteWriter out;
    out.signed_varint(event);
    out.float32(at.x);
    out.float32(at.y);
    out.float32(at.z);
    out.signed_varint(data);
    return packet(PacketId::LevelEvent, out.take());
}
} // namespace

void Server::spawn_orbs(Vec3 pos, int amount) {
    // ExperienceOrb::spawnOrbs 0x2405910 with DropType 2 (from a mob that is
    // not a player): game rule doMobLoot (GameRuleId 4, default true); the
    // amount is split into truncateExperienceAmount pieces, each an orb made
    // by ActorFactory::createSpawnedEntity (ActorType 0x45) at `pos`, given
    // its value and added to the level.
    while (amount > 0) {
        const int value = truncate_experience_amount(amount);
        amount -= value;
        auto orb = make_orb(pos, value, level_random_);
        const auto id = allocate_entity_id();
        orb.runtime_id = static_cast<std::uint64_t>(id);
        orb.unique_id = static_cast<std::int64_t>(0xffffffff00000000ULL | orb.runtime_id); // as for mobs (capture)
        orb.owner_chunk = orb_chunk(orb);
        chunk_actors_.add(orb.owner_chunk, orb.runtime_id);
        // The new actor's data (SynchedActorData::packAll) reaches the players
        // in whose ticking area it lies at once, a tick before its AddActor
        // (capture mob-combat-c1 packets 346 and 351).
        const auto data = packet(PacketId::SetActorData, orb_actor_data(orb.runtime_id, orb.value));
        for (auto &[player, session] : sessions_) {
            if (!session.active)
                continue;
            const auto &p = world_.players().at(session.key);
            const int cx = chunk_coordinate(static_cast<int>(std::floor(p.x)));
            const int cz = chunk_coordinate(static_cast<int>(std::floor(p.z)));
            for (auto [dx, dz] : ticking_offsets())
                if (std::pair{cx + dx, cz + dz} == orb.owner_chunk) {
                    enqueue(player, data);
                    break;
                }
        }
        orbs_.push_back(std::move(orb));
    }
}

void Server::tick_orb(ExperienceOrb &orb) {
    update_water_current(orb, world_); // Actor::updateWaterState, before normalTick
    if (orb_physics(orb, world_, level_random_)) {
        // Actor::playSynchronizedSound(FIZZ 0x1b) at getAttachPos(1), extra -1.
        const Vec3 at{orb.bb.min.x + (orb.bb.max.x - orb.bb.min.x) * 0.5f,
                      orb.bb.min.y + (orb.bb.max.y - orb.bb.min.y) * 0.5f,
                      orb.bb.min.z + (orb.bb.max.z - orb.bb.min.z) * 0.5f};
        broadcast(packet(PacketId::LevelSoundEvent, level_sound(27, at, -1, 69)));
    }
    if (orb.pos.y < 0.0f && orb.removed)
        return; // Actor::baseTick outOfWorld: normalTick ends there
    // The follow target: Level::fetchEntity(+0x11b8). Every 20 + uniqueId % 100
    // ticks (signed 64-bit remainder) the target is kept while within 8
    // blocks (distanceToSqr <= 64), else replaced by
    // Dimension::fetchNearestPlayer(orb, 8).
    const auto find = [this](std::int64_t unique) -> std::pair<std::uint64_t, Session *> {
        for (auto &[id, s] : sessions_)
            if (s.active && s.game.unique_id == unique)
                return {id, &s};
        return {0, nullptr};
    };
    auto target = orb.following ? find(*orb.following) : std::pair<std::uint64_t, Session *>{0, nullptr};
    const int every = static_cast<int>(orb.unique_id % 100) + 20;
    if (orb.follow_time < orb.tick_count - every) {
        const auto distance_sqr = [&](const Session &s) {
            const auto &p = world_.players().at(s.key);
            const float dx = p.x - orb.pos.x, dy = p.y - orb.pos.y, dz = p.z - orb.pos.z;
            return dx * dx + dy * dy + dz * dz; // Actor::distanceToSqr
        };
        if (!target.second || distance_sqr(*target.second) > 64.0f) {
            // Dimension::fetchNearestPlayer(x, y, z, 8) 0x1d66f80: the closest
            // player strictly within the distance, in the level's player order.
            float best = -1.0f;
            target = {0, nullptr};
            for (const auto id : player_order_) {
                const auto found = sessions_.find(id);
                if (found == sessions_.end() || !found->second.active)
                    continue;
                const float d = distance_sqr(found->second);
                if (d < 8.0f * 8.0f && (best == -1.0f || d < best)) {
                    best = d;
                    target = {id, &found->second};
                }
            }
            if (target.second)
                orb.following = target.second->game.unique_id;
            else
                orb.following.reset(); // ActorUniqueID() (invalid)
        }
        orb.follow_time = orb.tick_count;
    }
    if (target.second) {
        const auto body = player_body(world_.players().at(target.second->key));
        const Vec3 delta{(body.x - orb.pos.x) / 8.0f, (body.y - orb.pos.y) / 8.0f, (body.z - orb.pos.z) / 8.0f};
        if (!orb_attract(orb, delta)) {
            // Close enough: Actor::move(delta), then playerTouch for a player.
            move(orb, world_, delta);
            orb_touch(target.first, *target.second, orb);
        }
    }
    ++orb.tick_count;
    if (!orb.added)
        orb.added = true; // in its chunk: AddActor at the players' next _updateNearbyEntities
}

void Server::orb_touch(std::uint64_t id, Session &s, ExperienceOrb &orb) {
    // ExperienceOrb::playerTouch 0x2405150 (server side). INFERRED: a removed
    // orb is not touched again (BlockSource::fetchEntities skips it).
    if (orb.removed)
        return;
    if (s.dead || s.attributes[kHealth] <= 0.0f) // Mob::isAlive
        return;
    if (level_random_.next_int(6) != 0)
        return;
    // Level::broadcastDimensionEvent(SoundExperienceOrbPickup 0x41b, orb position, 0).
    broadcast(level_event(1051, orb.pos, 0));
    // handleMending 0x2405280: EnchantUtils::getRandomItemWithMending finds no
    // item (no enchantments in BDS-RE) and draws nothing.
    if (orb.value > 0)
        add_experience(id, s, orb.value); // Player::addExperience (vtable +0xca8)
    orb.removed = true;                   // Actor::remove
}

void Server::add_experience(std::uint64_t id, Session &s, int amount) {
    // Player::addExperience 0x22f6750. getXpNeededForNextLevel caches
    // _computeLevelXp(level) until _addLevels; recomputing it is the same.
    const auto needed = [&] { return level_xp(static_cast<int>(s.attributes[kLevel])); };
    float progress = s.attributes[kExperience] + static_cast<float>(amount) / static_cast<float>(needed());
    s.experience += amount; // +0x1414
    while (progress >= 1.0f) {
        progress = (progress - 1.0f) * static_cast<float>(needed());
        add_levels(id, s, 1);
        progress = progress / static_cast<float>(needed());
    }
    // EXPERIENCE: AttributeInstance::resetToMinValue (always dirty), then an
    // InstantaneousAttributeBuff(progress, 9).
    auto &a = s.attributes.values[static_cast<std::size_t>(kExperience)];
    a.value = a.min;
    s.attributes.mark(kExperience);
    add_buff(id, s, kExperience, progress, kBuffNone);
}

void Server::add_levels(std::uint64_t id, Session &s, int levels) {
    // Player::addLevels 0x22f68a0 on the server -> Player::_addLevels 0x22f48a0.
    const int level = static_cast<int>(s.attributes[kLevel]);
    if (level + levels < 0) {
        // Player::resetPlayerLevel 0x22e6b60: LEVEL and EXPERIENCE to their minimum.
        for (int attribute : {kLevel, kExperience}) {
            auto &a = s.attributes.values[static_cast<std::size_t>(attribute)];
            a.value = a.min;
            s.attributes.mark(attribute);
        }
        s.last_level_up = 0;
    } else
        add_buff(id, s, kLevel, static_cast<float>(levels), kBuffNone);
    const int now = static_cast<int>(s.attributes[kLevel]);
    if (levels > 0 && now % 5 == 0 &&
        static_cast<float>(s.tick_count) - 100.0f > static_cast<float>(s.last_level_up)) {
        // LevelSoundEvent LEVELUP (0x3e) at getAttachPos(1), volume data
        // 0.75 * min(level / 30, 1) * INT_MAX, ActorType 1 (undefined).
        const float volume = now > 30 ? 1.0f : static_cast<float>(now) / 30.0f;
        const int data = static_cast<int>(0.75f * volume * static_cast<float>(std::numeric_limits<int>::max()));
        broadcast(packet(PacketId::LevelSoundEvent, level_sound(62, player_body(world_.players().at(s.key)), data, 1)));
        s.last_level_up = s.tick_count;
    }
}
} // namespace bds::game
