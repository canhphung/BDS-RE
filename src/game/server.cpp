#include "bds/game/server.hpp"
#include "bds/network/spawn.hpp"
#include <algorithm>
#include <bit>
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
// Block::getDestroySpeed() (0x1e68db0, "hardness") and
// Player::canDestroy(Block) (0x22ed360: Material::isAlwaysDestroyable, else the
// held item's canDestroySpecial) for the internal palette. BDS-RE inventories
// hold only block items, whose canDestroySpecial is false. The values
// reproduce the original crack rates (tests/reference/break_bds_1.6.0.15.json).
struct Hardness {
    float destroy_time;
    bool always_destroyable;
};
Hardness hardness(BlockId id) {
    switch (id) {
    case 1: return {1.5f, false}; // stone: Material Stone
    case 2: return {0.6f, true};  // grass: Material Dirt
    case 3: return {0.5f, true};  // dirt: Material Dirt
    case 4: return {-1.0f, false}; // bedrock: unbreakable time
    default: return {0.0f, true};
    }
}
// Player::getDestroySpeed(Block) 0x22ed000. The held item speed
// (ItemInstance::getDestroySpeed) is 1.0 for block items and an empty hand;
// BDS-RE has no mob effects, rides, flying or water, so only the
// "not on ground" division by 5 (0x22ed1aa, constant 0x2830bf0) applies.
float destroy_speed(const PlayerData &p) {
    float speed = 1.0f;
    if (!p.on_ground)
        speed /= 5.0f;
    return speed;
}
// Player::getDestroyProgress(Block) 0x22ed290, used by GameMode::getDestroyRate
// (0x1ab5820, whose effect multipliers do not apply here).
float destroy_rate(const PlayerData &p, BlockId id) {
    const auto h = hardness(id);
    if (0.0f > h.destroy_time)
        return 0.0f;
    return destroy_speed(p) / h.destroy_time / (h.always_destroyable ? 30.0f : 100.0f);
}
GamePacket level_event(int event, float x, float y, float z, int data) {
    ByteWriter out;
    out.signed_varint(event);
    out.float32(x);
    out.float32(y);
    out.float32(z);
    out.signed_varint(data);
    return packet(PacketId::LevelEvent, out.take());
}
constexpr int LevelEventStartBlockCracking = 3600; // 0xe10
constexpr int LevelEventStopBlockCracking = 3601;  // 0xe11
constexpr int LevelEventParticlesDestroyBlock = 2001; // 0x7d1
std::uint32_t block_runtime_id(BlockId id) {
    const auto &palette = protocol::internal_block_palette();
    return protocol::runtime_id(palette[id].name, palette[id].data).value();
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
    // ItemUseInventoryTransaction::handle 0x1ae6290: for every action except
    // click-air, Actor::distanceTo(Vec3(BlockPos)) > 12.5 (0x2830f48) is
    // error 7. The distance is from the player position (eye height) to the
    // block's minimum corner, in single precision.
    auto [x, y, z] = b;
    const float dx = p.x - static_cast<float>(x), dy = p.y - static_cast<float>(y),
                dz = p.z - static_cast<float>(z);
    return !(std::sqrt(dx * dx + dy * dy + dz * dz) > 12.5f);
}
std::vector<GamePacket> Server::correct(Session &s) {
    const auto &p = world_.players().at(s.key);
    return {packet(PacketId::MovePlayer, motion({s.game.runtime_id, p, 1, false})),
            packet(PacketId::InventoryContent, inventory(p)),
            packet(PacketId::InventoryContent, cursor_inventory(p))};
}
std::vector<GamePacket> Server::resend_blocks(const Transaction &t) const {
    // ItemUseInventoryTransaction::resendBlocksAroundArea (0x1ae6120). The four
    // updates intentionally retain duplicates when the clicked face is vertical.
    auto [x, y, z] = t.block;
    const auto face = static_cast<std::int8_t>(t.face);
    World::Position adjacent{x + (face == 5) - (face == 4), y + (face == 1) - (face == 0),
                             z + (face == 3) - (face == 2)};
    auto [ax, ay, az] = adjacent;
    std::vector<GamePacket> out;
    for (const auto &pos :
         {t.block, World::Position{x, y + 1, z}, adjacent, World::Position{ax, ay + 1, az}})
        out.push_back(packet(PacketId::UpdateBlock, block_update(pos, world_.block(pos))));
    return out;
}
std::vector<GamePacket> Server::reject_item_use(const Session &s, const Transaction &t) const {
    // ItemUseInventoryTransaction::onTransactionError (original 0x1ae7710):
    // resendBlocksAroundArea, then inventory/hotbar correction.
    auto out = resend_blocks(t);
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
std::vector<GamePacket> Server::destroy(Session &s, const Transaction &t) {
    // Break branch of ItemUseInventoryTransaction::handle ($_2, 0x1ae6d20):
    // GameMode::destroyBlock (vtable +0x18); on failure resendBlocksAroundArea.
    // There is no destroy-time check: StartBreak is not required and the
    // elapsed time is never compared (oracle: tests/reference/break_bds_1.6.0.15.json).
    const auto b = t.block;
    const auto id = world_.block(b);
    // SurvivalMode::destroyBlock (0x1abcd90) -> GameMode::destroyBlock
    // (0x1ab4a80) -> BlockSource::checkBlockDestroyPermissions (0x1c588d0).
    // INFERRED: the permission check is reduced to the adventure game type and
    // the visitor permission level, the only adventure settings BDS-RE has.
    // _destroyBlockInternal (0x1ab4e50) refuses air.
    if (!id || s.game.player_game_type == 2 || s.game.settings.default_player_permission == 0)
        return resend_blocks(t);
    const bool creative = s.game.player_game_type == 1;
    // Unbreakable blocks are not refused: the original breaks bedrock through
    // this path in survival (oracle case "bedrock").
    world_.set_block(b, 0);
    auto [x, y, z] = b;
    const auto update = packet(PacketId::UpdateBlock, block_update(b, 0));
    // broadcastDimensionEvent(ParticlesDestroyBlock, pos + 0.5, runtime id, null)
    const auto particles = level_event(LevelEventParticlesDestroyBlock, x + .5f, y + .5f, z + .5f,
                                       static_cast<int>(block_runtime_id(id)));
    broadcast(update, s.game.runtime_id);
    broadcast(particles, s.game.runtime_id);
    // PlayerActionPacket(StopBreak, runtime id) sent to the breaking player.
    ByteWriter stop;
    stop.varint64(s.game.runtime_id);
    stop.signed_varint(2);
    stop.signed_varint(0);
    stop.varint(0);
    stop.signed_varint(0);
    stop.signed_varint(0);
    std::vector<GamePacket> out{update, particles, packet(PacketId::PlayerAction, stop.take())};
    if (!creative && hardness(id).always_destroyable) {
        // Block::playerDestroy (0x1e558a0) runs only when Player::canDestroy
        // holds (stone by hand drops nothing) and calls spawnResources: grass
        // and dirt drop one dirt item entity.
        spawn_item(b, {3, 1});
    }
    return out;
}
void Server::spawn_item(World::Position block, ItemStack item) {
    // BlockLegacy::spawnResources (0x1e54360) with chance 1.0: one
    // Level::getRandom().nextFloat() test per resource, then popResource.
    level_random_.next_float();
    auto a = pop_resource(block, item, level_random_, actor_random_);
    const auto id = allocate_entity_id();
    a.runtime_id = static_cast<std::uint64_t>(id);
    a.unique_id = id;
    items_.push_back(a);
}
void Server::tick_item(ItemActor &a) {
    normal_tick(a, world_, [this](ItemActor &self) {
        // ItemActor::_mergeWithNeighbours (0x23feee0): items whose box meets
        // the box grown by 0.5 on every axis; Level::broadcastEntityEvent
        // (ActorEvent 69, new stack size) for the absorbing item.
        const auto area = merge_area(self);
        for (auto &other : items_)
            if (!other.removed && other.bb.intersects(area))
                if (auto *kept = merge(self, other))
                    broadcast(packet(PacketId::ActorEvent, actor_event(kept->runtime_id, 69, kept->item.count)));
    });
    if (a.removed)
        return;
    if (!a.added) {
        // The entity reaches clients after its first tick: dirty actor data,
        // then AddItemActorPacket built from the last sent position.
        a.sent_pos = a.pos;
        a.sent_on_ground = a.on_ground;
        broadcast(packet(PacketId::SetActorData, item_actor_data(a.runtime_id)));
        broadcast(packet(PacketId::AddItemActor, add_item_actor(a)));
        a.added = true;
        return;
    }
    for (auto &q : replicate(a))
        broadcast(std::move(q));
}
void Server::touch(Session &s, ItemActor &a) {
    // ItemActor::playerTouch (0x23ff130): no throw time, no pickup delay and
    // PlayerInventoryProxy::canAdd (an unfinished matching stack or an empty
    // slot among the 36 inventory slots).
    if (a.throw_time != 0 || a.pickup_delay != 0)
        return;
    auto &p = world_.players().at(s.key);
    const auto fits = [&](const ItemStack &stack) {
        return (stack.block == a.item.block && stack.count < 64) || !stack.block;
    };
    if (std::none_of(p.inventory.begin(), p.inventory.end(), fits))
        return;
    // Player::take (0x22f0640) with slot -1: PlayerInventoryProxy::add ->
    // FillingContainer::add/_addResource: fill the first unfinished matching
    // stack, else the first empty slot, until nothing more fits.
    std::vector<SlotChange> changes;
    ItemStack left = a.item;
    while (left.count) {
        int slot = -1;
        for (int i = 0; i < 36 && slot < 0; ++i)
            if (p.inventory[i].block == left.block && p.inventory[i].count < 64)
                slot = i;
        for (int i = 0; i < 36 && slot < 0; ++i)
            if (!p.inventory[i].block)
                slot = i;
        if (slot < 0)
            break;
        auto &target = p.inventory[static_cast<unsigned>(slot)];
        const auto before = target;
        const auto add = std::min<int>(left.count, 64 - target.count);
        target = {left.block, static_cast<std::uint8_t>(target.count + add)};
        left.count = static_cast<std::uint8_t>(left.count - add);
        changes.push_back({static_cast<unsigned>(slot), before, target});
    }
    if (!left.count)
        left = {};
    enqueue(s.game.runtime_id, packet(PacketId::InventoryTransaction, pickup_transaction(changes, a.item, left)));
    broadcast(packet(PacketId::TakeItemActor, take_item_actor(a.runtime_id, s.game.runtime_id)));
    if (left.count) {
        // INFERRED: a partially taken stack keeps the remainder on the ground.
        a.item = left;
        return;
    }
    a.removed = true;
}
void Server::tick() {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (!items_[i].removed)
            tick_item(items_[i]);
    // Player::aiStep (0x22ebd40): living players touch every entity whose box
    // meets their box grown by (1, 0.5, 1).
    for (auto &[id, s] : sessions_) {
        if (!s.active)
            continue;
        const auto &p = world_.players().at(s.key);
        const float feet = p.y - 1.62f;
        const AABB area{{p.x - 0.3f - 1.0f, feet - 0.5f, p.z - 0.3f - 1.0f},
                        {p.x + 0.3f + 1.0f, feet + 1.8f + 0.5f, p.z + 0.3f + 1.0f}};
        for (auto &a : items_)
            if (!a.removed && a.added && area.intersects(a.bb))
                touch(s, a);
    }
    for (const auto &a : items_)
        if (a.removed && a.added)
            broadcast(packet(PacketId::RemoveActor, remove_actor(a.unique_id)));
    std::erase_if(items_, [](const ItemActor &a) { return a.removed; });
}
std::vector<GamePacket> Server::use(Session &s, const Transaction &t) {
    auto &p = world_.players().at(s.key);
    // Original ItemUseInventoryTransaction::handle (0x1ae62ee..0x1ae6366):
    // error 3 precedes dispatch for ALL actions, including click-air and break.
    // Network requests take the force=false path in ServerNetworkHandler.
    if (t.held != p.inventory[p.selected] || t.slot != static_cast<int>(p.selected))
        return reject_item_use(s, t);
    // Mob::isAlive is always true (no health yet); then the reach check, error 7.
    if (t.action != 1 && !reach(p, t.block))
        return reject_item_use(s, t);
    if (t.action == 2)
        return destroy(s, t);
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
        !held.block || held != t.held || !world_.block(t.block) ||
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
                                       std::uint64_t /*now*/) {
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
            // Item entities already in the level become visible to the new player.
            for (const auto &a : items_)
                if (a.added)
                    enqueue(id, packet(PacketId::AddItemActor, add_item_actor(a)));
            return {};
        }
        if (!s.active)
            return {};
        if (type == PacketId::MovePlayer) {
            // ServerNetworkHandler::handle(MovePlayerPacket) 0x182bcf0, translated.
            // The original performs no speed, distance or collision validation:
            // ServerPlayer::checkMovementStats (0x25fd0d0) only accumulates the
            // distance for a telemetry event.
            const auto m = motion(packet_in.body);
            // _getPlayer (0x182c1c0): a runtime id that is not the sender's
            // player yields null and the packet is ignored.
            if (m.runtime != id)
                return {};
            // Skipped while the player changes dimension or is not yet
            // initialised (+0x140c / +0x1410); BDS-RE has no dimension change,
            // and inactive sessions returned above. Riding (getRide) does not
            // exist yet either, so the rider branch is not reachable.
            // Normal path: Y is clamped to [-128, 512] (0x182be9f), then
            // mode 2 (teleport) sets position/rotation directly, mode 1 (reset)
            // does so only for a dead player, every other mode lerps to the
            // position (Mob::lerpTo); BDS-RE has no health yet, so players are
            // alive and the end state of every mode is the same position.
            // Deliberate deviation (safety, not gameplay): the original stores any
            // float, but BDS-RE converts X/Z to chunk coordinates, where a
            // non-finite or out-of-int-range value is undefined behaviour. Such
            // packets are ignored instead.
            if (!std::isfinite(m.data.x) || !std::isfinite(m.data.z) || std::abs(m.data.x) > 1.0e9f ||
                std::abs(m.data.z) > 1.0e9f)
                return {};
            // mce::Math::clamp(v, lo, hi) (0x1838170): v > hi ? hi : v > lo ? v : lo,
            // so a NaN Y becomes -128.
            const auto clamp = [](float v, float lo, float hi) { return v > hi ? hi : v > lo ? v : lo; };
            p.x = m.data.x;
            p.y = clamp(m.data.y, -128.0f, 512.0f);
            p.z = m.data.z;
            p.pitch = m.data.pitch;
            p.yaw = m.data.yaw;
            p.head_yaw = m.data.head_yaw; // +0x11c0 = packet head yaw
            // onGround (+0x124) is stored by every path except mode 1 with a
            // dead player, which BDS-RE cannot have.
            p.on_ground = m.grounded;
            // Dimension::sendPacketForEntity(player, packet, player): the
            // received packet is relayed unchanged to everyone else.
            broadcast(packet(PacketId::MovePlayer, packet_in.body), id);
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
            // ServerNetworkHandler::handle(PlayerActionPacket) 0x182d140. Only the
            // block-breaking actions are translated; the others (jump, sprint,
            // sneak, dimension change, gliding, ...) have no state in BDS-RE yet.
            if (in.varint64() != id)
                return {}; // _getPlayer returns null
            const auto action = in.signed_varint();
            const auto x = in.signed_varint();
            const auto y = static_cast<std::int32_t>(in.varint());
            const auto z = in.signed_varint();
            in.signed_varint(); // face
            in.finish();
            const float fx = static_cast<float>(x), fy = static_cast<float>(y),
                        fz = static_cast<float>(z);
            if (action == 0) {
                // StartBreak (0x182d6d3): Level::extinguishFire and Block::attack have
                // no effect on the internal palette. Unless the player is creative,
                // broadcastLevelEvent(StartBlockCracking, Vec3(pos),
                // floor(65535 * GameMode::getDestroyRate(block)), null) reaches
                // every player, the sender included. No reach check.
                if (s.game.player_game_type != 1) {
                    const auto block = y >= 0 && y < 256 ? world_.block({x, y, z}) : BlockId{0};
                    const auto rate = std::floor(65535.0f * destroy_rate(p, block));
                    broadcast(level_event(LevelEventStartBlockCracking, fx, fy, fz,
                                          static_cast<int>(rate)));
                }
            } else if (action == 1 || action == 2) {
                // AbortBreak / StopBreak: broadcastLevelEvent(StopBlockCracking, Vec3(pos), 0).
                broadcast(level_event(LevelEventStopBlockCracking, fx, fy, fz, 0));
            }
            return {};
        }
        if (type == PacketId::InventoryTransaction) {
            const auto t = transaction(packet_in.body);
            if (t.type == 2)
                return use(s, t);
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
