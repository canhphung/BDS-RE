#pragma once
#include "bds/game/player.hpp"
#include "bds/network/connection.hpp"
#include "bds/network/packet_ids.hpp"
#include "bds/world/world.hpp"
#include "bds/game/item_actor.hpp"
#include "bds/game/attributes.hpp"
#include "bds/game/items.hpp"
namespace bds::game {
using protocol::ByteReader;
using protocol::Bytes;
using protocol::ByteWriter;
using protocol::GamePacket;
using protocol::PacketId;
GamePacket packet(PacketId id, Bytes body);
void item(ByteWriter &out, ItemStack stack);
ItemStack item(ByteReader &in);
World::Position position(ByteReader &in);
void position(ByteWriter &out, World::Position p);
float number(ByteReader &in);
std::uint32_t fixed(ByteReader &in, unsigned bytes);
// MovePlayerPacket (read 0x18b4190 / write 0x18b40b0): runtime id, position,
// pitch/yaw, head yaw, mode, onGround, riding runtime id, and two signed
// ints (teleport cause, source actor type) only in mode 2. The original
// reader does not validate the mode or the riding id.
struct Motion {
    std::uint64_t runtime = 0;
    PlayerData data;
    std::uint8_t mode = 0;
    bool grounded = false;
    std::uint64_t riding = 0;
    std::int32_t teleport_cause = 0, teleport_source = 0;
};
Motion motion(std::span<const std::uint8_t> body);
Bytes motion(const Motion &value);
Bytes inventory(const PlayerData &p);
Bytes cursor_inventory(const PlayerData &p);
Bytes creative_inventory();
Bytes armor_inventory(const PlayerData &p); // InventoryContent window 120
Bytes armor_equipment(std::uint64_t runtime, const PlayerData &p); // MobArmorEquipment
Bytes block_update(World::Position p, BlockId id, unsigned flags = 3);
Bytes equipment(std::uint64_t runtime, const PlayerData &p);
// AddPlayerPacket with the original's full player metadata and device id.
Bytes add_player(const auth::Identity &identity, const protocol::StartGame &game,
                 const PlayerData &data, std::uint64_t flags);
Bytes actor_flags(std::uint64_t runtime, std::uint64_t flags);
// SetActorDataPacket with the complete player metadata (first dirty data).
Bytes player_data(std::uint64_t runtime, const std::string &name, std::uint64_t flags);
// UpdateAttributesPacket for a list of attribute handles (dirty list order).
Bytes update_attributes(std::uint64_t runtime, const Attributes &attributes, const std::vector<int> &ids);
// The original's first dirty UpdateAttributesPacket for a joining player.
Bytes spawn_attributes(std::uint64_t runtime, const Attributes &attributes);
Bytes actor_bounding_box(std::uint64_t runtime, float width, float height);
Bytes respawned_data(std::uint64_t runtime, std::uint64_t flags);
Bytes player_died_event(std::int64_t unique, int cause);
Bytes death_message(const std::string &key, const std::string &player_token);
Bytes respawn_position(float x, float y, float z);
Bytes set_health(int health);

Bytes remove_player_list(const auth::Identity &identity);
struct Action {
    unsigned source = 0, slot = 0;
    int window = 0;       // container id (source 0), flags (source 2, world interaction)
    ItemStack before, after;
};
struct Transaction {
    unsigned type = 0, action = 0;
    std::uint64_t target = 0;   // ItemUseOnActor (3): entity runtime id
    std::vector<Action> changes;
    World::Position block{};
    int face = 0, slot = 0;
    ItemStack held;
    float from[3] = {0, 0, 0};  // player position as the client saw it
    float click[3] = {0, 0, 0}; // click position on the face
};
Transaction transaction(std::span<const std::uint8_t> bytes);

// Item entity packets (protocol 282).
// SynchedActorData::packAll of a fresh ItemActor, as captured from the original.
Bytes item_actor_data(std::uint64_t runtime);
// AddActorPacket for a mob (protocol 282: numeric ActorType). Attribute and
// metadata lists as the original sends them for a newly spawned animal
// (input capture world-mobs-p1: chicken and pig AddActor bodies).
struct MobView {
    std::int64_t unique_id;
    std::uint64_t runtime_id;
    std::uint32_t type;
    Vec3 pos, motion;
    float pitch, yaw, head_yaw;
    bool baby;
    float scale, width, height;
    int variant, mark_variant;
    std::uint8_t color;
    float health, max_health, movement, fall_damage;
    bool tamed = false, saddled = false;
};
Bytes add_mob_actor(const MobView &mob);
// MobArmorEquipmentPacket with four empty slots.
Bytes empty_armor_equipment(std::uint64_t runtime);                  // SetActorDataPacket
Bytes add_item_actor(const ItemActor &a);                      // AddItemActorPacket::write 0x18b0680
// MoveActorDeltaPacket: runtime id, flags (x, y, z, pitch, yaw, head yaw,
// onGround, teleported), then each present position delta as a zigzag varint
// of the difference of the float bit patterns (MoveActorAbsoluteData::getIntDelta).
Bytes move_actor_delta(std::uint64_t runtime, std::uint8_t flags, const std::int32_t delta[3]);
Bytes set_actor_motion(std::uint64_t runtime, Vec3 motion);
Bytes take_item_actor(std::uint64_t item, std::uint64_t player);
Bytes remove_actor(std::int64_t unique);
Bytes actor_event(std::uint64_t runtime, std::uint8_t event, std::int32_t data);
struct SlotChange {
    unsigned slot = 0;
    ItemStack before, after;
};
// InventoryTransactionPacket (normal) sent after Player::take: the
// world-interaction action (taken -> left), then one container action per
// changed inventory slot (byte order checked against the original capture).
Bytes pickup_transaction(const std::vector<SlotChange> &slots, ItemStack taken, ItemStack left);

// Combat, effects and eating (packet formats from their write functions).
Bytes mob_effect(std::uint64_t runtime, int event, int effect, int amplifier, bool particles, int duration); // 0x18bc1e0
Bytes level_sound(int sound, Vec3 pos, int extra, int entity_type, bool baby = false, bool global = false); // V1
Bytes set_last_hurt_by(int actor_type);
Bytes hurt_armor(int damage);
Bytes animate(int action, std::uint64_t runtime);
Bytes effect_data(std::uint64_t runtime, std::int32_t color, bool ambient, unsigned keys = 3); // SetActorData keys 8 (bit 0), 9 (bit 1)
// Per-tick replication of an added item (MoveActorDelta, SetActorMotion).
std::vector<GamePacket> replicate(ItemActor &a);
} // namespace bds::game
