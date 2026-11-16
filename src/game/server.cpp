#include "bds/game/server.hpp"
#include "bds/network/spawn.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
namespace bds::game {
namespace {
// The eye offset Player::recheckSpawnPosition (0x22f28fb) adds to the spawn
// point before setTeleportDestination / RespawnPacket: 0x3fcf5c7e (1.62001014),
// not the 1.62001002 heightOffset.
constexpr float kSpawnEyeOffset = std::bit_cast<float>(0x3fcf5c7eu);
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
    // Every packet is delivered in order (the original never merges queued
    // movement); a client that falls this far behind is disconnected.
    if (s.queue.size() >= 4096) {
        s.overflow = true;
        return;
    }
    s.queue.push_back(std::move(packet));
}
std::uint64_t Server::Session::flags() const {
    // Actor flags of a standing survival player in the original (bits 14, 19,
    // 34, 46, 47), plus SNEAKING (1) and SPRINTING (3).
    // Bit 14 is added once the client has initialized (join capture packet 247);
    // a dead player loses it and gains INVISIBLE (bit 5).
    if (dead)
        return 0xc00400080020ULL;
    std::uint64_t f = active ? 0xc00400084000ULL : 0xc00400080000ULL;
    if (sneaking)
        f |= 1ULL << 1;
    if (sprinting)
        f |= 1ULL << 3;
    return f;
}
void Server::send_for_entity(std::uint64_t entity, const GamePacket &packet, std::uint64_t except) {
    for (auto &[id, s] : sessions_)
        if (s.active && id != except && (id == entity || s.known.contains(entity)))
            enqueue(id, packet);
}
std::vector<std::pair<int, int>> client_ticking_offsets(int r) {
    // Level::Level (0x1c814e9): for tick range r, rows z = ±(r - i) with
    // |x| <= i + 1 for i < r, plus the z = 0 row with |x| <= r (57 offsets for
    // r = 4, equal to the list dumped from the original).
    std::vector<std::pair<int, int>> out;
    for (int i = 0; i < r; ++i)
        for (int x = -i - 1; x <= i + 1; ++x) {
            out.emplace_back(x, r - i);
            out.emplace_back(x, -r + i);
            if (i == r - 1)
                out.emplace_back(x, 0);
        }
    return out;
}
std::vector<std::pair<int, int>> Server::ticking_offsets() const {
    // r is server.properties tick-distance (min with AppPlatform::
    // getMaxSimRadiusInChunks, which the 4..12 range does not reach).
    return client_ticking_offsets(static_cast<int>(properties_.tick_distance));
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
    while (!s.queue.empty() && out.size() < 64 &&
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
    s.attributes.values[kHealth].value = p.health;
    s.attributes.values[kHunger].value = p.hunger;
    s.attributes.values[kSaturation].value = p.saturation;
    s.attributes.values[kExhaustion].value = p.exhaustion;
    s.attributes.last_food = p.hunger;
    const auto spawn_attributes_body = spawn_attributes(g.runtime_id, s.attributes);
    sessions_.emplace(g.runtime_id, std::move(s));
    ByteWriter hotbar;
    hotbar.varint(p.selected);
    hotbar.byte(0);
    hotbar.boolean(true);
    // The player's first dirty attributes and synched data, as the original
    // sends them before the chunks (join capture packets 26-28): all attribute
    // instances, the complete metadata with flags 0xc00000080000 (BREATHING,
    // HAS_COLLISION, AFFECTED_BY_GRAVITY), then flags with bit 34 added. Without
    // them the client keeps default flags: no gravity/collision (floating) and
    // the air-bubble bar.
    return {packet(PacketId::InventoryContent, inventory(p)),
            packet(PacketId::InventoryContent, creative_inventory()),
            packet(PacketId::InventoryContent, cursor_inventory(p)),
            packet(PacketId::PlayerHotbar, hotbar.take()),
            packet(PacketId::UpdateAttributes, spawn_attributes_body),
            packet(PacketId::SetActorData, player_data(g.runtime_id, identity.display_name, 0xc00000080000ULL)),
            packet(PacketId::SetActorData, actor_flags(g.runtime_id, 0xc00400080000ULL))};
}
void Server::leave(std::uint64_t runtime) {
    auto i = sessions_.find(runtime);
    if (i == sessions_.end())
        return;
    // RemoveActor reaches the players who knew this one at their next
    // _updateNearbyEntities (the entity no longer exists).
    if (i->second.active)
        broadcast(packet(PacketId::PlayerList, remove_player_list(i->second.identity)), runtime);
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
        // Block::playerDestroy ends with Player::causeFoodExhaustion(EXHAUSTION_MINE).
        cause_food_exhaustion(s.game.runtime_id, s, 0.025f);
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
                    send_for_entity(kept->runtime_id,
                                    packet(PacketId::ActorEvent, actor_event(kept->runtime_id, 69, kept->item.count)));
    });
    if (a.removed)
        return;
    if (!a.added) {
        // After its first tick the entity is in its chunk: players see it at
        // their next _updateNearbyEntities, which sends AddItemActorPacket
        // built from the last sent position (preceded by the dirty actor data).
        a.sent_pos = a.pos;
        a.sent_on_ground = a.on_ground;
        a.added = true;
        fresh_items_.insert(a.runtime_id);
        return;
    }
    for (auto &q : replicate(a))
        send_for_entity(a.runtime_id, q);
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
    send_for_entity(a.runtime_id, packet(PacketId::TakeItemActor, take_item_actor(a.runtime_id, s.game.runtime_id)));
    if (left.count) {
        // INFERRED: a partially taken stack keeps the remainder on the ground.
        a.item = left;
        return;
    }
    a.removed = true;
}
void Server::tick_player(std::uint64_t id, Session &s) {
    auto &p = world_.players().at(s.key);
    // Mob::baseTick (0x22b1770): hurt time countdown (0x22b1989),
    // Mob::tickAttributes (0x22b1fc0), then Actor::_sendDirtyActorData
    // (0x21bd970): dirty attributes, then dirty synched data, to the player and
    // every player that knows it.
    if (s.hurt_time > 0)
        --s.hurt_time;
    tick_attributes(id, s);
    if (!s.attributes.dirty.empty()) {
        send_for_entity(id, packet(PacketId::UpdateAttributes,
                                   update_attributes(id, s.attributes, s.attributes.dirty)));
        s.attributes.dirty.clear();
    }
    if (s.death_data_dirty)
        send_for_entity(id, packet(PacketId::SetActorData, actor_bounding_box(id, 0.2f, 0.2f)));
    if (s.flags_dirty)
        send_for_entity(id, packet(PacketId::SetActorData, actor_flags(id, s.flags())));
    s.flags_dirty = s.death_data_dirty = false;
    const float before[3] = {p.x, p.y, p.z};
    // Mob::normalTick -> MovementInterpolator::tick (0x23cf280): move a
    // 1/steps share towards the MovePlayer target, the last step exactly.
    if (s.lerp.steps > 0) {
        if (s.lerp.steps == 1) {
            p.x = s.lerp.x;
            p.y = s.lerp.y;
            p.z = s.lerp.z;
            p.pitch = s.lerp.pitch;
            p.yaw = s.lerp.yaw;
        } else {
            const float t = 1.0f / static_cast<float>(s.lerp.steps);
            p.x = p.x + (s.lerp.x - p.x) * t;
            p.y = p.y + (s.lerp.y - p.y) * t;
            p.z = p.z + (s.lerp.z - p.z) * t;
            // mce::Math::lerpRotate: a + wrapDegrees(b - a) * t
            // mce::Math::wrapDegrees (0x26dde50): fmod(180 + d, 360), +360 if
            // negative, minus 180.
            const auto wrap = [](float d) {
                float x = std::fmod(180.0f + d, 360.0f);
                if (x < 0.0f)
                    x = x + 360.0f;
                return x - 180.0f;
            };
            p.pitch = p.pitch + wrap(s.lerp.pitch - p.pitch) * t;
            p.yaw = p.yaw + wrap(s.lerp.yaw - p.yaw) * t;
        }
        --s.lerp.steps;
    }
    // Player::aiStep (0x22ebd40): living players touch every entity whose box
    // meets their box grown by (1, 0.5, 1).
    if (s.attributes[kHealth] > 0.0f) {
        const float feet = p.y - 1.62f;
        const AABB area{{p.x - 0.3f - 1.0f, feet - 0.5f, p.z - 0.3f - 1.0f},
                        {p.x + 0.3f + 1.0f, feet + 1.8f + 0.5f, p.z + 0.3f + 1.0f}};
        for (auto &a : items_)
            if (!a.removed && a.added && area.intersects(a.bb))
                touch(s, a);
    }
    // Player::normalTick -> Player::_applyExhaustion (0x22e9e20) with this
    // tick's movement: on ground, 0.01 (walk) or EXHAUSTION_SPRINT (sprint) per
    // centimetre-rounded horizontal metre. Water branches cannot occur.
    {
        const float dx = p.x - before[0], dz = p.z - before[2];
        const int horizontal = static_cast<int>(std::round(static_cast<double>(std::sqrt(dx * dx + dz * dz) * 100.0f)));
        if (p.on_ground && horizontal > 0) {
            const float rate = s.sprinting ? std::bit_cast<float>(0x3dccccccU)  // FoodConstants::EXHAUSTION_SPRINT
                                           : std::bit_cast<float>(0x3c23d70aU); // EXHAUSTION_WALK
            cause_food_exhaustion(id, s, rate * static_cast<float>(horizontal) * 0.01f);
        }
    }
    // ServerPlayer::normalTick (0x25fad17): every 80 ticks ActorEvent 66 with
    // the world generator's structure feature at the player (none in BDS-RE).
    if (--s.feature_timer <= 0) {
        s.feature_timer = 80;
        send_for_entity(id, packet(PacketId::ActorEvent, actor_event(id, 66, 0)));
    }
    s.was_on_ground = p.on_ground; // 0x25fb074
    p.health = s.attributes[kHealth];
    p.hunger = s.attributes[kHunger];
    p.saturation = s.attributes[kSaturation];
    p.exhaustion = s.attributes[kExhaustion];
}
void Server::add_buff(std::uint64_t id, Session &s, int attribute, float amount, int type) {
    // AttributeInstance::addBuff (0x24a7760) for an InstantaneousAttributeBuff:
    // value = clamp(value + amount), the delegate may reject the change
    // (HealthAttributeDelegate::change), and a changed value is marked dirty.
    // HealthAttributeDelegate::getBuffValue only alters harm/heal types for
    // inverted (undead) mobs and the magic damage types, none of which occur.
    auto &a = s.attributes.values[static_cast<std::size_t>(attribute)];
    const float old = a.value;
    a.value = Attributes::apply(a, amount);
    if (attribute == kHealth && !health_change(id, s, old, a.value, type))
        a.value = old;
    if (old != a.value)
        s.attributes.mark(attribute);
}
bool Server::health_change(std::uint64_t id, Session &s, float old_value, float new_value, int type) {
    // HealthAttributeDelegate::change (0x24a3330): integer health before and
    // after (floor(v + FLT_EPSILON)); damage types run Mob::hurtEffects.
    const float eps = std::numeric_limits<float>::epsilon();
    const int before = static_cast<int>(std::floor(old_value + eps));
    const int after = static_cast<int>(std::floor(new_value + eps));
    const int diff = before - after;
    if (before == after)
        return true;
    switch (type) {
    case kBuffStarve: // cause STARVE (AttributeBuff::buffTypeToDamageCause 0 -> 16)
        return hurt_effects(id, s, 16, diff);
    case kBuffHeal:
        if (s.hurt_time < 5) // Actor::healEffects (0x21bdcb0)
            s.hurt_time = 5;
        return true;
    case kBuffNone:
        if (-diff > 0) {
            if (s.hurt_time < 5)
                s.hurt_time = 5;
        } else if (-diff < 0)
            hurt_effects(id, s, -1, diff);
        return true;
    default: // types 1, 2 and 10 (Mob::actuallyHurt) are accepted as they are
        return true;
    }
}
void Server::cause_food_exhaustion(std::uint64_t id, Session &s, float amount) {
    // Player::causeFoodExhaustion (0x22eb710): not for creative players.
    if (s.game.player_game_type == 1)
        return;
    add_buff(id, s, kExhaustion, amount, kBuffNone);
}
void Server::tick_attributes(std::uint64_t id, Session &s) {
    // Mob::tickAttributes: AttributeInstance::tick (0x24a7950) for each
    // instance in map order; an instance whose value changed during its
    // delegate's tick is marked dirty again.
    auto &a = s.attributes;
    const int difficulty = static_cast<int>(properties_.difficulty);
    const bool creative = s.game.player_game_type == 1;
    const bool natural_regeneration = true; // game rule 15, not configurable in BDS-RE
    const auto health = [&] { return static_cast<int>(a[kHealth]); };
    for (int attribute = 0; attribute < kAttributeCount; ++attribute) {
        const float old = a[attribute];
        if (attribute == kHealth) {
            // HealthAttributeDelegate::tick (0x24a31e0): peaceful regeneration.
            ++a.health_ticks;
            if (difficulty == 0 && natural_regeneration && a.health_ticks % 20 == 0)
                add_buff(id, s, kHealth, 1.0f, kBuffHeal);
        } else if (attribute == kHunger && !creative) {
            // HungerAttributeDelegate::tick (0x24a2a10)
            a.last_food = a[kHunger];
            const float food = a[kHunger];
            ++a.hunger_ticks;
            if (difficulty == 0 && a.hunger_ticks % 10 == 0)
                add_buff(id, s, kHunger, 1.0f, kBuffNone);
            const bool hurt_now = health() > 0 && a[kHealth] < a.values[kHealth].max; // Player::isHurt
            if (natural_regeneration && food >= 18.0f && hurt_now) { // FoodConstants::HEAL_LEVEL
                if (++a.heal_timer >= 80) {                         // HEALTH_TICK_COUNT
                    add_buff(id, s, kHealth, 1.0f, kBuffHeal);
                    add_buff(id, s, kExhaustion, 3.0f, kBuffNone); // EXHAUSTION_HEAL
                    a.heal_timer = 0;
                }
            } else if (0.0f >= food) { // STARVE_LEVEL
                if (++a.heal_timer >= 80) {
                    if (health() > 10 || difficulty == 3 || (health() > 1 && difficulty == 2)) {
                        add_buff(id, s, kHealth, -1.0f, kBuffStarve);
                        hurt(id, s, 16, 1);
                    }
                    a.heal_timer = 0;
                }
            } else
                a.heal_timer = 0;
        } else if (attribute == kExhaustion) {
            // ExhaustionAttributeDelegate::tick (0x24a43a0): at MAX_EXHAUSTION
            // (4) take 4 and notify the hunger delegate
            // (HungerAttributeDelegate::notify 0x24a2e50): saturation first,
            // then food unless peaceful.
            if (a[kExhaustion] >= 4.0f) {
                add_buff(id, s, kExhaustion, -4.0f, kBuffNone);
                if (!creative) {
                    if (a[kSaturation] > 0.0f)
                        add_buff(id, s, kSaturation, -1.0f, kBuffNone);
                    else if (difficulty != 0)
                        add_buff(id, s, kHunger, -1.0f, kBuffNone);
                }
            }
        }
        if (a[attribute] != old)
            a.mark(attribute);
    }
}
bool Server::hurt(std::uint64_t id, Session &s, int cause, int damage) {
    // Actor::hurt -> Player::_hurt (0x22f0ec0) -> Mob::_hurt (0x22b37f0).
    if (s.attributes[kHealth] <= 0.0f || s.dead)
        return false;
    // INFERRED: creative players (invulnerable ability) only take void damage.
    if (s.game.player_game_type == 1 && cause != 12)
        return false;
    if (cause == 13)
        s.last_hurt = 0;
    else if (s.hurt_time > 5) {
        if (damage <= s.last_hurt)
            return false;
    } else if (s.hurt_time > 0)
        return false;
    else
        s.last_hurt = 0;
    // Mob::actuallyHurt (0x22b44c0): no armor, enchantments, resistance or
    // absorption here, so health takes the remaining damage (buff type 10).
    const int dealt = damage - s.last_hurt;
    if (dealt != 0)
        add_buff(id, s, kHealth, static_cast<float>(-dealt), kBuffDamage);
    return hurt_effects(id, s, cause, damage);
}
bool Server::hurt_effects(std::uint64_t id, Session &s, int cause, int damage) {
    // Mob::hurtEffects (0x22b3b60) for a damage source without an entity.
    bool animate = false;
    if (cause == 13 || s.hurt_time <= 5) {
        animate = true;
        s.last_hurt = damage;
        s.hurt_time = 10;
    } else if (s.hurt_time > 5) {
        if (damage <= s.last_hurt)
            return false;
        s.last_hurt = damage;
    }
    // Level::broadcastEntityEvent(HURT, cause), only while still alive (or
    // saved by a totem, which does not exist here).
    if (animate && s.attributes[kHealth] > 0.0f)
        send_for_entity(id, packet(PacketId::ActorEvent, actor_event(id, 2, cause)));
    if (s.attributes[kHealth] <= 0.0f) // no totem of undying exists here
        die(id, s, cause);
    return true;
}
void Server::cause_fall_damage(std::uint64_t id, Session &s, float distance) {
    // Mob::causeFallDamage (0x22b5190): ceil(distance * fall_damage - 3 - jump boost).
    const float scaled = s.attributes[kFallDamage] * distance - 3.0f - 0.0f;
    const float damage = static_cast<float>(std::ceil(static_cast<double>(scaled)));
    if (damage > 0.0f)
        hurt(id, s, 5, static_cast<int>(damage)); // ActorDamageCause FALL
}
void Server::die(std::uint64_t id, Session &s, int cause) {
    // ServerPlayer::die -> Player::die (0x22ec9b0) -> Mob::die (0x22b4a00), as
    // observed on the original (tools/reference/hunger_health_oracle.py).
    if (s.dead)
        return;
    s.dead = true;
    auto &p = world_.players().at(s.key);
    // keepInventory is false: the cursor item and every inventory slot are
    // dropped (Actor::drop with random throw) and the inventory cleared; the
    // client receives its inventory three times.
    std::vector<ItemStack> drops;
    if (p.cursor.block)
        drops.push_back(p.cursor);
    for (const auto &stack : p.inventory)
        if (stack.block)
            drops.push_back(stack);
    for (const auto &stack : drops) {
        // Actor::drop (0x21bfbc0): at getAttachPos(4) (0.3 below the eyes, 0.15
        // ahead, measured under gdb) plus the motion, pickup delay 40, throw
        // time 10, speed nextFloat * 0.5 in a random direction, 0.2 upwards.
        const float yaw = p.yaw * std::numbers::pi_v<float> / 180.0f;
        const Vec3 at{p.x - std::sin(yaw) * 0.15f + s.motion[0], p.y - 0.3f + s.motion[1],
                      p.z + std::cos(yaw) * 0.15f + s.motion[2]};
        auto a = drop_item(at, stack, actor_random_);
        const float speed = actor_random_.next_float() * 0.5f;
        const float angle = actor_random_.next_float() * std::numbers::pi_v<float> * 2.0f;
        a.motion = {-std::sin(angle) * speed, 0.2f, std::cos(angle) * speed};
        const auto entity = allocate_entity_id();
        a.runtime_id = static_cast<std::uint64_t>(entity);
        a.unique_id = entity;
        items_.push_back(a);
    }
    p.cursor = {};
    p.inventory.fill({});
    ByteWriter hotbar;
    hotbar.varint(p.selected);
    hotbar.byte(0);
    hotbar.boolean(true);
    const auto hotbar_body = hotbar.take();
    for (int i = 0; i < 3; ++i) {
        enqueue(id, packet(PacketId::InventoryContent, inventory(p)));
        enqueue(id, packet(PacketId::InventoryContent, protocol::empty_inventory_body(120, 4)));
        enqueue(id, packet(PacketId::InventoryContent, cursor_inventory(p)));
        enqueue(id, packet(PacketId::InventoryContent, protocol::empty_inventory_body(119, 1)));
        enqueue(id, packet(PacketId::PlayerHotbar, hotbar_body));
    }
    send_for_entity(id, packet(PacketId::ActorEvent, actor_event(id, 3, 0))); // DEATH
    enqueue(id, packet(PacketId::Event, player_died_event(s.game.unique_id, cause)));
    const char *key = cause == 5    ? "death.attack.fall"
                      : cause == 16 ? "death.attack.starve"
                      : cause == 12 ? "death.attack.outOfWorld"
                                    : "death.attack.generic";
    // The name argument is Player::getPlayerNameLocToken: "\x01" + player UUID.
    broadcast(packet(PacketId::Text, death_message(key, std::string("\x01") + s.identity.identity)));
    const auto &spawn = s.game.settings.spawn;
    enqueue(id, packet(PacketId::Respawn,
                       respawn_position(static_cast<float>(spawn.x) + 0.5f, static_cast<float>(spawn.y) + kSpawnEyeOffset,
                                        static_cast<float>(spawn.z) + 0.5f)));
    s.death_data_dirty = true;
    s.flags_dirty = true;
}
void Server::respawn(std::uint64_t id, Session &s) {
    // PlayerAction 7 for a dead player (0x182d4b3): ActorEvent RESPAWN,
    // SetHealth, the reset attributes, the living actor data, and the player
    // back at the spawn point (observed on the original).
    if (!s.dead)
        return;
    s.dead = false;
    s.hurt_time = s.last_hurt = 0;
    send_for_entity(id, packet(PacketId::ActorEvent, actor_event(id, 18, 0)));
    enqueue(id, packet(PacketId::SetHealth, set_health(20)));
    auto &a = s.attributes;
    for (int attribute : {kHealth, kHunger, kExhaustion, kSaturation, kLevel, kExperience}) {
        auto &v = a.values[static_cast<std::size_t>(attribute)];
        v.value = attribute == kExhaustion || attribute == kLevel || attribute == kExperience ? 0.0f : v.max;
    }
    enqueue(id, packet(PacketId::UpdateAttributes,
                       update_attributes(id, a, {kHealth, kHunger, kExhaustion, kSaturation, kLevel, kExperience})));
    send_for_entity(id, packet(PacketId::SetActorData, respawned_data(id, s.flags())));
    auto &p = world_.players().at(s.key);
    const auto &spawn = s.game.settings.spawn;
    p.x = static_cast<float>(spawn.x) + 0.5f;
    p.y = static_cast<float>(spawn.y) + kSpawnEyeOffset;
    p.z = static_cast<float>(spawn.z) + 0.5f;
    s.lerp.steps = 0;
}
void Server::update_nearby(std::uint64_t id, Session &s) {
    // ServerPlayer::_updateNearbyEntities (0x25f90e0) / _scanForNewEntities
    // (0x25fe670): entities in loaded chunks at the client ticking offsets
    // around the player, and other players whose chunk is at such an offset.
    const auto &p = world_.players().at(s.key);
    const int cx = chunk_coordinate(static_cast<int>(std::floor(p.x)));
    const int cz = chunk_coordinate(static_cast<int>(std::floor(p.z)));
    std::set<std::pair<int, int>> chunks;
    for (auto [dx, dz] : ticking_offsets())
        chunks.insert({cx + dx, cz + dz});
    const auto near = [&](float x, float z) {
        return chunks.contains({chunk_coordinate(static_cast<int>(std::floor(x))),
                                chunk_coordinate(static_cast<int>(std::floor(z)))});
    };
    std::set<std::uint64_t> scan;
    for (const auto &[other_id, other] : sessions_)
        if (other_id != id && other.active) {
            const auto &o = world_.players().at(other.key);
            if (near(o.x, o.z))
                scan.insert(other_id);
        }
    for (const auto &a : items_)
        if (a.added && !a.removed && near(a.pos.x, a.pos.z) &&
            world_.chunk(chunk_coordinate(static_cast<int>(std::floor(a.pos.x))),
                         chunk_coordinate(static_cast<int>(std::floor(a.pos.z)))))
            scan.insert(a.runtime_id);
    for (auto entity : scan) {
        if (s.known.contains(entity))
            continue;
        s.known.insert(entity);
        if (auto other = sessions_.find(entity); other != sessions_.end()) {
            // tryCreateAddActorPacket, then for mobs MobArmorEquipment (four
            // armor slots, always empty in BDS-RE) and, when the carried item is
            // not empty, MobEquipment (slot 0, selected 0, container 0). The
            // offhand is always empty and players here are alive.
            const auto &o = world_.players().at(other->second.key);
            enqueue(id, packet(PacketId::AddPlayer,
                               add_player(other->second.identity, other->second.game, o, other->second.flags())));
            ByteWriter armor;
            armor.varint64(entity);
            for (int slot = 0; slot < 4; ++slot)
                item(armor, {});
            enqueue(id, packet(PacketId::MobArmorEquipment, armor.take()));
            if (o.inventory[o.selected].block) {
                ByteWriter held;
                held.varint64(entity);
                item(held, o.inventory[o.selected]);
                held.byte(0);
                held.byte(0);
                held.byte(0);
                enqueue(id, packet(PacketId::MobEquipment, held.take()));
            }
        } else
            for (const auto &a : items_)
                if (a.runtime_id == entity) {
                    if (fresh_items_.contains(entity))
                        enqueue(id, packet(PacketId::SetActorData, item_actor_data(entity)));
                    enqueue(id, packet(PacketId::AddItemActor, add_item_actor(a)));
                }
    }
    for (auto it = s.known.begin(); it != s.known.end();) {
        if (scan.contains(*it)) {
            ++it;
            continue;
        }
        // Left the area or no longer exists: RemoveActorPacket(unique id).
        std::int64_t unique = static_cast<std::int64_t>(*it);
        for (const auto &a : items_)
            if (a.runtime_id == *it)
                unique = a.unique_id;
        if (auto other = sessions_.find(*it); other != sessions_.end())
            unique = other->second.game.unique_id;
        enqueue(id, packet(PacketId::RemoveActor, remove_actor(unique)));
        it = s.known.erase(it);
    }
}
void Server::tick() {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (!items_[i].removed)
            tick_item(items_[i]);
    for (auto &[id, s] : sessions_)
        if (s.active)
            tick_player(id, s);
    std::erase_if(items_, [](const ItemActor &a) { return a.removed; });
    for (auto &[id, s] : sessions_)
        if (s.active)
            update_nearby(id, s);
    fresh_items_.clear();
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
    // The click-block branch of ItemUseInventoryTransaction::handle ($_2,
    // 0x1ae6f67) puts the player at the transaction's fromPosition for the
    // duration of useItemOn, so the acting player is checked where the client
    // was, not at the interpolated server position (which trails by up to
    // three ticks and made pillar building fail).
    for (const auto &[id, session] : sessions_) {
        if (!session.active)
            continue;
        PlayerData where = world_.players().at(session.key);
        if (&session == &s) {
            where.x = t.from[0];
            where.y = t.from[1];
            where.z = t.from[2];
        }
        if (overlap(where, target))
            return restore();
    }
    world_.set_block(target, held.block);
    if (s.game.player_game_type != 1) {
        auto &stack = p.inventory[p.selected];
        if (--stack.count == 0)
            stack = {};
    }
    broadcast(packet(PacketId::UpdateBlock, block_update(target, held.block)));
    send_for_entity(s.game.runtime_id, packet(PacketId::MobEquipment, equipment(s.game.runtime_id, p)), s.game.runtime_id);
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
            s.flags_dirty = true; // flag 14 goes out at the next tick
            // Player list entries are exchanged at once; AddPlayer and item
            // entities follow at the next tick through _updateNearbyEntities.
            for (const auto &[other_id, other] : sessions_)
                if (other.active && other_id != id) {
                    enqueue(id, packet(PacketId::PlayerList,
                                       protocol::player_list_body(other.identity,
                                                                  other.game.unique_id)));
                    enqueue(other_id,
                            packet(PacketId::PlayerList,
                                   protocol::player_list_body(s.identity, s.game.unique_id)));
                }
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
            const float y = clamp(m.data.y, -128.0f, 512.0f);
            // The position delta is kept as the actor's motion (+0xa0); it
            // shifts items dropped before the interpolation catches up.
            s.motion[0] = m.data.x - p.x;
            s.motion[1] = y - p.y;
            s.motion[2] = m.data.z - p.z;
            p.on_ground = m.grounded; // +0x124 (mode 1 with a dead player cannot occur)
            if (m.mode == 2) {
                // Teleport mode: position and rotation are set at once.
                p.x = m.data.x;
                p.y = y;
                p.z = m.data.z;
                p.pitch = m.data.pitch;
                p.yaw = m.data.yaw;
            } else {
                // checkMovementStats only feeds telemetry. Mob::lerpTo
                // (MovementInterpolator::lerpTo 0x23cf1d0): one step on the tick
                // the player lands (on ground now, not on ground last tick,
                // +0x125), otherwise three (0x182c087).
                s.lerp = {m.data.x, y, m.data.z, m.data.pitch, m.data.yaw,
                          !s.was_on_ground && p.on_ground ? 1 : 3};
            }
            p.head_yaw = m.data.head_yaw; // +0x11c0 = packet head yaw
            // Dimension::sendPacketForEntity(player, packet, player): the
            // received packet is relayed unchanged to the players that know
            // this one.
            send_for_entity(id, packet(PacketId::MovePlayer, packet_in.body), id);
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
            send_for_entity(id, packet(PacketId::MobEquipment, equipment(id, p)), id);
            return {};
        }
        if (type == PacketId::PlayerHotbar) {
            auto selected = in.varint();
            require(in.byte() == 0);
            in.boolean();
            in.finish();
            require(selected < 9);
            p.selected = selected;
            send_for_entity(id, packet(PacketId::MobEquipment, equipment(id, p)), id);
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
        if (type == PacketId::ActorFall) {
            // ServerNetworkHandler::handle(ActorFallPacket) (0x182de80):
            // Level::getRuntimeEntity(runtime id), then
            // ServerPlayer::handleFallDistanceOnServer (0x25fb2e0): in the void
            // 20 VOID damage, otherwise Mob::causeFallDamage(distance).
            const auto target = in.varint64();
            const float distance = std::bit_cast<float>(fixed(in, 4));
            const bool void_fall = in.boolean();
            in.finish();
            auto other = sessions_.find(target);
            if (other == sessions_.end() || !other->second.active)
                return {};
            if (void_fall)
                hurt(target, other->second, 12, 20);
            else if (std::isfinite(distance))
                cause_fall_damage(target, other->second, distance);
            return {};
        }
        if (type == PacketId::PlayerAction) {
            // ServerNetworkHandler::handle(PlayerActionPacket) 0x182d140.
            if (in.varint64() != id)
                return {}; // _getPlayer returns null
            const auto action = in.signed_varint();
            const auto x = in.signed_varint();
            const auto y = static_cast<std::int32_t>(in.varint());
            const auto z = in.signed_varint();
            const auto face = in.signed_varint();
            in.finish();
            const float fx = static_cast<float>(x), fy = static_cast<float>(y),
                        fz = static_cast<float>(z);
            const auto block_at = [&] { return y >= 0 && y < 256 ? world_.block({x, y, z}) : BlockId{0}; };
            switch (action) {
            case 0:
                // StartBreak (0x182d6d3): Level::extinguishFire and Block::attack have
                // no effect on the internal palette. Unless the player is creative,
                // broadcastLevelEvent(StartBlockCracking, Vec3(pos),
                // floor(65535 * GameMode::getDestroyRate(block)), null) reaches
                // every player, the sender included. No reach check.
                if (s.game.player_game_type != 1)
                    broadcast(level_event(LevelEventStartBlockCracking, fx, fy, fz,
                                          static_cast<int>(std::floor(65535.0f * destroy_rate(p, block_at())))));
                break;
            case 1: // AbortBreak
            case 2: // StopBreak: broadcastLevelEvent(StopBlockCracking, Vec3(pos), 0)
                broadcast(level_event(LevelEventStopBlockCracking, fx, fy, fz, 0));
                break;
            case 7: // Respawn (dead players only); not relayed
                respawn(id, s);
                return {};
            case 13: // dimension change acknowledgement; not relayed
            case 14:
                return {};
            case 8:
                // Jump: Player::handleJumpEffects (0x22eb7c0) causes food
                // exhaustion, EXHAUSTION_SPRINT_JUMP (0.8) or EXHAUSTION_JUMP (0.2);
                // not relayed.
                cause_food_exhaustion(id, s, s.sprinting ? 0.8f : 0.2f);
                return {};
            case 9:  // StartSprint: Mob::setSprinting(true)
            case 10: // StopSprint
                // Status flag 3 and the +30% minecraft:movement modifier
                // (0.1 -> 0.13); both go out as dirty data at the next tick.
                if (s.sprinting != (action == 9)) {
                    s.sprinting = action == 9;
                    s.flags_dirty = true;
                    s.attributes.set(kMovement, s.sprinting ? 0.13f : 0.1f);
                }
                break;
            case 11: // StartSneak: Mob::setSneaking -> status flag 1; not relayed
            case 12: // StopSneak
                if (s.sneaking != (action == 11)) {
                    s.sneaking = action == 11;
                    s.flags_dirty = true;
                }
                return {};
            case 17:
                // broadcastLevelEvent(2008, pos + 0.5, 0)
                broadcast(level_event(2008, fx + 0.5f, fy + 0.5f, fz + 0.5f, 0));
                break;
            case 18: {
                // ContinueBreak: nothing (and no relay) on air; otherwise the
                // punch particles (2014, runtime id + face << 24) and, unless
                // creative, UpdateBlockCracking (3602) with the destroy rate.
                const auto id_here = block_at();
                if (!id_here)
                    return {};
                broadcast(level_event(2014, fx + 0.5f, fy + 0.5f, fz + 0.5f,
                                      static_cast<int>(block_runtime_id(id_here) +
                                                       (static_cast<std::uint32_t>(face) << 24))));
                if (s.game.player_game_type != 1)
                    broadcast(level_event(3602, fx, fy, fz,
                                          static_cast<int>(std::floor(65535.0f * destroy_rate(p, id_here)))));
                break;
            }
            default:
                // Sleeping (5, 6), gliding (15, 16), enchantment seed (20),
                // swimming and spin attack (21-24) change no state BDS-RE has.
                break;
            }
            // Dimension::sendPacketForEntity(player, packet, player) (0x182de67).
            send_for_entity(id, packet(PacketId::PlayerAction, packet_in.body), id);
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
            send_for_entity(id, packet(PacketId::MobEquipment, equipment(id, p)), id);
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
