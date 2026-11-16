#pragma once
#include "bds/game/player.hpp"
#include "bds/network/connection.hpp"
#include "bds/network/packet_ids.hpp"
#include "bds/world/world.hpp"
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
struct Motion {
    std::uint64_t runtime = 0;
    PlayerData data;
    std::uint8_t mode = 0;
    bool grounded = false;
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
} // namespace bds::game
