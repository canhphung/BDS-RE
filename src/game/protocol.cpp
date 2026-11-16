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
} // namespace bds::game
