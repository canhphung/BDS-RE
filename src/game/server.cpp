#include "bds/game/server.hpp"
#include "bds/network/spawn.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace bds::game {
namespace {
void require(bool ok) {
    if (!ok)
        throw protocol::ProtocolError("invalid gameplay request");
}
std::string key(const auth::Identity &i) {
    return i.xbox_authenticated ? "xuid:" + i.xuid : "uuid:" + i.identity;
}
bool overlap(const PlayerData &p, World::Position block) {
    auto [x, y, z] = block;
    const auto feet = p.y - 1.62f;
    return p.x + .3f > x && p.x - .3f < x + 1 && p.z + .3f > z && p.z - .3f < z + 1 &&
           feet + 1.8f > y && feet < y + 1;
}
} // namespace
void Server::enqueue(std::uint64_t id, GamePacket packet) {
    auto i = sessions_.find(id);
    if (i == sessions_.end())
        return;
    auto &s = i->second;
    if (packet.header.id == static_cast<unsigned>(PacketId::MovePlayer)) {
        ByteReader incoming(packet.body);
        const auto actor = incoming.varint64();
        for (auto &old : s.queue)
            if (old.header.id == packet.header.id) {
                ByteReader reader(old.body);
                if (reader.varint64() == actor) {
                    old = std::move(packet);
                    return;
                }
            }
    }
    if (s.queue.size() >= 128) {
        s.overflow = true;
        return;
    }
    s.queue.push_back(std::move(packet));
}
void Server::broadcast(GamePacket packet, std::uint64_t exclude) {
    for (auto &[id, s] : sessions_)
        if (s.active && id != exclude)
            enqueue(id, packet);
}
std::vector<GamePacket> Server::drain(std::uint64_t id) {
    auto i = sessions_.find(id);
    if (i == sessions_.end())
        return {};
    auto &s = i->second;
    require(!s.overflow);
    std::vector<GamePacket> out;
    std::size_t bytes = 0;
    while (!s.queue.empty() && out.size() < 16 &&
           (out.empty() || bytes + s.queue.front().body.size() < 65536)) {
        bytes += s.queue.front().body.size();
        out.push_back(std::move(s.queue.front()));
        s.queue.pop_front();
    }
    return out;
}
std::optional<std::pair<int, int>> Server::center(std::uint64_t id) const {
    auto i = sessions_.find(id);
    if (i == sessions_.end())
        return {};
    const auto &p = world_.players().at(i->second.key);
    return std::pair{chunk_coordinate(static_cast<int>(std::floor(p.x))),
                     chunk_coordinate(static_cast<int>(std::floor(p.z)))};
}
bool Server::collides(const PlayerData &p) const {
    const auto feet = p.y - 1.62f;
    for (int x = static_cast<int>(std::floor(p.x - .299f));
         x <= static_cast<int>(std::floor(p.x + .299f)); ++x)
        for (int z = static_cast<int>(std::floor(p.z - .299f));
             z <= static_cast<int>(std::floor(p.z + .299f)); ++z)
            for (int y = static_cast<int>(std::floor(feet + .01f));
                 y <= static_cast<int>(std::floor(feet + 1.79f)); ++y)
                if (world_.block({x, y, z}))
                    return true;
    return false;
}
bool Server::reach(const PlayerData &p, World::Position b) const {
    auto [x, y, z] = b;
    double dx = p.x - (x + .5), dy = p.y - (y + .5), dz = p.z - (z + .5);
    return dx * dx + dy * dy + dz * dz <= 36;
}
std::vector<GamePacket> Server::correct(Session &s) {
    const auto &p = world_.players().at(s.key);
    return {packet(PacketId::MovePlayer, motion({s.game.runtime_id, p, 1, false})),
            packet(PacketId::InventoryContent, inventory(p)),
            packet(PacketId::InventoryContent, cursor_inventory(p))};
}
std::vector<GamePacket> Server::reject_item_use(const Session &s, const Transaction &t) const {
    // ItemUseInventoryTransaction::onTransactionError (original 0x1ae7710):
    // resendBlocksAroundArea, then inventory/hotbar correction. The four updates
    // intentionally retain duplicates when the clicked face is vertical.
    auto [x, y, z] = t.block;
    const auto face = static_cast<std::int8_t>(t.face);
    World::Position adjacent{x + (face == 5) - (face == 4), y + (face == 1) - (face == 0),
                             z + (face == 3) - (face == 2)};
    auto [ax, ay, az] = adjacent;
    std::vector<GamePacket> out;
    for (const auto &pos :
         {t.block, World::Position{x, y + 1, z}, adjacent, World::Position{ax, ay + 1, az}})
        out.push_back(packet(PacketId::UpdateBlock, block_update(pos, world_.block(pos))));
    const auto &p = world_.players().at(s.key);
    out.push_back(packet(PacketId::InventoryContent, inventory(p)));
    // Only block items are currently supported; armor and offhand stay empty.
    out.push_back(packet(PacketId::InventoryContent, protocol::empty_inventory_body(120, 4)));
    out.push_back(packet(PacketId::InventoryContent, cursor_inventory(p)));
    out.push_back(packet(PacketId::InventoryContent, protocol::empty_inventory_body(119, 1)));
    ByteWriter hotbar;
    hotbar.varint(p.selected);
    hotbar.byte(0);
    hotbar.boolean(true);
    out.push_back(packet(PacketId::PlayerHotbar, hotbar.take()));
    return out;
}
std::vector<GamePacket> Server::prepare(const auth::Identity &identity, protocol::StartGame &g) {
    const auto k = key(identity);
    require(sessions_.size() < properties_.max_players);
    for (const auto &[id, s] : sessions_)
        require(s.key != k);
    require(world_.players().contains(k) || world_.players().size() < 1024);
    PlayerData initial;
    initial.x = g.x;
    initial.y = g.y;
    initial.z = g.z;
    auto &p = world_.players().try_emplace(k, initial).first->second;
    if (collides(p) || p.y < 1) {
        p.x = g.x;
        p.y = g.y;
        p.z = g.z;
    }
    g.x = p.x;
    g.y = p.y;
    g.z = p.z;
    g.pitch = p.pitch;
    g.yaw = p.yaw;
    if (identity.xbox_authenticated && access_.is_operator(identity.xuid))
        g.settings.default_player_permission = 2;
    Session s;
    s.identity = identity;
    s.game = g;
    s.key = k;
    sessions_.emplace(g.runtime_id, std::move(s));
    ByteWriter hotbar;
    hotbar.varint(p.selected);
    hotbar.byte(0);
    hotbar.boolean(true);
    return {packet(PacketId::InventoryContent, inventory(p)),
            packet(PacketId::InventoryContent, creative_inventory()),
            packet(PacketId::InventoryContent, cursor_inventory(p)),
            packet(PacketId::PlayerHotbar, hotbar.take())};
}
void Server::leave(std::uint64_t runtime) {
    auto i = sessions_.find(runtime);
    if (i == sessions_.end())
        return;
    if (i->second.active) {
        ByteWriter removed;
        removed.signed_varint64(i->second.game.unique_id);
        broadcast(packet(PacketId::RemoveActor, removed.take()), runtime);
        broadcast(packet(PacketId::PlayerList, remove_player_list(i->second.identity)), runtime);
    }
    sessions_.erase(i);
}
std::vector<GamePacket> Server::destroy(Session &s, World::Position b, std::uint64_t now) {
    auto &p = world_.players().at(s.key);
    auto id = world_.block(b);
    const auto restore = [&] {
        std::vector<GamePacket> out{packet(PacketId::InventoryContent, inventory(p)),
                                    packet(PacketId::InventoryContent, cursor_inventory(p))};
        if (id < 5)
            out.push_back(packet(PacketId::UpdateBlock, block_update(b, id)));
        return out;
    };
    if (!id || id > 4 || !reach(p, b) || s.game.player_game_type == 2 ||
        s.game.settings.default_player_permission == 0)
        return restore();
    auto next = p.inventory;
    if (s.game.player_game_type != 1) {
        const auto delay = id == 1 ? 1000U : 300U;
        if (id == 4 || !s.breaking || *s.breaking != b || now < s.break_time ||
            now - s.break_time < delay)
            return restore();
        const auto drop = static_cast<std::uint16_t>(id == 1 ? 0 : id == 2 ? 3 : id);
        bool stored = drop == 0; // bare-hand stone mining yields no item

        for (auto &stack : next)
            if (!stored && stack.block == drop && stack.count < 64) {
                ++stack.count;
                stored = true;
                break;
            }
        if (!stored)
            for (auto &stack : next)
                if (!stack.block) {
                    stack = {drop, 1};
                    stored = true;
                    break;
                }
        if (!stored)
            return restore();
    }
    world_.set_block(b, 0);
    p.inventory = next;
    s.breaking.reset();
    broadcast(packet(PacketId::UpdateBlock, block_update(b, 0)));
    return {packet(PacketId::InventoryContent, inventory(p)),
            packet(PacketId::InventoryContent, cursor_inventory(p))};
}
std::vector<GamePacket> Server::use(Session &s, const Transaction &t, std::uint64_t now) {
    auto &p = world_.players().at(s.key);
    // Original ItemUseInventoryTransaction::handle (0x1ae62ee..0x1ae6366):
    // error 3 precedes dispatch for ALL actions, including click-air and break.
    // Network requests take the force=false path in ServerNetworkHandler.
    if (t.held != p.inventory[p.selected] || t.slot != static_cast<int>(p.selected))
        return reject_item_use(s, t);
    if (t.action == 2)
        return destroy(s, t.block, now);
    if (t.action == 1)
        return {};
    auto [x, y, z] = t.block;
    const auto held = p.inventory[p.selected];
    require(t.face >= 0 && t.face < 6);
    constexpr int faces[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                 {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
    World::Position target{x + faces[t.face][0], y + faces[t.face][1], z + faces[t.face][2]};
    const auto restore = [&] {
        std::vector<GamePacket> out{packet(PacketId::InventoryContent, inventory(p)),
                                    packet(PacketId::InventoryContent, cursor_inventory(p))};
        const auto block = world_.block(target);
        if (std::get<1>(target) >= 0 && std::get<1>(target) < 256 && block < 5)
            out.push_back(packet(PacketId::UpdateBlock, block_update(target, block)));
        return out;
    };
    if (s.game.player_game_type == 2 || s.game.settings.default_player_permission == 0 ||
        !held.block || held != t.held || !reach(p, t.block) || !world_.block(t.block) ||
        world_.block(target) || std::get<1>(target) < 0 || std::get<1>(target) >= 256)
        return restore();
    for (const auto &[id, session] : sessions_)
        if (session.active && overlap(world_.players().at(session.key), target))
            return restore();
    world_.set_block(target, held.block);
    if (s.game.player_game_type != 1) {
        auto &stack = p.inventory[p.selected];
        if (--stack.count == 0)
            stack = {};
    }
    broadcast(packet(PacketId::UpdateBlock, block_update(target, held.block)));
    broadcast(packet(PacketId::MobEquipment, equipment(s.game.runtime_id, p)), s.game.runtime_id);
    return {packet(PacketId::InventoryContent, inventory(p)),
            packet(PacketId::InventoryContent, cursor_inventory(p))};
}
std::vector<GamePacket> Server::handle(std::uint64_t id, const GamePacket &packet_in,
                                       std::uint64_t now) {
    auto found = sessions_.find(id);
    if (found == sessions_.end())
        return {};
    auto &s = found->second;
    auto &p = world_.players().at(s.key);
    try {
        const auto type = static_cast<PacketId>(packet_in.header.id);
        ByteReader in(packet_in.body);
        if (type == PacketId::SetLocalPlayerAsInitialized) {
            if (s.active)
                return {};
            s.active = true;
            for (const auto &[other_id, other] : sessions_)
                if (other.active && other_id != id) {
                    enqueue(id, packet(PacketId::PlayerList,
                                       protocol::player_list_body(other.identity,
                                                                  other.game.unique_id)));
                    enqueue(id, packet(PacketId::AddPlayer,
                                       add_player(other.identity, other.game,
                                                  world_.players().at(other.key))));
                    enqueue(other_id,
                            packet(PacketId::PlayerList,
                                   protocol::player_list_body(s.identity, s.game.unique_id)));
                    enqueue(other_id,
                            packet(PacketId::AddPlayer, add_player(s.identity, s.game, p)));
                }
            return {};
        }
        if (!s.active)
            return {};
        if (type == PacketId::MovePlayer) {
            auto m = motion(packet_in.body);
            require(m.runtime == id && (m.mode == 0 || m.mode == 3));
            const double dt = s.credit_time && now >= s.credit_time
                                  ? std::min(1.0, (now - s.credit_time) / 1000.0)
                                  : 0;
            s.credit_time = now;
            s.move_credit =
                std::min(3.0, s.move_credit + dt * (s.game.player_game_type == 1 ? 20 : 12));
            s.up_credit = std::min(3.0, s.up_credit + dt * 12);
            auto next = p;
            if (m.mode == 0) {
                next.x = m.data.x;
                next.y = m.data.y;
                next.z = m.data.z;
            }
            require(std::abs(next.x) < 30000000 && std::abs(next.z) < 30000000 && next.y >= 1 &&
                    next.y <= 258);
            const double dx = next.x - p.x, dz = next.z - p.z,
                         up = std::max(0.0, double(next.y - p.y)), distance = std::hypot(dx, dz);
            require(distance <= s.move_credit && up <= s.up_credit &&
                    p.y - next.y <= std::max(4.0, dt * 80) && !collides(next));
            s.move_credit -= distance;
            s.up_credit -= up;
            next.pitch = std::clamp(m.data.pitch, -90.0f, 90.0f);
            next.yaw = std::remainder(m.data.yaw, 360.0f);
            next.head_yaw = std::remainder(m.data.head_yaw, 360.0f);
            p = next;
            m.data = p;
            broadcast(packet(PacketId::MovePlayer, motion(m)), id);
            return {};
        }
        if (type == PacketId::MobEquipment) {
            require(in.varint64() == id);
            auto stack = item(in);
            auto slot = in.byte(), selected = in.byte(), window = in.byte();
            in.finish();
            require(
                window == 0 && selected < 9 &&
                (slot == selected || slot == selected + 9 || (slot == 255 && stack.block == 0)) &&
                stack == p.inventory[selected]);
            p.selected = selected;
            broadcast(packet(PacketId::MobEquipment, equipment(id, p)), id);
            return {};
        }
        if (type == PacketId::PlayerHotbar) {
            auto selected = in.varint();
            require(in.byte() == 0);
            in.boolean();
            in.finish();
            require(selected < 9);
            p.selected = selected;
            broadcast(packet(PacketId::MobEquipment, equipment(id, p)), id);
            return {};
        }
        if (type == PacketId::ContainerClose) {
            const auto window = in.byte();
            in.finish();
            if ((window == 0 || window == 124) && p.cursor.block) {
                auto remaining = p.cursor;
                for (auto &stack : p.inventory)
                    if (stack.block == remaining.block && stack.count < 64) {
                        const auto count = std::min<unsigned>(64 - stack.count, remaining.count);
                        stack.count = static_cast<std::uint8_t>(stack.count + count);
                        remaining.count = static_cast<std::uint8_t>(remaining.count - count);
                    }
                if (remaining.count)
                    for (auto &stack : p.inventory)
                        if (!stack.block) {
                            stack = remaining;
                            remaining.count = 0;
                            break;
                        }
                p.cursor = remaining.count ? remaining : ItemStack{};
            }
            return {packet(PacketId::InventoryContent, inventory(p)),
                    packet(PacketId::InventoryContent, cursor_inventory(p))};
        }
        if (type == PacketId::PlayerAction) {
            require(in.varint64() == id);
            auto action = in.signed_varint();
            if (action != 0 && action != 13) {
                // Jump/sprint/sneak/abort use no block target and may carry
                // sentinel coordinates; consume their fields without applying
                // build-position bounds or resetting a valid movement.
                in.signed_varint();
                in.varint();
                in.signed_varint();
                in.signed_varint();
                in.finish();
                if (action == 1)
                    s.breaking.reset();
                return {};
            }
            auto b = position(in);
            in.signed_varint();
            in.finish();
            if (action == 0 && reach(p, b)) {
                s.breaking = b;
                s.break_time = now;
            } else if (action == 1)
                s.breaking.reset();
            else if (action == 13 && s.game.player_game_type == 1)
                return destroy(s, b, now);
            return {};
        }
        if (type == PacketId::InventoryTransaction) {
            const auto t = transaction(packet_in.body);
            if (t.type == 2)
                return use(s, t, now);
            require(t.type == 0);
            auto next = p.inventory;
            auto cursor = p.cursor;
            std::array<int, 5> balance{};
            std::set<std::pair<int, unsigned>> touched;
            bool creative = false;
            for (const auto &a : t.changes) {
                if (a.source == 3) {
                    require(s.game.player_game_type == 1 &&
                            s.game.settings.default_player_permission != 0);
                    creative = true;
                    continue;
                }
                require(a.source == 0 &&
                        ((a.window == 0 && a.slot < 36) || (a.window == 124 && a.slot == 0)) &&
                        touched.insert({a.window, a.slot}).second);
                auto &slot = a.window == 0 ? next[a.slot] : cursor;
                require(slot == a.before);
                balance[a.before.block] -= a.before.count;
                balance[a.after.block] += a.after.count;
                slot = a.after;
            }
            require(creative ||
                    std::all_of(balance.begin() + 1, balance.end(), [](int n) { return n == 0; }));
            p.inventory = next;
            p.cursor = cursor;
            broadcast(packet(PacketId::MobEquipment, equipment(id, p)), id);
            return {packet(PacketId::InventoryContent, inventory(p)),
                    packet(PacketId::InventoryContent, cursor_inventory(p))};
        }
        return {};
    } catch (const protocol::ProtocolError &) {
        return correct(s);
    } catch (const std::length_error &) {
        return correct(s);
    }
}
} // namespace bds::game
