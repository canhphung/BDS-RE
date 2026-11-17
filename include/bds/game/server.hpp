#pragma once
#include "bds/game/protocol.hpp"
#include "bds/game/world_tick.hpp"
#include "bds/world/chunk_ticks.hpp"
#include "bds/server/access.hpp"
#include "bds/server/properties.hpp"
#include <deque>
#include <map>
#include <random>
#include <set>
namespace bds::game {
// Level client ticking offsets (Level::Level 0x1c814e9) for a tick range.
std::vector<std::pair<int, int>> client_ticking_offsets(int range);
// The level seed (LevelSettings::getSeed) as sent in StartGame: the terrain
// seed of generated worlds, else a purely numeric level-seed (0 otherwise).
std::int32_t level_seed(const World &world, const Properties &properties);
class Server {
  public:
    // Level::Level seeds Level::getRandom() with the level seed (0x1c8038f);
    // the first draws shuffle the block palette (ServerLevel::ServerLevel ->
    // BlockPalette::initFromBlockDefinitions), so runtime ids match the original.
    Server(World &world, const Properties &properties, AccessLists &access,
           Random::DrawObserver random_observer = {});
    std::vector<GamePacket> prepare(const auth::Identity &identity, protocol::StartGame &game);
    std::vector<GamePacket> handle(std::uint64_t runtime, const GamePacket &packet,
                                   std::uint64_t now);
    std::vector<GamePacket> drain(std::uint64_t runtime);
    // What the `effect <player> <effect> <seconds> <amplifier>` and
    // `effect <player> clear` commands do to a player (EffectCommand:
    // Mob::addEffect with seconds * 20 ticks, Mob::removeAllEffects).
    // Returns false for an unknown runtime id.
    bool give_effect(std::uint64_t runtime, int effect, int seconds, int amplifier);
    bool clear_effects(std::uint64_t runtime);
    // Player::setChunkRadius (0x22f5730): the radius granted to the client.
    void set_chunk_radius(std::uint64_t runtime, int radius);
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
        bool flags_dirty = false;
        // Attributes, hurt state (Mob +0x20c hurt time, +0x12c0 last hurt) and death.
        Attributes attributes;
        int hurt_time = 0, last_hurt = 0;
        bool dead = false, death_data_dirty = false;
        int death_time = 0; // Mob +0x11d8
        float motion[3] = {0, 0, 0}; // Actor +0xa0, set by MovePlayer to the move delta
        int feature_timer = 0;                    // ServerPlayer +0x1b5c
        // Mob +0x1248 effects indexed by id (id 0: none), +0x1359 dirty.
        struct Effect {
            int id = 0, duration = 0, amplifier = 0;
            bool ambient = false, visible = true;
        };
        std::array<Effect, 28> effects{};
        bool effects_dirty = false;
        unsigned effect_keys = 0; // dirty synched data: bit 0 key 8 (colour), bit 1 key 9 (ambient)
        std::int32_t effect_color = 0; // synched data 8
        // Player +0x1808 item in use and +0x1878 its duration (status flag 4).
        ItemStack using_item;
        int use_duration = 0;
        float fall_distance = 0;                  // Actor +0x138
        unsigned chunk_radius = 6;                // Player +0x1a68 (6 in Player::Player)
        // Actor +0x2b0: the actor's own Random, Random::Random() (seeded from
        // std::random_device, never reseeded): drop throws, knockback roll.
        Random random{std::random_device{}()};
        int damage_remainder = 0, absorb_remainder = 0; // Mob +0x12c4, +0x12c8
        std::uint64_t flags() const;
    };
    World &world_;
    WorldLighting lighting_;
    BlockTickRandom block_tick_random_;
    const Properties &properties_;
    AccessLists &access_;
    std::map<std::uint64_t, Session> sessions_;
    std::vector<ItemActor> items_;
    ChunkActorIndex chunk_actors_;
    std::vector<std::uint64_t> pending_item_moves_;
    std::int64_t next_entity_id_ = 1;
    // Level::getRandom (Level +0x568): seeded with the level seed in the
    // constructor. The thread-local Random is seeded from std::random_device
    // (Random::Random() 0x187d430).
    Random level_random_{0};
    Random actor_random_{std::random_device{}()};
    void spawn_item(World::Position block, ItemStack item);
    void tick_item(ItemActor &a);
    void add_item(ItemActor actor);
    void touch(Session &s, ItemActor &a);
    void enqueue(std::uint64_t runtime, GamePacket packet);
    // Dimension::sendPacketForEntity (0x1d69020): players for whom the entity
    // is relevant (itself, or in their known set), except `except`.
    void send_for_entity(std::uint64_t entity, const GamePacket &packet, std::uint64_t except = 0);
    void tick_player(std::uint64_t id, Session &s);
    // Attribute system (see src/game/server.cpp for the original functions).
    void add_buff(std::uint64_t id, Session &s, int attribute, float amount, int type);
    bool health_change(std::uint64_t id, Session &s, float old_value, float new_value, int type);
    void cause_food_exhaustion(std::uint64_t id, Session &s, float amount);
    void tick_attributes(std::uint64_t id, Session &s);
    // attacker: runtime id of the player behind an ActorDamageByActorSource, 0 for none.
    bool hurt(std::uint64_t id, Session &s, int cause, int damage, std::uint64_t attacker = 0, bool knock = true);
    bool hurt_effects(std::uint64_t id, Session &s, int cause, int damage, std::uint64_t attacker = 0, bool knock = true);
    void actually_hurt(std::uint64_t id, Session &s, int cause, int damage);
    void knockback(std::uint64_t id, Session &s, float xd, float zd);
    // Effects (Mob::addEffect 0x22b0630, tickEffects 0x22b2050).
    void add_effect(std::uint64_t id, Session &s, int effect, int duration, int amplifier);
    void remove_effect(std::uint64_t id, Session &s, int effect);
    void remove_all_effects(std::uint64_t id, Session &s);
    void apply_effect(std::uint64_t id, Session &s, const Session::Effect &e);
    void remove_effect_buffs(std::uint64_t id, Session &s, int effect);
    void tick_effects(std::uint64_t id, Session &s);
    void background_tick_seasons();
    void save_some_chunks();
    void tick_spawner(int chunk_x, int chunk_z);
    // Items: ItemUse click air (GameMode::useItem), ItemRelease consume,
    // ItemUseOnActor attack, and Player::drop.
    std::vector<GamePacket> use_item(std::uint64_t id, Session &s, const Transaction &t);
    std::vector<GamePacket> release_item(std::uint64_t id, Session &s, const Transaction &t);
    std::vector<GamePacket> use_on_actor(std::uint64_t id, Session &s, const Transaction &t);
    void attack(std::uint64_t id, Session &s, std::uint64_t target);
    void eat(std::uint64_t id, Session &s, const ItemStack &item);
    void drop(std::uint64_t id, Session &s, ItemStack item, bool randomly);
    void set_sprinting(Session &s, bool sprinting);
    void player_move(Session &s, PlayerData &p);
    std::vector<GamePacket> inventory_resend(const Session &s) const;
    void cause_fall_damage(std::uint64_t id, Session &s, float distance);
    void die(std::uint64_t id, Session &s, int cause);
    void respawn(std::uint64_t id, Session &s);
    void update_nearby(std::uint64_t id, Session &s);
    std::vector<std::pair<int, int>> ticking_offsets() const;
    std::set<std::uint64_t> fresh_items_; // added to the level this tick
    std::vector<std::uint64_t> player_order_; // Level +0x50: players in join order, shuffled every 20 ticks
    std::vector<std::pair<int, int>> chunk_offsets_; // Level +0x8
    std::uint64_t level_tick_ = 0;              // Level::getCurrentTick
    bool overworld_created_ = false;            // Level dimension map non-empty (first player)
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
