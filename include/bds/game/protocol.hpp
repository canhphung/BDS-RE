#pragma once
#include "bds/game/player.hpp"
#include "bds/network/connection.hpp"
#include "bds/network/packet_ids.hpp"
#include "bds/world/world.hpp"
#include "bds/game/item_actor.hpp"
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
Bytes block_update(World::Position p, BlockId id);
Bytes equipment(std::uint64_t runtime, const PlayerData &p);
Bytes add_player(const auth::Identity &identity, const protocol::StartGame &game,
                 const PlayerData &data);
Bytes remove_player_list(const auth::Identity &identity);
struct Action {
    unsigned source = 0, slot = 0;
    int window = 0;
    ItemStack before, after;
};
struct Transaction {
    unsigned type = 0, action = 0;
    std::vector<Action> changes;
    World::Position block{};
    int face = 0, slot = 0;
    ItemStack held;
};
Transaction transaction(std::span<const std::uint8_t> bytes);

// Item entity packets (protocol 282).
// SynchedActorData::packAll of a fresh ItemActor, as captured from the original.
Bytes item_actor_data(std::uint64_t runtime);                  // SetActorDataPacket
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
// Per-tick replication of an added item (MoveActorDelta, SetActorMotion).
std::vector<GamePacket> replicate(ItemActor &a);
} // namespace bds::game
