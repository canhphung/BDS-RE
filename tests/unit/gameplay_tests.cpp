#include "bds/game/server.hpp"
#include "bds/world/store.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <set>
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
GamePacket use_from(World::Position p, ItemStack held, float fx, float fy, float fz) {
    ByteWriter w;
    w.varint(2);
    w.varint(0);
    w.varint(0); // click block
    position(w, p);
    w.signed_varint(1);
    w.signed_varint(0);
    item(w, held);
    w.float32(fx);
    w.float32(fy);
    w.float32(fz);
    for (int i = 0; i < 3; ++i)
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
        {
            // 57 offsets dumped from the original Level (tick-distance 4).
            const std::set<std::pair<int, int>> original{
                {-4, -1}, {-1, 4}, {4, 1}, {-2, -2}, {4, 0}, {2, -3}, {2, 3}, {0, -1}, {1, 1}, {-1, 1},
                {1, -3}, {0, 0}, {0, -4}, {-1, 3}, {2, 1}, {-4, 1}, {-1, -1}, {-3, 2}, {-3, 0}, {4, -1},
                {-3, -1}, {1, 0}, {3, 2}, {1, 3}, {-2, -1}, {-3, 1}, {-2, -3}, {3, 0}, {0, -2}, {-1, -2},
                {3, 1}, {1, -4}, {-4, 0}, {3, -1}, {2, 0}, {1, -2}, {-2, 1}, {-2, 2}, {1, 2}, {-2, 0},
                {-3, -2}, {-1, -3}, {2, -1}, {-1, 0}, {0, 4}, {1, 4}, {0, 2}, {0, 1}, {2, -2}, {2, 2},
                {0, -3}, {3, -2}, {-2, 3}, {0, 3}, {1, -1}, {-1, 2}, {-1, -4}};
            const auto offsets = client_ticking_offsets(4);
            check(offsets.size() == 57 &&
                      std::set<std::pair<int, int>>(offsets.begin(), offsets.end()) == original,
                  "client ticking offsets equal the original's");
        }
        terrain();
        ByteWriter simple;
        item(simple, {1, 1});
        check(simple.bytes() == Bytes({2, 2, 0, 0, 0, 0}), "original legacy stone item encoding");
        World w;
        w.configure_terrain({1, 123});
        // An already finalized area (LevelChunk +0x1054 = 2), as in a world
        // that has run before: no first-tick or post-processing ticks here.
        for (int cx = -5; cx <= 5; ++cx)
            for (int cz = -5; cz <= 5; ++cz)
                w.ensure_chunk(cx, cz);
        Properties properties;
        AccessLists access(".");
        Server server(w, properties, access);
        for (int i = 0; i < 10; ++i)
            server.allocate_entity_id(); // test players use runtime ids 1..5
        auto a = make_game(1, 1), b = make_game(2);
        server.prepare(identity(1), a);
        server.prepare(identity(2), b);
        server.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
        server.handle(2, packet(PacketId::SetLocalPlayerAsInitialized, {2}), 0);
        const auto joined = server.drain(1);
        check(joined.size() == 1 && joined[0].header.id == 63, "player list on initialization");
        server.drain(2);
        // ServerPlayer::_updateNearbyEntities at the next tick: AddPlayer and the
        // carried item (MobEquipment) of every player within the ticking offsets.
        server.tick();
        const auto visible = server.drain(1);
        const auto ids = [](const std::vector<GamePacket> &v) {
            std::vector<unsigned> out;
            for (const auto &q : v)
                out.push_back(q.header.id);
            return out;
        };
        const auto seen_ids = ids(visible);
        check(std::count(seen_ids.begin(), seen_ids.end(), 12u) == 1 &&
                  std::count(seen_ids.begin(), seen_ids.end(), 32u) == 1 &&
                  std::count(seen_ids.begin(), seen_ids.end(), 31u) == 0,
              "two-player visibility: AddPlayer, MobArmorEquipment, no MobEquipment for an empty hand");
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
        const auto other = server.drain(1);
        check(crack.size() == 1 && crack[0].header.id == 25 && crack[0].body == expected.bytes() &&
                  other.size() == 2 && other[0].header.id == 25 && other[1].header.id == 36 &&
                  other[1].body == action(2, 0, {5, 3, 1}).body,
              "StartBreak crack rate broadcast, PlayerAction relayed to the other player");
        // No destroy-time gate: the break immediately after StartBreak succeeds.
        server.handle(2, use(2, {5, 3, 1}), 1001);
        check(w.block({5, 3, 1}) == 0 && pb.inventory[0].count == 0 && server.items().size() == 1 &&
                  server.items()[0].item == ItemStack{3, 1} && server.items()[0].pickup_delay == 10,
              "survival break drops a dirt item entity (popResource)");
        server.drain(1);
        server.drain(2);
        // First tick: actor data and AddItemActor; the pickup delay of 10 ticks
        // then expires and player 2, standing on the hole, takes the item.
        server.tick();
        // The chunk actor and player 2 tick before player 1 in this shuffle:
        // player 2 announces the new item before player 1 flushes its attributes.
        // See chunk_order_reference for the original per-player scheduler trace.
        auto added = server.drain(1);
        check(added.size() == 3 && added[0].header.id == 0x27 && added[1].header.id == 0x0f &&
                  added[2].header.id == 0x1d,
              "item entity added after its first tick");
        // Player::aiStep touches entities within the player box grown by
        // (1, 0.5, 1): player 2 steps into the hole the item fell into.
        Motion into_hole;
        into_hole.runtime = 2;
        into_hole.data = pb;
        into_hole.data.y = 4.62f;
        into_hole.data.z = 1.5f;
        into_hole.grounded = true;
        server.handle(2, packet(PacketId::MovePlayer, motion(into_hole)), 1001);
        std::vector<GamePacket> seen_by_taker;
        for (int i = 0; i < 12; ++i) {
            server.tick();
            for (auto &q : server.drain(2))
                seen_by_taker.push_back(q);
        }
        const auto has = [&](unsigned id) {
            return std::any_of(seen_by_taker.begin(), seen_by_taker.end(),
                               [&](auto &q) { return q.header.id == id; });
        };
        check(pb.inventory[0] == ItemStack{3, 1} && server.items().empty() && has(0x1e) && has(0x11) &&
                  has(0x0e) && has(0x6f),
              "pickup: MoveActorDelta, InventoryTransaction, TakeItemActor, RemoveActor");
        server.drain(1);
        into_hole.data.y = 5.62f;
        into_hole.data.z = 0.5f;
        server.handle(2, packet(PacketId::MovePlayer, motion(into_hole)), 1001);
        for (int i = 0; i < 3; ++i)
            server.tick();
        check(pb.y == 5.62f && pb.z == 0.5f, "back on the surface after interpolation");
        server.drain(1);
        server.drain(2);
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
        // The server position follows through Mob::lerpTo: three ticks unless
        // the player just landed (one tick).
        const auto settle = [&] {
            for (int i = 0; i < 3; ++i)
                server.tick();
        };
        Motion m;
        m.runtime = 1;
        m.data = pa;
        const float standing_y = pa.y;
        const float start_x = pa.x;
        m.data.x = 3.5f;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2000);
        check(pa.x == start_x, "position is not applied by the packet itself");
        auto moved = server.drain(2);
        check(moved.size() == 1 && moved[0].header.id == 19 && moved[0].body == motion(m),
              "movement relayed unchanged");
        server.tick();
        check(pa.x == start_x + (3.5f - start_x) * (1.0f / 3.0f), "first interpolation step");
        server.tick();
        server.tick();
        check(pa.x == 3.5f, "valid movement");
        m.data.x = 40;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2000);
        settle();
        check(pa.x == 40.0f, "large moves are accepted like the original");
        m.data = pa;
        m.data.x = 1.5f;
        m.data.y = 3;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2200);
        settle();
        check(pa.x == 1.5f && pa.y == 3.0f, "no collision rejection");
        m.data.y = 1000;
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2210);
        settle();
        check(pa.y == 512.0f, "Y clamped to 512");
        m.data.y = std::numeric_limits<float>::quiet_NaN();
        server.handle(1, packet(PacketId::MovePlayer, motion(m)), 2220);
        settle();
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
              "teleport mode sets the position at once; riding id and teleport fields relayed");
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
            settle();
        }
        check(server.center(1)->first == 1, "movement crosses chunk boundary");
        server.drain(2);
        server.leave(1);
        server.tick();
        std::vector<GamePacket> removed;
        // drain() is a bounded network batch (64 packets). Walking mobs can
        // enqueue additional movement before the disconnect notification.
        for (;;) {
            auto batch=server.drain(2);
            if(batch.empty())break;
            removed.insert(removed.end(),batch.begin(),batch.end());
        }
        check(std::any_of(removed.begin(),removed.end(),[](const auto &packet) {
            if(packet.header.id!=14)return false;
            ByteReader body(packet.body);
            const auto encoded=body.varint64();body.finish();
            const auto id=(encoded>>1)^(0ULL-(encoded&1));
            return id==1;
        }),"disconnect removes the departing player across bounded batches");
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
        {
            // MovementInterpolator against the original (gdb trace of
            // ServerPlayer::normalTick): a grounded move right after spawn takes
            // one step (landing), the next one three: 0.5, 10.5, 20.5, 30.5.
            World lw;
            lw.configure_terrain({1, 123});
            Server ls(lw, properties, access);
            auto g = make_game(1);
            ls.prepare(identity(7), g);
            ls.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
            auto &lp = lw.players().at("uuid:" + identity(7).identity);
            Motion lm;
            lm.runtime = 1;
            lm.data = lp;
            lm.data.x = 0.5f;
            lm.data.y = 5.62f;
            lm.data.z = 0.5f;
            lm.grounded = true;
            ls.handle(1, packet(PacketId::MovePlayer, motion(lm)), 0);
            ls.tick();
            check(lp.x == 0.5f && lp.y == 5.62f, "landing move in one step");
            lm.data.x = 30.5f;
            ls.handle(1, packet(PacketId::MovePlayer, motion(lm)), 0);
            const float expected[] = {10.5f, 20.5f, 30.5f};
            for (float e : expected) {
                ls.tick();
                check(lp.x == e, "interpolation steps equal the original's");
            }
        }
        {
            // Pillar building: the click-block branch checks the acting player at
            // the transaction's fromPosition (ItemUseInventoryTransaction $_2
            // 0x1ae6f67), not at the lagging interpolated server position.
            World pw;
            pw.configure_terrain({1, 123});
            Server ps(pw, properties, access);
            auto g = make_game(1, 1);
            ps.prepare(identity(8), g);
            ps.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
            auto &pp = pw.players().at("uuid:" + identity(8).identity);
            pp.x = 0.5f;
            pp.y = 5.62f; // feet at 4: the server still sees the player in the target block
            pp.z = 0.5f;
            pp.inventory[0] = {3, 64};
            ps.handle(1, use_from({0, 3, 0}, {3, 64}, 0.5f, 5.62f, 0.5f), 0);
            check(pw.block({0, 4, 0}) == 0, "placement into the player's own block is refused");
            ps.handle(1, use_from({0, 3, 0}, {3, 64}, 0.5f, 6.82f, 0.5f), 0);
            check(pw.block({0, 4, 0}) == 3, "pillar placement uses the client's fromPosition");
        }
        {
            // Hunger, regeneration, fall damage, death and respawn, byte for byte
            // with the original (tests/reference/hunger_health_bds_1.6.0.15.json:
            // survival, difficulty normal, runtime id 1, standing still).
            const auto hex = [](const Bytes &b) {
                std::string out;
                for (auto c : b) {
                    out += "0123456789abcdef"[c >> 4];
                    out += "0123456789abcdef"[c & 15];
                }
                return out;
            };
            const auto fall = [](float distance) {
                ByteWriter f;
                f.varint64(1);
                f.float32(distance);
                f.boolean(false);
                return packet(PacketId::ActorFall, f.take());
            };
            World hw;
            hw.configure_terrain({1, 123});
            Server hs(hw, properties, access);
            auto g = make_game(1, 0);
            hs.prepare(identity(9), g);
            hs.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
            hs.tick();
            hs.drain(1);
            // ActorFallPacket 19.8: Mob::causeFallDamage ceil(19.8 - 3) = 17.
            hs.handle(1, fall(19.8f), 0);
            auto hurt = hs.drain(1);
            // LevelSoundEvent FALL_BIG, the hurt event, the block-fall sound.
            check(hurt.size() == 3 && hurt[0].header.id == 0x18 && hurt[1].header.id == 0x1b &&
                      hex(hurt[1].body) == "01020a" && hurt[2].header.id == 0x18,
                  "fall hurt ActorEvent (cause FALL)");
            std::vector<std::string> attributes;
            for (int t = 0; t < 200 && attributes.size() < 3; ++t) {
                hs.tick();
                for (auto &out : hs.drain(1))
                    if (out.header.id == 0x1d)
                        attributes.push_back(hex(out.body));
            }
            check(attributes.size() == 3 &&
                      attributes[0] == "0101000000000000a041000040400000a041106d696e6563726166743a6865616c7468",
                  "health 3 after the fall");
            check(attributes[1] == "0102000000000000a041000080400000a041106d696e6563726166743a6865616c7468000000000000804000004040000000001b6d696e6563726166743a706c617965722e65786861757374696f6e",
                  "natural regeneration: health +1, exhaustion +3");
            check(attributes[2].rfind("0105000000000000a0410000a0400000a041106d696e6563726166743a6865616c7468000000000000804000000000000000001b6d696e6563726166743a706c617965722e65786861757374696f6e", 0) == 0,
                  "exhaustion overflow drains saturation");
            for (int t = 0; t < 10; ++t)
                hs.tick();
            hs.drain(1);
            // A 40-block fall kills: drops, inventories, death event, message, Respawn.
            hs.handle(1, fall(40.0f), 0);
            hs.tick();
            const auto died = hs.drain(1);
            std::vector<unsigned> ids;
            for (auto &out : died)
                ids.push_back(out.header.id);
            const auto at = [&](unsigned id) {
                auto it = std::find_if(died.begin(), died.end(), [&](auto &o) { return o.header.id == id; });
                check(it != died.end(), "death packet present");
                return hex(it->body);
            };
            check(at(0x1b) == "010300", "ActorEvent DEATH");
            check(at(0x2d) == "0000003f20d7b3400000003f", "Respawn at the spawn point (recheckSpawnPosition offset)");
            check(at(0x1d).rfind("0101000000000000a041000000000000a041106d696e6563726166743a6865616c7468", 0) == 0,
                  "health 0 after death");
            check(at(0x27) == "01023503cdcc4c3e3603cdcc4c3e", "dead bounding box");
            check(std::count(ids.begin(), ids.end(), 0x31u) == 12 && std::count(ids.begin(), ids.end(), 0x30u) == 3,
                  "inventories sent three times on death");
            // PlayerAction 7 (respawn).
            hs.handle(1, action(1, 7, {0, 0, 0}), 0);
            auto respawned = hs.drain(1);
            std::vector<std::string> bodies;
            for (auto &out : respawned)
                if (out.header.id != 0x0e)
                    bodies.push_back(hex(out.body));
            check(bodies.size() == 4 && bodies[0] == "011200" && bodies[1] == "28" &&
                      bodies[2].rfind("0106000000000000a0410000a0410000a041106d696e6563726166743a6865616c7468", 0) == 0 &&
                      bodies[3] == "010300078080c28080816035039a99193f36036666e63f",
                  "respawn ActorEvent, SetHealth, attributes and actor data");
        }
        {
            // HungerAttributeDelegate::tick (0x24a2a10) starvation on normal
            // difficulty: food 0 and health > 10 lose 1 health every 80 ticks
            // (buff type STARVE -> hurtEffects cause STARVE 16).
            World sw;
            sw.configure_terrain({1, 123});
            Server ss(sw, properties, access);
            PlayerData starving;
            starving.hunger = 0;
            starving.saturation = 0;
            sw.players()["uuid:" + identity(10).identity] = starving;
            auto g = make_game(1, 0);
            ss.prepare(identity(10), g);
            auto &sp = sw.players().at("uuid:" + identity(10).identity);
            ss.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
            int hurts = 0;
            for (int t = 0; t < 161; ++t) {
                ss.tick();
                for (auto &out : ss.drain(1))
                    if (out.header.id == 0x1b && out.body == Bytes({1, 2, 0x20}))
                        ++hurts;
            }
            check(hurts == 2 && sp.health == 18, "starvation: -1 health per 80 ticks above 10 health");
        }
        {
            // The player's own first SetActorData packets at spawn, byte for byte
            // with the original join capture (runtime id 1, name "Reference").
            const auto hex = [](const Bytes &b) {
                std::string out;
                for (auto c : b) {
                    out += "0123456789abcdef"[c >> 4];
                    out += "0123456789abcdef"[c & 15];
                }
                return out;
            };
            check(hex(player_data(1, "Reference", 0xc00000080000ULL)) == "012800078080c0808080600102020202000300000404095265666572656e636505070107012c010802000900000a00001600001a00001b02021c0600000025070026030000803f2704002a012c012b02002c00002d02002e02003503cdcc4c3f36036666e63f38080000000000000000000000003900003a03000000003b03000000004500004604004704004800014900004a02004b02004c02004d0201500001510000530400", "spawn metadata bytes");
            check(hex(actor_flags(1, 0xc00400080000ULL)) == "010100078080c080808160", "spawn flags bytes");
        }
        std::cout << "terrain, sparse restart, movement, collision, two-player broadcasts, "
                     "building and transactional inventory passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
