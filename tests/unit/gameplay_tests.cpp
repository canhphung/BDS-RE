#include "bds/game/server.hpp"
#include "bds/world/store.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
using namespace bds;
using namespace bds::game;
using namespace bds::protocol;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
auth::Identity identity(unsigned n) {
    auth::Identity i;
    i.identity = "00000000-0000-4000-8000-00000000000" + std::to_string(n);
    i.display_name = "Player" + std::to_string(n);
    i.client_data_json = "{}";
    return i;
}
StartGame make_game(unsigned n, unsigned mode = 0) {
    StartGame g;
    g.runtime_id = n;
    g.unique_id = n;
    g.player_game_type = static_cast<int>(mode);
    g.x = n == 2 ? 5.5f : .5f;
    g.y = 5.62f;
    g.settings.spawn = {0, 4, 0};
    return g;
}
GamePacket action(unsigned id, int type, World::Position p) {
    ByteWriter w;
    w.varint64(id);
    w.signed_varint(type);
    position(w, p);
    w.signed_varint(1);
    return packet(PacketId::PlayerAction, w.take());
}
GamePacket use(unsigned type, World::Position p, ItemStack held = {}) {
    ByteWriter w;
    w.varint(2);
    w.varint(0);
    w.varint(type);
    position(w, p);
    w.signed_varint(1);
    w.signed_varint(0);
    item(w, held);
    for (int i = 0; i < 6; ++i)
        w.float32(0);
    return packet(PacketId::InventoryTransaction, w.take());
}
GamePacket changes(std::vector<Action> actions) {
    ByteWriter w;
    w.varint(0);
    w.varint(static_cast<unsigned>(actions.size()));
    for (auto &a : actions) {
        w.varint(a.source);
        if (a.source == 0)
            w.signed_varint(a.window);
        w.varint(a.slot);
        item(w, a.before);
        item(w, a.after);
    }
    return packet(PacketId::InventoryTransaction, w.take());
}
void terrain() {
    World a, b, c;
    a.configure_terrain({2, 42});
    b.configure_terrain({2, 42});
    c.configure_terrain({2, 43});
    bool different = false;
    for (int x = -64; x <= 64; x += 7)
        for (int z = -64; z <= 64; z += 11) {
            check(a.surface(x, z) == b.surface(x, z), "seed determinism");
            different |= a.surface(x, z) != c.surface(x, z);
            check(a.surface(x, z) >= 54 && a.surface(x, z) <= 70, "bounded surface");
        }
    check(different, "seed changes terrain");
    World flat;
    flat.set_block({0, 70, 0}, 1);
    flat.configure_terrain({1, 9});
    check(flat.block({0, 3, 0}) == 0 && flat.block({16, 3, 0}) == 2, "legacy columns preserved");
    flat.set_block({0, 70, 0}, 0);
    flat.set_block({-1, 3, -1}, 0);
    flat.set_block({-2, 4, -1}, 1);
    for (int x = 0; x < 260; ++x)
        flat.ensure_chunk(x, 2);
    check(flat.loaded_chunks() <= 257, "bounded generation cache");
    const auto bytes = WorldStore::encode(flat);
    check(bytes[8] == 2 && bytes.size() < 10000, "sparse procedural snapshot");
    auto restored = WorldStore::decode(bytes, {});
    restored.ensure_chunk(-1, -1);
    check(restored.terrain().seed == 9 && restored.block({-1, 3, -1}) == 0 &&
              restored.block({-2, 4, -1}) == 1 && restored.block({0, 3, 0}) == 0,
          "terrain edits and empty legacy tombstones survive reload");
    auto bad = bytes;
    bad.back() ^= 1;
    bool rejected = false;
    try {
        WorldStore::decode(bad, {});
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "v2 checksum");
}
int main() {
    try {
        terrain();
        ByteWriter simple;
        item(simple, {1, 1});
        check(simple.bytes() == Bytes({2, 2, 0, 0, 0, 0}), "original legacy stone item encoding");
        World w;
        w.configure_terrain({1, 123});
        Properties properties;
        AccessLists access(".");
        Server server(w, properties, access);
        auto a = make_game(1, 1), b = make_game(2);
        server.prepare(identity(1), a);
        server.prepare(identity(2), b);
        server.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
        server.handle(2, packet(PacketId::SetLocalPlayerAsInitialized, {2}), 0);
        const auto joined = server.drain(1);
        check(joined.size() == 2 && joined[0].header.id == 63 && joined[1].header.id == 12,
              "two-player visibility");
        server.drain(2);
        auto &pa = w.players().at("uuid:" + identity(1).identity),
             &pb = w.players().at("uuid:" + identity(2).identity);
        server.handle(1, changes({{3, 0, 0, {}, {1, 64}}, {0, 0, 0, {}, {1, 64}}}), 1000);
        check(pa.inventory[0] == ItemStack{1, 64}, "creative inventory acquisition");
        server.drain(2);
        server.handle(1, use(0, {1, 3, 0}, {1, 64}), 1100);
        check(w.block({1, 4, 0}) == 1 && pa.inventory[0].count == 64, "creative placement");
        auto updated = server.drain(2);
        check(!updated.empty() && updated[0].header.id == 21, "block broadcast");
        // Breaking follows ItemUseInventoryTransaction::handle and
        // GameMode::destroyBlock: action 13 does not break blocks, creative
        // players break through ItemUse action 2.
        server.handle(1, action(1, 13, {1, 4, 0}), 1200);
        check(w.block({1, 4, 0}) == 1, "PlayerAction 13 is not a break");
        server.drain(2);
        auto creative_break = server.handle(1, use(2, {1, 4, 0}, {1, 64}), 1200);
        check(w.block({1, 4, 0}) == 0 && creative_break.size() == 3 && creative_break[0].header.id == 21 &&
                  creative_break[1].header.id == 25 && creative_break[2].header.id == 36 &&
                  pa.inventory[0].count == 64,
              "creative break: UpdateBlock, LevelEvent 2001, PlayerAction StopBreak, no drop");
        auto seen = server.drain(2);
        check(seen.size() == 2 && seen[0].header.id == 21 && seen[1].header.id == 25,
              "break broadcast to other players");
        // StartBreak broadcasts the crack rate to every player, the sender included:
        // grass on ground = floor(65535 / 0.6 / 30) = 3640 (oracle value).
        Motion ground;
        ground.runtime = 2;
        ground.data = pb;
        ground.grounded = true;
        server.handle(2, packet(PacketId::MovePlayer, motion(ground)), 1000);
        server.drain(1);
        server.drain(2);
        server.handle(2, action(2, 0, {5, 3, 1}), 1000);
        auto crack = server.drain(2);
        ByteWriter expected;
        expected.signed_varint(3600);
        expected.float32(5);
        expected.float32(3);
        expected.float32(1);
        expected.signed_varint(3640);
        check(crack.size() == 1 && crack[0].header.id == 25 && crack[0].body == expected.bytes() &&
                  server.drain(1).size() == 1,
              "StartBreak crack rate broadcast");
        // No destroy-time gate: the break immediately after StartBreak succeeds.
        server.handle(2, use(2, {5, 3, 1}), 1001);
        check(w.block({5, 3, 1}) == 0 && pb.inventory[0] == ItemStack{3, 1},
              "survival break yields dirt without waiting");
        // Reach: Actor::distanceTo(Vec3(BlockPos)) > 12.5 is error 7.
        w.set_block({17, 3, 1}, 1);
        w.set_block({18, 3, 1}, 1);
        auto far = server.handle(2, use(2, {18, 3, 1}, {3, 1}), 1002);
        check(w.block({18, 3, 1}) == 1 && far.size() == 9 && far[0].header.id == 21 && far[8].header.id == 48,
              "out of reach: resendBlocksAroundArea and inventory resend");
        server.handle(2, use(2, {17, 3, 1}, {3, 1}), 1003);
        check(w.block({17, 3, 1}) == 0 && pb.inventory[0] == ItemStack{3, 1},
              "in reach: stone by hand breaks without drop");
        w.set_block({5, 3, 0}, 4);
        server.handle(2, use(2, {5, 3, 0}, {3, 1}), 1004);
        check(w.block({5, 3, 0}) == 0, "bedrock breaks through ItemUse like the original");
        server.handle(2, use(0, {5, 2, 1}, {3, 1}), 1500);
        check(w.block({5, 3, 1}) == 3 && pb.inventory[0].count == 0,
              "survival placement consumes item");
        server.handle(2, changes({{0, 0, 0, {}, {1, 64}}}), 1600);
        check(pb.inventory[0].count == 0, "survival cannot create stacks");
        server.handle(1, changes({{0, 0, 0, {1, 64}, {}}, {0, 0, 124, {}, {1, 64}}}), 1700);
        check(pa.cursor == ItemStack{1, 64} && pa.inventory[0].count == 0,
              "inventory cursor transfer");
        auto saved = WorldStore::decode(WorldStore::encode(w), {});
        check(saved.players().at("uuid:" + identity(1).identity).cursor == ItemStack{1, 64},
              "cursor persistence");
        server.handle(1, changes({{0, 0, 124, {1, 64}, {}}, {0, 5, 0, {}, {1, 64}}}), 1800);
        check(pa.inventory[5] == ItemStack{1, 64} && pa.cursor.count == 0,
              "cursor back to inventory");
        const auto old = pa.inventory;
        server.handle(1, changes({{0, 5, 0, {1, 64}, {}}, {0, 5, 0, {}, {2, 64}}}), 1900);
        check(pa.inventory == old, "duplicate-slot transaction rolls back");
        check(server.handle(1, action(1, 8, {0, -1, 0}), 1950).empty(),
              "unused action target does not correct movement");
        server.drain(2);
        // Movement follows ServerNetworkHandler::handle(MovePlayerPacket): no
        // speed/collision validation, Y clamped to [-128, 512], packet relayed
        // unchanged to other players.
        Motion m;
        m.runtime = 1;
        m.data = pa;
        const float standing_y = pa.y;
        m.data.x = 1.5f;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2000);
        check(pa.x == 1.5f, "valid movement");
        auto moved = server.drain(2);
        check(moved.size() == 1 && moved[0].header.id == 19 && moved[0].body == motion(m),
              "movement relayed unchanged");
        m.data.x = 100;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2000);
        check(pa.x == 100.0f, "large moves are accepted like the original");
        m.data = pa;
        m.data.x = 1.5f;
        m.data.y = 3;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2200);
        check(pa.x == 1.5f && pa.y == 3.0f, "no collision rejection");
        m.data.y = 1000;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2210);
        check(pa.y == 512.0f, "Y clamped to 512");
        m.data.y = std::numeric_limits<float>::quiet_NaN();
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2220);
        check(pa.y == -128.0f, "NaN Y becomes -128 (mce::Math::clamp)");
        server.drain(2);
        m.data.y = 4;
        m.mode = 2;
        m.riding = 77;
        m.teleport_cause = 3;
        m.teleport_source = 9;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2230);
        auto relayed = server.drain(2);
        check(pa.y == 4.0f && relayed.size() == 1 && relayed[0].body == motion(m),
              "teleport mode, riding id and teleport fields relayed");
        m.mode = 0;
        m.riding = 0;
        m.data.x = std::numeric_limits<float>::quiet_NaN();
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2300);
        check(pa.x == 1.5f && server.drain(2).empty(), "non-finite X ignored (documented safety deviation)");
        m.data = pa;
        m.runtime = 2;
        m.data.x = 2;
        check(server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2400).empty() && pa.x == 1.5f,
              "foreign runtime id ignored silently");
        m.runtime = 1;
        for (unsigned i = 0; i < 20; ++i) {
            m.data = pa;
            m.data.y = standing_y; // stand on the surface again before rejoining
            m.data.x += 1;
            server.handle(1, packet(PacketId::MovePlayer, motion(m)), 3000 + i * 200);
        }
        check(server.center(1)->first == 1, "movement crosses chunk boundary");
        server.leave(1);
        auto removed = server.drain(2);
        check(
            std::any_of(removed.begin(), removed.end(), [](auto &p) { return p.header.id == 14; }),
            "disconnect removes actor");
        auto again = make_game(3, 1);
        server.prepare(identity(1), again);
        check(again.x > 16 && pa.inventory[5].count == 64, "rejoin restores state");
        auto final = WorldStore::decode(WorldStore::encode(w), {});
        check(final.block({5, 3, 1}) == 3 &&
                  final.players().at("uuid:" + identity(1).identity).inventory[5].count == 64,
              "world and inventory atomic snapshot");
        auto visitor_game = make_game(4, 1);
        visitor_game.settings.default_player_permission = 0;
        server.prepare(identity(4), visitor_game);
        server.handle(4, packet(PacketId::SetLocalPlayerAsInitialized, {4}), 10000);
        auto &visitor = w.players().at("uuid:" + identity(4).identity);
        visitor.cursor = {3, 1};
        server.handle(4, packet(PacketId::ContainerClose, {124}), 10000);
        check(visitor.cursor.count == 0 && visitor.inventory[0] == ItemStack{3, 1},
              "closing inventory preserves cursor items");
        ByteWriter empty_hand;
        empty_hand.varint64(4);
        item(empty_hand, {});
        empty_hand.byte(255);
        empty_hand.byte(1);
        empty_hand.byte(0);
        server.handle(4, packet(PacketId::MobEquipment, empty_hand.take()), 10000);
        check(visitor.selected == 1, "native empty-hand slot marker");
        server.handle(4, packet(PacketId::PlayerHotbar, {0, 0, 1}), 10000);
        const auto before_block = w.block({1, 4, 0});
        server.handle(4, use(0, {1, 3, 0}, {3, 1}), 10000);
        check(w.block({1, 4, 0}) == before_block && visitor.inventory[0].count == 1,
              "visitor cannot build");
        bool duplicate = false;
        try {
            auto duplicate_game = make_game(5);
            server.prepare(identity(1), duplicate_game);
        } catch (const ProtocolError &) {
            duplicate = true;
        }
        check(duplicate, "duplicate identity rejected");
        std::cout << "terrain, sparse restart, movement, collision, two-player broadcasts, "
                     "building and transactional inventory passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
