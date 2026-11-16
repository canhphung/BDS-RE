#pragma once
#include "bds/game/protocol.hpp"
#include "bds/server/access.hpp"
#include "bds/server/properties.hpp"
#include <deque>
#include <map>
#include <random>
#include <set>
namespace bds::game {
// Level client ticking offsets (Level::Level 0x1c814e9) for a tick range.
std::vector<std::pair<int, int>> client_ticking_offsets(int range);
class Server {
  public:
    Server(World &world, const Properties &properties, AccessLists &access)
        : world_(world), properties_(properties), access_(access) {}
    std::vector<GamePacket> prepare(const auth::Identity &identity, protocol::StartGame &game);
    std::vector<GamePacket> handle(std::uint64_t runtime, const GamePacket &packet,
                                   std::uint64_t now);
    std::vector<GamePacket> drain(std::uint64_t runtime);
    std::optional<std::pair<int, int>> center(std::uint64_t runtime) const;
    void leave(std::uint64_t runtime);
    // One level tick: item entities (ItemActor::normalTick, replication) and
    // player item pickup (Player::aiStep touch).
    void tick();
    // Level::getNewUniqueID / getNextRuntimeID share one counter in BDS-RE.
    std::int64_t allocate_entity_id() { return next_entity_id_++; }
    const std::vector<ItemActor> &items() const { return items_; }

  private:
    struct Session {
        auth::Identity identity;
        protocol::StartGame game;
        std::string key;
        bool active = false;
        std::deque<GamePacket> queue;
        bool overflow = false;
        // ServerPlayer +0x1bb8: entities this client has been sent (runtime ids).
        std::set<std::uint64_t> known;
        // MovementInterpolator (Mob +0x129c): target position/rotation, steps.
        struct Lerp {
            float x = 0, y = 0, z = 0, pitch = 0, yaw = 0;
            int steps = 0;
        } lerp;
        bool was_on_ground = false; // Actor +0x125, written at the end of each tick
        bool sneaking = false, sprinting = false; // actor flags 1 and 3
        bool flags_dirty = false, movement_dirty = false;
        int feature_timer = 0;                    // ServerPlayer +0x1b5c
        std::uint64_t flags() const;
    };
    World &world_;
    const Properties &properties_;
    AccessLists &access_;
    std::map<std::uint64_t, Session> sessions_;
    std::vector<ItemActor> items_;
    std::int64_t next_entity_id_ = 1;
    // Level::getRandom and the thread-local Random of the original are both
    // seeded from std::random_device (Random::Random() 0x187d430).
    Random level_random_{std::random_device{}()};
    Random actor_random_{std::random_device{}()};
    void spawn_item(World::Position block, ItemStack item);
    void tick_item(ItemActor &a);
    void touch(Session &s, ItemActor &a);
    void enqueue(std::uint64_t runtime, GamePacket packet);
    // Dimension::sendPacketForEntity (0x1d69020): players for whom the entity
    // is relevant (itself, or in their known set), except `except`.
    void send_for_entity(std::uint64_t entity, const GamePacket &packet, std::uint64_t except = 0);
    void tick_player(std::uint64_t id, Session &s);
    void update_nearby(std::uint64_t id, Session &s);
    std::vector<std::pair<int, int>> ticking_offsets() const;
    std::set<std::uint64_t> fresh_items_; // added to the level this tick
    void broadcast(GamePacket packet, std::uint64_t exclude = 0);
    bool collides(const PlayerData &p) const;
    bool reach(const PlayerData &p, World::Position block) const;
    std::vector<GamePacket> correct(Session &s);
    std::vector<GamePacket> resend_blocks(const Transaction &t) const;
    std::vector<GamePacket> reject_item_use(const Session &s, const Transaction &t) const;
    std::vector<GamePacket> use(Session &s, const Transaction &t);
    std::vector<GamePacket> destroy(Session &s, const Transaction &t);
};
} // namespace bds::game
