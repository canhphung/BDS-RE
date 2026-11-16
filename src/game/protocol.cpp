#include "bds/game/protocol.hpp"
#include "bds/network/spawn.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace bds::game {
namespace {
void require(bool ok) {
    if (!ok)
        throw protocol::ProtocolError("unsupported or malformed gameplay packet");
}
constexpr std::uint16_t legacy[]{0, 1, 2, 3, 7};
} // namespace
GamePacket packet(PacketId id, Bytes body) {
    return {{static_cast<std::uint16_t>(id), 0, 0}, std::move(body)};
}
std::uint32_t fixed(ByteReader &in, unsigned size) {
    std::uint32_t v = 0;
    auto b = in.take(size);
    for (unsigned i = 0; i < size; ++i)
        v |= std::uint32_t(b[i]) << (8 * i);
    return v;
}
float number(ByteReader &in) {
    const auto n = std::bit_cast<float>(fixed(in, 4));
    require(std::isfinite(n));
    return n;
}
World::Position position(ByteReader &in) {
    auto x = in.signed_varint();
    auto y = in.varint();
    auto z = in.signed_varint();
    require(y < 256 && std::abs(std::int64_t(x)) < 30000000 &&
            std::abs(std::int64_t(z)) < 30000000);
    return {x, static_cast<int>(y), z};
}
void position(ByteWriter &out, World::Position p) {
    auto [x, y, z] = p;
    out.signed_varint(x);
    out.varint(static_cast<std::uint32_t>(y));
    out.signed_varint(z);
}
void item(ByteWriter &out, ItemStack stack) {
    require(stack.block < 5 && stack.count <= 64 && (stack.block == 0) == (stack.count == 0));
    out.signed_varint(legacy[stack.block]);
    if (!stack.block)
        return;
    out.signed_varint(stack.count);
    out.little_endian(0, 2);
    out.signed_varint(0);
    out.signed_varint(0);
}
ItemStack item(ByteReader &in) {
    const auto id = in.signed_varint();
    if (id == 0 || id == -1)
        return {};
    const auto aux = in.signed_varint();
    require(aux > 0 && aux <= 64); // zero damage and bounded count
    const auto nbt = fixed(in, 2);
    require(nbt == 0); // supported blocks have no NBT
    for (unsigned list = 0; list < 2; ++list) {
        const auto count = in.signed_varint();
        require(count >= 0 && count <= 32);
        for (int i = 0; i < count; ++i)
            in.string(256);
    }
    for (unsigned i = 1; i < 5; ++i)
        if (id == legacy[i])
            return {static_cast<std::uint16_t>(i), static_cast<std::uint8_t>(aux)};
    throw protocol::ProtocolError("unsupported item");
}
Motion motion(std::span<const std::uint8_t> bytes) {
    ByteReader in(bytes);
    Motion m;
    m.runtime = in.varint64();
    // ReadOnlyBinaryStream::getFloat reads raw IEEE bits; nothing is rejected here.
    const auto raw = [&in] { return std::bit_cast<float>(fixed(in, 4)); };
    m.data.x = raw();
    m.data.y = raw();
    m.data.z = raw();
    m.data.pitch = raw();
    m.data.yaw = raw();
    m.data.head_yaw = raw();
    m.mode = in.byte();
    m.grounded = in.boolean();
    m.riding = in.varint64();
    if (m.mode == 2) {
        m.teleport_cause = static_cast<std::int32_t>(fixed(in, 4));
        m.teleport_source = static_cast<std::int32_t>(fixed(in, 4));
    }
    in.finish();
    return m;
}
Bytes motion(const Motion &m) {
    ByteWriter out;
    out.varint64(m.runtime);
    out.float32(m.data.x);
    out.float32(m.data.y);
    out.float32(m.data.z);
    out.float32(m.data.pitch);
    out.float32(m.data.yaw);
    out.float32(m.data.head_yaw);
    out.byte(m.mode);
    out.boolean(m.grounded);
    out.varint64(m.riding);
    if (m.mode == 2) {
        out.little_endian(static_cast<std::uint32_t>(m.teleport_cause), 4);
        out.little_endian(static_cast<std::uint32_t>(m.teleport_source), 4);
    }
    return out.take();
}
Bytes inventory(const PlayerData &p) {
    ByteWriter out;
    out.varint(0);
    out.varint(36);
    for (auto s : p.inventory)
        item(out, s);
    return out.take();
}
Bytes cursor_inventory(const PlayerData &p) {
    ByteWriter out;
    out.varint(124);
    out.varint(1);
    item(out, p.cursor);
    return out.take();
}
Bytes creative_inventory() {
    ByteWriter out;
    out.varint(121);
    out.varint(4);
    for (unsigned i = 1; i < 5; ++i)
        item(out, {static_cast<std::uint16_t>(i), 1});
    return out.take();
}
namespace {
// Metadata of a freshly spawned ItemActor exactly as the original sends it
// (SetActorData and AddItemActor in tests/reference/break_bds_1.6.0.15.json):
// key, type (0 byte, 1 short, 2 int, 3 float, 4 string, 7 long, 8 vec3), value.
void write_item_metadata(ByteWriter &out) {
    struct Entry {
        unsigned key, type;
        std::int64_t value;
        float real;
    };
    static const Entry entries[] = {
        {0, 7, 0xC00000000000LL, 0}, // flags: HAS_COLLISION (46), AFFECTED_BY_GRAVITY (47)
        {1, 2, 1, 0}, {2, 2, 0, 0}, {3, 0, 0, 0}, {4, 4, 0, 0}, {5, 7, -1, 0}, {7, 1, 300, 0},
        {22, 0, 0, 0}, {37, 7, 0, 0}, {38, 3, 0, 1.0f}, {42, 1, 300, 0}, {43, 2, 0, 0}, {44, 0, 0, 0},
        {45, 2, 0, 0}, {46, 2, 0, 0}, {53, 3, 0, 0.25f}, {54, 3, 0, 0.25f}, {56, 8, 0, 0}, {57, 0, 0, 0},
        {58, 3, 0, 0.0f}, {59, 3, 0, 0.0f}, {69, 0, 0, 0}, {70, 4, 0, 0}, {71, 4, 0, 0}, {72, 0, 1, 0},
        {73, 0, 0, 0}, {74, 2, 0, 0}, {75, 2, 0, 0}, {76, 2, 0, 0}, {77, 2, -1, 0}, {80, 0, 255, 0},
        {81, 0, 0, 0}};
    out.varint(static_cast<std::uint32_t>(std::size(entries)));
    for (const auto &e : entries) {
        out.varint(e.key);
        out.varint(e.type);
        switch (e.type) {
        case 0: out.byte(static_cast<std::uint8_t>(e.value)); break;
        case 1: out.little_endian(static_cast<std::uint16_t>(e.value), 2); break;
        case 2: out.signed_varint(static_cast<std::int32_t>(e.value)); break;
        case 3: out.float32(e.real); break;
        case 4: out.varint(0); break;
        case 7: out.signed_varint64(e.value); break;
        case 8: out.float32(0); out.float32(0); out.float32(0); break;
        }
    }
}
} // namespace
Bytes block_update(World::Position p, BlockId id) {
    const auto &palette = protocol::internal_block_palette();
    require(id < palette.size());
    ByteWriter out;
    position(out, p);
    out.varint(protocol::runtime_id(palette[id].name, palette[id].data).value());
    out.varint(3);
    out.varint(0);
    return out.take();
}
Bytes equipment(std::uint64_t runtime, const PlayerData &p) {
    ByteWriter out;
    out.varint64(runtime);
    item(out, p.inventory[p.selected]);
    out.byte(static_cast<std::uint8_t>(p.selected));
    out.byte(static_cast<std::uint8_t>(p.selected));
    out.byte(0);
    return out.take();
}
Bytes remove_player_list(const auth::Identity &identity) {
    const auto profile = protocol::player_list_body(identity, 1);
    ByteReader in(profile);
    in.byte();
    in.varint();
    ByteWriter out;
    out.byte(1);
    out.varint(1);
    out.raw(in.take(16));
    return out.take();
}
Bytes add_player(const auth::Identity &identity, const protocol::StartGame &game,
                 const PlayerData &p) {
    const auto profile = protocol::player_list_body(identity, game.unique_id);
    ByteReader in(profile);
    in.byte();
    in.varint();
    ByteWriter out;
    out.raw(in.take(16));
    in.varint64();
    out.string(in.string(1024));
    out.string(in.string(1024));
    out.signed_varint(in.signed_varint());
    out.signed_varint64(game.unique_id);
    out.varint64(game.runtime_id);
    out.string("");
    out.float32(p.x);
    out.float32(p.y);
    out.float32(p.z);
    for (unsigned i = 0; i < 3; ++i)
        out.float32(0);
    out.float32(p.pitch);
    out.float32(p.yaw);
    out.float32(p.head_yaw);
    item(out, p.inventory[p.selected]);
    out.varint(0);
    out.raw(protocol::adventure_settings_body(game));
    out.varint(0);
    out.string("");
    return out.take();
}
Transaction transaction(std::span<const std::uint8_t> bytes) {
    ByteReader in(bytes);
    Transaction t;
    t.type = in.varint();
    require(t.type <= 2);
    const auto count = in.varint();
    require(count <= 128);
    for (unsigned i = 0; i < count; ++i) {
        Action a;
        a.source = in.varint();
        if (a.source == 0 || a.source == 99999)
            a.window = in.signed_varint();
        else if (a.source == 2)
            in.varint();
        else
            require(a.source == 3);
        a.slot = in.varint();
        a.before = item(in);
        a.after = item(in);
        t.changes.push_back(a);
    }
    if (t.type == 2) {
        t.action = in.varint();
        require(t.action <= 2);
        t.block = position(in);
        t.face = in.signed_varint();
        t.slot = in.signed_varint();
        t.held = item(in);
        for (unsigned i = 0; i < 6; ++i)
            number(in);
    }
    in.finish();
    return t;
}
Bytes item_actor_data(std::uint64_t runtime) {
    ByteWriter out;
    out.varint64(runtime);
    write_item_metadata(out);
    return out.take();
}
Bytes add_item_actor(const ItemActor &a) {
    ByteWriter out;
    out.signed_varint64(a.unique_id);
    out.varint64(a.runtime_id);
    item(out, a.item);
    for (float v : {a.sent_pos.x, a.sent_pos.y, a.sent_pos.z, a.motion.x, a.motion.y, a.motion.z})
        out.float32(v);
    write_item_metadata(out);
    out.boolean(false); // isFromFishing
    return out.take();
}
Bytes move_actor_delta(std::uint64_t runtime, std::uint8_t flags, const std::int32_t delta[3]) {
    ByteWriter out;
    out.varint64(runtime);
    out.byte(flags);
    for (int i = 0; i < 3; ++i)
        if (flags & (1U << i))
            out.signed_varint(delta[i]);
    return out.take();
}
Bytes set_actor_motion(std::uint64_t runtime, Vec3 motion) {
    ByteWriter out;
    out.varint64(runtime);
    out.float32(motion.x);
    out.float32(motion.y);
    out.float32(motion.z);
    return out.take();
}
Bytes take_item_actor(std::uint64_t item_runtime, std::uint64_t player) {
    ByteWriter out;
    out.varint64(item_runtime);
    out.varint64(player);
    return out.take();
}
Bytes remove_actor(std::int64_t unique) {
    ByteWriter out;
    out.signed_varint64(unique);
    return out.take();
}
Bytes actor_event(std::uint64_t runtime, std::uint8_t event, std::int32_t data) {
    ByteWriter out;
    out.varint64(runtime);
    out.byte(event);
    out.signed_varint(data);
    return out.take();
}
Bytes pickup_transaction(const std::vector<SlotChange> &slots, ItemStack taken, ItemStack left) {
    ByteWriter out;
    out.varint(0); // normal transaction
    out.varint(static_cast<std::uint32_t>(slots.size() + 1));
    out.varint(2); // InventorySource::fromWorldInteraction
    out.varint(0); // flags
    out.varint(1); // slot argument of InventoryAction in Player::take
    item(out, taken);
    item(out, left);
    for (const auto &c : slots) {
        out.varint(0);        // InventorySource container
        out.signed_varint(0); // ContainerID inventory
        out.varint(c.slot);
        item(out, c.before);
        item(out, c.after);
    }
    return out.take();
}
std::vector<GamePacket> replicate(ItemActor &a) {
    std::vector<GamePacket> out;
    // Actor::sendMotionPacketIfNeeded (0x21bb110): SpatialActorNetworkData::
    // shouldSendUpdate -> MoveActorAbsoluteData::isDifferenceSignificant
    // (0x1919f70); rotation never changes for an item.
    const Vec3 d{a.pos.x - a.sent_pos.x, a.pos.y - a.sent_pos.y, a.pos.z - a.sent_pos.z};
    const float moved2 = d.x * d.x + d.y * d.y + d.z * d.z;
    const float speed2 = a.motion.x * a.motion.x + a.motion.y * a.motion.y + a.motion.z * a.motion.z;
    // Thresholds 0x28275a0 (1e-4f) and 0x2827598 (0x358637be, one ulp above 1e-6f).
    const float tiny = std::bit_cast<float>(0x358637beU);
    if (moved2 > 1e-4f || (tiny > speed2 && moved2 > tiny)) {
        // calculateDelta (0x191a250): int differences of the float bits; the
        // runtime id and the onGround/teleported header are copied from the
        // previously sent data, so a landing is flagged one packet later.
        const std::int32_t delta[3] = {
            static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a.pos.x) - std::bit_cast<std::uint32_t>(a.sent_pos.x)),
            static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a.pos.y) - std::bit_cast<std::uint32_t>(a.sent_pos.y)),
            static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a.pos.z) - std::bit_cast<std::uint32_t>(a.sent_pos.z))};
        std::uint8_t flags = a.sent_on_ground ? 0x40 : 0;
        for (int i = 0; i < 3; ++i)
            if (delta[i] != 0)
                flags |= static_cast<std::uint8_t>(1U << i);
        out.push_back(packet(PacketId::MoveActorDelta, move_actor_delta(a.runtime_id, flags, delta)));
        a.sent_pos = a.pos;
        a.sent_on_ground = a.on_ground;
    }
    // SetActorMotion when the motion changed by more than 0.2 since the last
    // one, or stopped. The last sent motion (+0x110) starts at zero (INFERRED).
    const Vec3 dm{a.motion.x - a.sent_motion.x, a.motion.y - a.sent_motion.y, a.motion.z - a.sent_motion.z};
    const float changed2 = dm.x * dm.x + dm.y * dm.y + dm.z * dm.z;
    if (changed2 > 0.04f || (changed2 > 0.0f && a.motion.x == 0.0f && a.motion.y == 0.0f && a.motion.z == 0.0f)) {
        a.sent_motion = a.motion;
        out.push_back(packet(PacketId::SetActorMotion, set_actor_motion(a.runtime_id, a.motion)));
    }
    return out;
}
} // namespace bds::game
