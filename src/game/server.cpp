#include "bds/game/server.hpp"
#include "bds/game/effects.hpp"
#include "bds/game/mce_math.hpp"
#include "bds/game/random_pos.hpp"
#include "bds/network/spawn.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
namespace bds::game {
namespace {
std::uint8_t max_stack(const ItemStack &s) { return item_info(s.block).max_stack; }
// ItemInstance::isStackable: same item and aux value.
bool stacks_with(const ItemStack &a, const ItemStack &b) { return a.block == b.block && a.aux == b.aux; }
// The eye offset Player::recheckSpawnPosition (0x22f28fb) adds to the spawn
// point before setTeleportDestination / RespawnPacket: 0x3fcf5c7e (1.62001014),
// not the 1.62001002 heightOffset.
constexpr float kSpawnEyeOffset = std::bit_cast<float>(0x3fcf5c7eu);
TickChunkPosition item_chunk(const ItemActor &actor) {
    return {chunk_coordinate(static_cast<int>(std::floor(actor.pos.x))),
            chunk_coordinate(static_cast<int>(std::floor(actor.pos.z)))};
}
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
std::int32_t level_seed(const World &world, const Properties &p) {
    // A purely numeric level-seed is used as-is, anything else as 0 (policy).
    std::int32_t seed = 0;
    const auto *begin = p.seed.data();
    if (!p.seed.empty() && std::from_chars(begin, begin + p.seed.size(), seed).ptr != begin + p.seed.size())
        seed = 0;
    return world.terrain().kind ? static_cast<std::int32_t>(world.terrain().seed) : seed;
}
Server::Server(World &world, const Properties &properties, AccessLists &access,
               Random::DrawObserver random_observer)
    : world_(world), lighting_(world), properties_(properties), access_(access) {
    level_random_.set_seed(static_cast<std::uint32_t>(level_seed(world, properties)));
    level_random_.observe_draws(std::move(random_observer));
    level_tick_ = world.age(); // Level::getCurrentTick
    protocol::shuffle_block_palette(level_random_);
    chunk_offsets_ = ticking_offsets(); // Level::Level constructs the list even on non-shuffle ticks.
    // LevelChunk +0x1054 (Finalization) is stored with the chunk
    // (serializeFinalization 0x1de651f; legacy deserialize sets 2). BDS-RE does
    // not persist it: INFERRED, chunks present at startup count as finalized,
    // generated ones start at 0 (LevelChunk::LevelChunk 0x1de08cf).
    for (const auto &position : world.chunk_positions())
        chunk_state_[position] = 2;
}
std::uint64_t Server::Session::flags() const {
    // Actor flags of a standing survival player in the original (bits 14, 19,
    // 34, 46, 47), plus SNEAKING (1) and SPRINTING (3).
    // Bit 14 is added once the client has initialized (join capture packet 247);
    // a dead player loses it and gains INVISIBLE (bit 5).
    // Mob::tickDeath (0x22b2970) makes the body invisible at death time 20;
    // the dead flags go out then (instant damage capture).
    if (dead && death_time >= 20)
        return 0xc00400080020ULL;
    std::uint64_t f = active ? 0xc00400084000ULL : 0xc00400080000ULL;
    if (vehicle)
        f |= 1ULL << 2;
    if (sneaking)
        f |= 1ULL << 1;
    if (sprinting)
        f |= 1ULL << 3;
    if (using_item.block)
        f |= 1ULL << 4; // USINGITEM (Player::startUsingItem)
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
    out.push_back(packet(PacketId::InventoryContent, armor_inventory(p))); // the offhand stays empty
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
    player_order_.push_back(g.runtime_id); // Level::addPlayer: emplace_back
    // The overworld Dimension is created for the first player; before it the
    // original's Level::_saveSomeChunks returns at its empty dimension map
    // (reference trace: no level-Random draws between startup and the first
    // player's tick).
    overworld_created_ = true;
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
    dismount(runtime);
    auto i = sessions_.find(runtime);
    if (i == sessions_.end())
        return;
    // RemoveActor reaches the players who knew this one at their next
    // _updateNearbyEntities (the entity no longer exists).
    if (i->second.active)
        broadcast(packet(PacketId::PlayerList, remove_player_list(i->second.identity)), runtime);
    sessions_.erase(i);
    std::erase(player_order_, runtime); // Level::_cleanupDisconnectedPlayers: vector::erase
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
        // LiquidBlock::getResourceCount (0x1e8f2c0) returns zero. Internal
        // liquid indices must never be treated as item/block drop indices.
        if (!is_water(id) && id != kStillLava) spawn_item(b, {3, 1});
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
    add_item(std::move(a));
}
void Server::add_item(ItemActor actor) {
    actor.owner_chunk = item_chunk(actor); // LevelChunk::addEntity writes Actor +0xc8.
    chunk_actors_.add(actor.owner_chunk, actor.runtime_id);
    items_.push_back(std::move(actor));
}
void Server::tick_item(ItemActor &a) {
    update_water_current(a, world_); // Actor::updateWaterState, before normalTick
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
        return (stacks_with(stack, a.item) && stack.count < max_stack(stack)) || !stack.block;
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
            if (stacks_with(p.inventory[i], left) && p.inventory[i].count < max_stack(left))
                slot = i;
        for (int i = 0; i < 36 && slot < 0; ++i)
            if (!p.inventory[i].block)
                slot = i;
        if (slot < 0)
            break;
        auto &target = p.inventory[static_cast<unsigned>(slot)];
        const auto before = target;
        const auto add = std::min<int>(left.count, max_stack(left) - target.count);
        target = {left.block, static_cast<std::uint8_t>(target.count + add), left.aux};
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
    if (s.dead && ++s.death_time == 20) // Mob::tickDeath
        s.flags_dirty = true;
    tick_attributes(id, s);
    // Only syncable attributes go out (attack damage, knockback resistance
    // and follow range never appear in the original's UpdateAttributes).
    std::erase_if(s.attributes.dirty, [](int a) { return a == kAttackDamage || a == kKnockbackResistance || a == kFollowRange; });
    if (!s.attributes.dirty.empty()) {
        send_for_entity(id, packet(PacketId::UpdateAttributes,
                                   update_attributes(id, s.attributes, s.attributes.dirty)));
        s.attributes.dirty.clear();
    }
    const auto send_dirty_data = [&] {
        if (s.death_data_dirty)
            send_for_entity(id, packet(PacketId::SetActorData, actor_bounding_box(id, 0.2f, 0.2f)));
        if (s.flags_dirty)
            send_for_entity(id, packet(PacketId::SetActorData, actor_flags(id, s.flags())));
        if (s.effect_keys)
            send_for_entity(id, packet(PacketId::SetActorData, effect_data(id, s.effect_color, false, s.effect_keys)));
        s.flags_dirty = s.death_data_dirty = false;
        s.effect_keys = 0;
    };
    send_dirty_data();
    // Mob::tickEffects (0x22b2050) after the dirty data went out.
    tick_effects(id, s);
    const float before[3] = {p.x, p.y, p.z};
    // Mob::normalTick -> MovementInterpolator::tick (0x23cf280): move a
    // 1/steps share towards the MovePlayer target, the last step exactly.
    if (s.vehicle)
        s.lerp.steps = 0;
    if (s.lerp.steps > 0) {
        // The interpolator clears the motion (+0xa0, the MovePlayer delta)
        // whenever it steps (traced under gdb).
        s.motion[0] = s.motion[1] = s.motion[2] = 0.0f;
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
    // The end of Mob::normalTick (0x22b356a): synched data dirtied since
    // _sendDirtyActorData (the effect colour of tickEffects) goes out in the
    // same tick (traced under gdb: instant health capture).
    send_dirty_data();
    // Actor::checkFallDamage with this tick's movement: falling adds the drop,
    // landing resets (ServerPlayer::checkFallDamage deals no damage; the
    // client reports falls with ActorFallPacket).
    if (p.on_ground)
        s.fall_distance = 0.0f;
    else if (p.y < before[1])
        s.fall_distance -= p.y - before[1];
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
void Server::add_buff(std::uint64_t id, EffectState &s, int attribute, float amount, int type) {
    // AttributeInstance::addBuff (0x24a7760) for an InstantaneousAttributeBuff:
    // value = clamp(value + amount), the delegate may reject the change
    // (HealthAttributeDelegate::change), and a changed value is marked dirty.
    // HealthAttributeDelegate::getBuffValue only alters harm/heal types for
    // inverted (undead) mobs and the magic damage types, none of which occur.
    auto &a = s.attributes.values[static_cast<std::size_t>(attribute)];
    const float old = a.value;
    a.value = Attributes::apply(a, amount);
    if (attribute == kHealth) {
        const auto session = sessions_.find(id);
        if (session != sessions_.end() && !health_change(id, session->second, old, a.value, type))
            a.value = old;
    }
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
    case 4: // harm, 5 and 6 (wither): cause MAGIC / WITHER
    case 5:
    case 6:
    case 8: // fatal poison
        return hurt_effects(id, s, type == 6 ? 15 : 14, diff);
    case 7: // poison never kills: the change is refused below one health
        if (new_value >= 1.0f)
            return hurt_effects(id, s, 14, diff);
        return false;
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
        // Temporal buffs first (AttributeInstance::tick 0x24a7950): tick the
        // life timer, apply the buff when due (_calculateValue through the
        // delegate's getBuffValue and change), erase it once complete.
        auto &buffs = a.temporal[static_cast<std::size_t>(attribute)];
        for (std::size_t i = 0; i < buffs.size();) {
            auto &b = buffs[i];
            ++b.life;
            if (b.should_buff()) {
                float amount = b.value();
                // HealthAttributeDelegate::getBuffValue (0x24a3980): harm and
                // poison types pass Mob::getDamageAfterMagicAbsorb (resistance).
                if (attribute == kHealth && (b.type == 4 || b.type == 5 || b.type == 7 || b.type == 8) &&
                    s.effects[kResistance].id) {
                    const int amp = s.effects[kResistance].amplifier;
                    if (amp >= 5)
                        amount = 0.0f;
                    else {
                        const int t = (25 * static_cast<int>(amount) + s.absorb_remainder) *
                                      std::clamp(25 - (amp + 1) * 5, 5, 25) / 25;
                        s.absorb_remainder = t % 25;
                        amount = static_cast<float>(t / 25);
                    }
                }
                const int type = b.type;
                auto &v = a.values[static_cast<std::size_t>(attribute)];
                const float before = v.value;
                v.value = Attributes::apply(v, amount);
                if (attribute == kHealth && !health_change(id, s, before, v.value, type))
                    v.value = before;
                ++i;
            } else if (b.complete())
                buffs.erase(buffs.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
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
bool Server::hurt(std::uint64_t id, Session &s, int cause, int damage, std::uint64_t attacker, bool knock) {
    // Actor::hurt -> Player::_hurt (0x22f0ec0) -> Mob::_hurt (0x22b37f0).
    if (s.attributes[kHealth] <= 0.0f || s.dead)
        return false;
    // Player::isInvulnerableTo (0x22f6540): the INVULNERABLE ability of
    // creative players. INFERRED: void damage still applies.
    if (s.game.player_game_type == 1 && cause != 12)
        return false;
    // Player::_hurt for an entity source: Player::setLastHurtBy(ActorType)
    // (0x22f1330) sends SetLastHurtByPacket through the packet sender, which
    // reaches every client; the attacker is always a player (type 319).
    if (attacker)
        broadcast(packet(PacketId::SetLastHurtBy, set_last_hurt_by(319)));
    if (damage == 0)
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
    actually_hurt(id, s, cause, damage - s.last_hurt);
    return hurt_effects(id, s, cause, damage, attacker, knock);
}
void Server::actually_hurt(std::uint64_t id, Session &s, int cause, int damage) {
    // Mob::actuallyHurt (0x22b44c0) with bypassArmor false (Mob::_hurt).
    // +0x12c8 carries the 1/25 remainders of the absorb steps, +0x12c4 the
    // remainder of hits below one point.
    auto &a = s.attributes;
    s.absorb_remainder = 0;
    const auto absorb = [&](int d, int factor) { // Mob::_getDamageAfterAbsorb (0x22bcba0)
        const int t = (25 * d + s.absorb_remainder) * factor / 25;
        s.absorb_remainder = t % 25;
        return t / 25;
    };
    // Mob::getDamageAfterEnchantReduction (0x22bcb30): EnchantUtils::getDamageReduction
    // draws Random::nextFloat(0.5, 1) from the thread-local Random even
    // without protection; with none the factor is exactly 1.
    actor_random_.next_float(0.5f, 1.0f);
    {
        const int t = static_cast<int>(static_cast<float>(25 * damage + s.absorb_remainder) * 1.0f);
        damage = t / 25;
        s.absorb_remainder = t % 25;
    }
    // Mob::getDamageAfterArmorAbsorb (0x22bcbf0): not for causes 0, 4, 5, 7,
    // 9, 12, 13, 14; ServerPlayer::hurtArmor first, then the armor value.
    if (cause != 0 && cause != 14 && cause != 13 && cause != 12 && cause != 9 && cause != 7 && cause != 5 &&
        cause != 4) {
        auto &p = world_.players().at(s.key);
        // Mob::hurtArmor (0x22bd700): max(1, d / 4) to every damageable
        // armor item (ItemInstance::hurtAndBreak), then HurtArmorPacket with
        // the full damage to the player itself (ServerPlayer::hurtArmor 0x25fb150).
        const int wear = std::max(1, damage / 4);
        bool broke = false;
        for (auto &piece : p.armor)
            if (piece.block && item_info(piece.block).max_damage > 0) {
                piece.aux = static_cast<std::int16_t>(piece.aux + wear);
                if (piece.aux > item_info(piece.block).max_damage) {
                    piece = {};
                    broke = true;
                }
            }
        // Mob::sendArmor (0x22bdcd0) through the level's packet sender.
        if (broke)
            broadcast(packet(PacketId::MobArmorEquipment, armor_equipment(id, p)));
        enqueue(id, packet(PacketId::HurtArmor, hurt_armor(damage)));
        int armor = 0; // Mob::getArmorValue (0x22bd000)
        for (const auto &piece : p.armor)
            if (item_info(piece.block).kind == ItemKind::Armor)
                armor += item_info(piece.block).defense;
        damage = absorb(damage, std::clamp(25 - armor, 5, 25)); // ABSORPTION_FACTOR_MIN/MAX
    }
    // Mob::getDamageAfterMagicAbsorb (0x22bcd20): resistance.
    if (s.effects[kResistance].id && cause != 0 && cause != 12 && cause != 13) {
        const int amp = s.effects[kResistance].amplifier;
        damage = amp >= 5 ? 0 : absorb(damage, std::clamp(25 - (amp + 1) * 5, 5, 25));
    }
    if (damage < 1) {
        const int t = 25 * damage + s.damage_remainder + s.absorb_remainder;
        damage = t / 25;
        s.damage_remainder = t % 25;
    } else
        s.damage_remainder = 0;
    s.absorb_remainder = 0;
    // Absorption takes what it can (buff type 9), health the rest (type 10).
    const int before = damage;
    damage = static_cast<int>(std::max(static_cast<float>(damage) - a[kAbsorption], 0.0f));
    add_buff(id, s, kAbsorption, static_cast<float>(-(before - damage)), kBuffNone);
    if (damage != 0)
        add_buff(id, s, kHealth, static_cast<float>(-damage), kBuffDamage);
}
bool Server::hurt_effects(std::uint64_t id, Session &s, int cause, int damage, std::uint64_t attacker, bool knock) {
    // Mob::hurtEffects (0x22b3b60).
    if (s.game.player_game_type == 1 && cause != 12)
        return false;
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
    if (s.effects[kInstantHealth].id)
        return false;
    if (animate) {
        // Level::broadcastEntityEvent(HURT, cause), only while still alive (or
        // saved by a totem, which does not exist here).
        if (s.attributes[kHealth] > 0.0f)
            send_for_entity(id, packet(PacketId::ActorEvent, actor_event(id, 2, cause)));
        const auto other = sessions_.find(attacker);
        if (attacker && other != sessions_.end()) {
            const auto &me = world_.players().at(s.key);
            const auto &by = world_.players().at(other->second.key);
            float dx = by.x - me.x, dz = by.z - me.z;
            if (knock) {
                while (static_cast<double>(dx * dx + dz * dz) < 0.0001) {
                    dx = (actor_random_.next_float() - actor_random_.next_float()) * 0.01f;
                    dz = (actor_random_.next_float() - actor_random_.next_float()) * 0.01f;
                }
                knockback(id, s, dx, dz);
            }
        } else
            actor_random_.next_float(); // hurt direction: (int)(nextFloat() * 2) * 180
    }
    if (s.attributes[kHealth] <= 0.0f) { // no totem of undying exists here
        die(id, s, cause);
        return true;
    }
    return damage > 0 && animate;
}
void Server::knockback(std::uint64_t id, Session &s, float xd, float zd) {
    // ServerPlayer::knockback (0x25f89f0) -> Mob::knockback (0x22b4880) with
    // power 1, height 0.4, cap 0.4 (Mob::hurtEffects): the player's own
    // Random decides against the knockback resistance (0 for players). Then
    // SetActorMotion to every player that knows this one, itself included.
    // ServerPlayer::knockback sends the motion even when Mob::knockback returns early.
    if (!(s.attributes[kKnockbackResistance] > s.random.next_float())) {
        const float inv = mce_inv_sqrt(xd * xd + zd * zd); // mce::Math::invSqrt
        const float power = 0.4f * 1.0f;
        for (auto &m : s.motion)
            m = 0.5f * m;
        s.motion[0] = s.motion[0] - xd * inv * power;
        s.motion[1] = 0.4f + s.motion[1];
        s.motion[2] = s.motion[2] - zd * inv * power;
        if (s.motion[1] > 0.4f)
            s.motion[1] = 0.4f;
    }
    send_for_entity(id, packet(PacketId::SetActorMotion, set_actor_motion(id, {s.motion[0], s.motion[1], s.motion[2]})));
}
void Server::cause_fall_damage(std::uint64_t id, Session &s, float distance) {
    // Mob::causeFallDamage (0x22b5190): ceil(distance * fall_damage - 3 - jump boost).
    const float scaled = s.attributes[kFallDamage] * distance - 3.0f - 0.0f;
    const float damage = static_cast<float>(std::ceil(static_cast<double>(scaled)));
    if (damage > 0.0f) {
        // Actor::playSynchronizedSound at getAttachPos(0) (the feet): FALL_BIG
        // (24) above 4 damage, else FALL_SMALL (25), extra -1; then the hurt;
        // then the block-fall sound (9) with the runtime id of the block 0.2
        // below the feet (Level::broadcastSoundEvent reaches every client).
        const auto &p = world_.players().at(s.key);
        constexpr float height_offset = std::bit_cast<float>(0x3fcf5c7du);
        const Vec3 feet{p.x, p.y - height_offset, p.z};
        broadcast(packet(PacketId::LevelSoundEvent, level_sound(damage <= 4.0f ? 25 : 24, feet, -1, 319)));
        hurt(id, s, 5, static_cast<int>(damage)); // ActorDamageCause FALL
        const World::Position under{static_cast<int>(std::floor(p.x)),
                                    static_cast<int>(std::floor(p.y + -0.2f - height_offset)),
                                    static_cast<int>(std::floor(p.z))};
        const int y = std::get<1>(under);
        const BlockId block = y >= 0 && y < 256 ? world_.block(under) : 0;
        broadcast(packet(PacketId::LevelSoundEvent,
                         level_sound(9, feet, static_cast<int>(block_runtime_id(block)), 319)));
    }
}
void Server::die(std::uint64_t id, Session &s, int cause) {
    // ServerPlayer::die -> Player::die (0x22ec9b0) -> Mob::die (0x22b4a00), as
    // observed on the original (tools/reference/hunger_health_oracle.py).
    if (s.dead)
        return;
    dismount(id);
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
    for (const auto &stack : p.armor) // Player::dropEquipment: the armor too
        if (stack.block)
            drops.push_back(stack);
    for (const auto &stack : drops)
        drop(id, s, stack, true);
    p.cursor = {};
    p.inventory.fill({});
    p.armor.fill({});
    ByteWriter hotbar;
    hotbar.varint(p.selected);
    hotbar.byte(0);
    hotbar.boolean(true);
    const auto hotbar_body = hotbar.take();
    for (int i = 0; i < 3; ++i) {
        enqueue(id, packet(PacketId::InventoryContent, inventory(p)));
        enqueue(id, packet(PacketId::InventoryContent, armor_inventory(p)));
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
    s.death_time = 0;
}
void Server::respawn(std::uint64_t id, Session &s) {
    // PlayerAction 7 for a dead player (0x182d4b3): ActorEvent RESPAWN,
    // SetHealth, the reset attributes, the living actor data, and the player
    // back at the spawn point (observed on the original).
    if (!s.dead)
        return;
    s.dead = false;
    s.hurt_time = s.last_hurt = 0;
    // Player::resetPos (0x22eba90): motion cleared, Mob::removeAllEffects
    // (REMOVE packets) before the respawn event, fall distance 0.
    s.motion[0] = s.motion[1] = s.motion[2] = 0.0f;
    remove_all_effects(id, s);
    s.fall_distance = 0.0f;
    s.using_item = {}; // INFERRED: the item in use is dropped with everything else
    send_for_entity(id, packet(PacketId::ActorEvent, actor_event(id, 18, 0)));
    enqueue(id, packet(PacketId::SetHealth, set_health(20)));
    auto &a = s.attributes;
    for (int attribute : {kHealth, kHunger, kExhaustion, kSaturation, kLevel, kExperience}) {
        auto &v = a.values[static_cast<std::size_t>(attribute)];
        // resetPos: health resetToMaxValue, the others resetToDefaultValue (no clamp).
        v.value = attribute == kHealth ? v.max : v.fallback;
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
        if (a.added && !a.removed && chunks.contains(a.owner_chunk) &&
            world_.chunk(a.owner_chunk.first, a.owner_chunk.second))
            scan.insert(a.runtime_id);
    for (const auto &m : mobs_)
        if (!m.removed && chunks.contains(m.owner_chunk) && world_.chunk(m.owner_chunk.first, m.owner_chunk.second))
            scan.insert(m.runtime_id);
    for (auto entity : scan) {
        if (s.known.contains(entity))
            continue;
        s.known.insert(entity);
        if (auto other = sessions_.find(entity); other != sessions_.end()) {
            // tryCreateAddActorPacket, then for mobs MobArmorEquipment (the four
            // armor slots) and, when the carried item is
            // not empty, MobEquipment (slot 0, selected 0, container 0). The
            // offhand is always empty and players here are alive.
            const auto &o = world_.players().at(other->second.key);
            enqueue(id, packet(PacketId::AddPlayer,
                               add_player(other->second.identity, other->second.game, o, other->second.flags())));
            enqueue(id, packet(PacketId::MobArmorEquipment, armor_equipment(entity, o)));
            if (o.inventory[o.selected].block) {
                ByteWriter held;
                held.varint64(entity);
                item(held, o.inventory[o.selected]);
                held.byte(0);
                held.byte(0);
                held.byte(0);
                enqueue(id, packet(PacketId::MobEquipment, held.take()));
            }
        } else if (const auto mob = std::find_if(mobs_.begin(), mobs_.end(),
                                                  [&](const MobActor &m) { return m.runtime_id == entity; });
                   mob != mobs_.end()) {
            // Actor::tryCreateAddActorPacket then MobArmorEquipment (capture
            // world-mobs-p1: AddActor 0x0d followed by 0x20 per mob).
            enqueue(id, packet(PacketId::AddActor, add_mob_actor({mob->unique_id,
                                                                  mob->runtime_id,
                                                                  mob->data->type,
                                                                  mob->sent_pos,
                                                                  mob->motion,
                                                                  mob->sent_rotation[0] / 256.0f * 360.0f,
                                                                  mob->sent_rotation[1] / 256.0f * 360.0f,
                                                                  mob->sent_rotation[2] / 256.0f * 360.0f,
                                                                  mob->baby,
                                                                  mob->scale,
                                                                  mob->width,
                                                                  mob->height,
                                                                  mob->variant,
                                                                  mob->mark_variant,
                                                                  mob->color,
                                                                  mob->health,
                                                                  mob->max_health,
                                                                  mob->movement,
                                                                  mob->fall_damage,
                                                                  mob->tamed,
                                                                  mob->saddled})));
            enqueue(id, packet(PacketId::MobArmorEquipment, empty_armor_equipment(entity)));
            if (mob->rider) {
                const auto rider = sessions_.find(*mob->rider);
                if (rider != sessions_.end()) {
                    ByteWriter link;
                    link.signed_varint64(mob->unique_id);
                    link.signed_varint64(rider->second.game.unique_id);
                    link.byte(1);
                    link.boolean(false);
                    enqueue(id, packet(PacketId::SetActorLink, link.take()));
                }
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
        for (const auto &m : mobs_)
            if (m.runtime_id == *it)
                unique = m.unique_id;
        if (auto other = sessions_.find(*it); other != sessions_.end())
            unique = other->second.game.unique_id;
        enqueue(id, packet(PacketId::RemoveActor, remove_actor(unique)));
        it = s.known.erase(it);
    }
}
void Server::set_chunk_radius(std::uint64_t runtime, int radius) {
    if (auto found = sessions_.find(runtime); found != sessions_.end())
        found->second.chunk_radius = static_cast<unsigned>(radius);
}
void Server::background_tick_seasons() {
    // Dimension::backgroundTickSeasons (0x1d64680): the overworld has weather;
    // TickingAreaList::tickSeasons has no ticking areas here; then for every
    // player of the level's active list (Dimension::forEachPlayer 0x1d64760,
    // the shuffled Level +0x50 order) the callback 0x1d694b0 tries ten points
    // around the player and post-processes the first loaded chunk whose seasons
    // state is dirty.
    constexpr float tau = std::bit_cast<float>(0x40c90fdbu); // mce::Math::TAU
    for (const auto id : player_order_) {
        const auto found = sessions_.find(id);
        if (found == sessions_.end())
            continue;
        const auto &p = world_.players().at(found->second.key);
        const float radius = static_cast<float>(found->second.chunk_radius) * 0.7f;
        for (int attempt = 0; attempt < 10; ++attempt) {
            const float distance = level_random_.next_float(0.0f, radius * 16.0f);
            const float angle = level_random_.next_float(0.0f, tau);
            // Vec3(distance, 0, 0).yRot(angle) (0x1d768b0): double libm cos/sin
            // rounded to float.
            const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
            const float sn = static_cast<float>(std::sin(static_cast<double>(angle)));
            const float x = distance * c + 0.0f * sn, z = 0.0f * c - distance * sn;
            const int bx = static_cast<int>(std::floor(p.x + x)), bz = static_cast<int>(std::floor(p.z + z));
            // LevelChunk::checkSeasonsPostProcessDirty (0x1dea0f0): a biome of
            // the chunk whose snow state differs from
            // Biome::getSnowAccumulationLayers. BDS-RE's terrain has no snow
            // accumulating biomes, so a loaded chunk is never dirty and
            // applySeasonsPostProcess never runs.
            const bool loaded = world_.chunk(bx >> 4, bz >> 4) != nullptr;
            const bool dirty = false;
            if (loaded && dirty)
                break;
        }
    }
}
void Server::save_some_chunks() {
    // Level::_saveSomeChunks (0x1c86c30). It returns while a save task is
    // queued (+0x19b0), without level storage or without dimensions. Up to 30
    // attempts: nextInt(dimension count) (always one draw, also for a single
    // dimension), then MainChunkSource::getRandomChunk (0x1e22a80): an empty
    // storage map returns null without drawing, otherwise nextInt(map size)
    // selects the chunk in unordered_map iteration order, which is returned
    // only at ChunkState >= 8. The first chunk for which
    // LevelChunk::needsSaving(60, 300) (0x1de83d0) holds is queued and the loop
    // stops; a non-empty queue sets +0x19b0 and posts the save task.
    if (!overworld_created_)
        return;
    for (int attempt = 0; attempt < 30; ++attempt) {
        level_random_.next_int(1);
        const auto stored = world_.loaded_chunks();
        if (stored == 0)
            continue;
        level_random_.next_int(static_cast<std::int32_t>(stored));
        // needsSaving checks the chunk's 21 DirtyTicksCounters (touched by
        // setBlock, add/removeEntity, ticking-queue changes; set to max by
        // LevelChunk::setUnsaved after post-processing; incremented by
        // LevelChunk::tick). BDS-RE does not keep those counters or chunk
        // states, so no chunk is selected and all 30 attempts run, as in the
        // original before its first post-processed chunk. The save task then
        // completes on a worker thread, so the ticks it skips are not
        // deterministic in the original either.
    }
}
class Server::Region final : public SpawnRegion {
  public:
    explicit Region(Server &server) : s_(server) {}
    bool has_chunk(int x, int z) const override { return s_.world_.chunk(x, z) != nullptr; }
    BlockId block(World::Position at) const override {
        const auto [x, y, z] = at;
        if (y < 0 || y >= static_cast<int>(Chunk::height) || !s_.world_.chunk(x >> 4, z >> 4))
            return 0;
        return s_.world_.block(at);
    }
    unsigned raw_brightness(World::Position at) const override {
        const auto [x, y, z] = at;
        if (!s_.world_.chunk(x >> 4, z >> 4))
            return 0; // INFERRED: no loaded chunk reads as dark
        return s_.lighting_.raw(at, clear_sky_darken(static_cast<int>(s_.level_tick_ % 24000)));
    }
    bool sky(World::Position at) const override { return can_see_sky(s_.world_, at); }
    std::optional<float> nearest_player_distance_sqr(float x, float y, float z, float max) const override {
        // Dimension::fetchNearestPlayer $_4 (0x1d6a3c0): best starts at -1;
        // a player within `max` (any when max < 0) closer than the best wins.
        float best = -1.0f;
        for (const auto id : s_.player_order_)
            if (const auto found = s_.sessions_.find(id); found != s_.sessions_.end()) {
                const auto &p = s_.world_.players().at(found->second.key);
                const float dx = static_cast<float>(p.x) - x, dy = static_cast<float>(p.y) - y,
                            dz = static_cast<float>(p.z) - z;
                const float d = dx * dx + dy * dy + dz * dz; // Actor::distanceToSqr
                if ((max < 0.0f || d < max * max) && (best == -1.0f || d < best))
                    best = d;
            }
        if (best == -1.0f)
            return std::nullopt;
        return best;
    }
    int difficulty() const override { return static_cast<int>(s_.properties_.difficulty); }
    int ticked_mob_count_previous() const override { return s_.ticked_mobs_previous_; }
    void chunk_mobs(int x, int z, const std::function<void(std::uint32_t, bool)> &visit) const override {
        for (const auto id : s_.chunk_actors_.members({x, z}))
            for (const auto &mob : s_.mobs_)
                if (mob.runtime_id == id && !mob.removed)
                    visit(mob.data->type, mob.surface);
    }
    std::optional<std::uint64_t> spawn_mob(const MobSpawnerData &data, float x, float y, float z,
                                           bool surface) override {
        // Mob::checkSpawnRules (0x22bb790): no solid block and no liquid in
        // the collision box, no magma floor, Animal::_getWalkTargetValue >= 0
        // (grass below: 10), BlockLegacy::canSpawnOn (true).
        const float half = data.width / 2.0f;
        const int x0 = static_cast<int>(std::floor(x - half)), x1 = static_cast<int>(std::floor(x + half));
        const int z0 = static_cast<int>(std::floor(z - half)), z1 = static_cast<int>(std::floor(z + half));
        const int y0 = static_cast<int>(std::floor(y)), y1 = static_cast<int>(std::floor(y + data.height));
        for (int bx = x0; bx <= x1; ++bx)
            for (int by = y0; by <= y1; ++by)
                for (int bz = z0; bz <= z1; ++bz)
                    if (spawn_block_solid_blocking(block({bx, by, bz})))
                        return std::nullopt;
        MobActor mob;
        mob.runtime_id = static_cast<std::uint64_t>(s_.allocate_entity_id());
        // Level::getNewUniqueID: the original's mob unique ids carry 0xffffffff
        // in the high half (capture: runtime 4 -> unique -4294967292).
        mob.unique_id = static_cast<std::int64_t>(0xffffffff00000000ULL | mob.runtime_id);
        mob.data = &data;
        mob.pos = {x, y, z};
        // Actor::setPos/AABBShapeComponent: collision box uses definition
        // dimensions (native cow/pig/sheep traces); scale is render metadata.
        mob.bb={{x-half,y,z-half},{x+half,y+data.height,z+half}};
        mob.surface = surface;
        define(mob);
        mob.width = data.width * mob.scale;
        mob.height = data.height * mob.scale;
        const float scaled_half = mob.width / 2.0f;
        mob.bb = {{x - scaled_half, y, z - scaled_half}, {x + scaled_half, y + mob.height, z + scaled_half}};
        mob.attributes.values[kMovement].value = mob.attributes.values[kMovement].fallback = mob.movement;
        mob.attributes.values[kHealth].value = mob.attributes.values[kHealth].fallback = mob.health;
        mob.attributes.values[kHealth].max = mob.attributes.values[kHealth].default_max = mob.max_health;
        mob.brain = make_brain(data.type, mob.baby, mob.width, mob.height);
        mob.sent_pos = mob.pos;
        mob.owner_chunk = {static_cast<int>(std::floor(x)) >> 4, static_cast<int>(std::floor(z)) >> 4};
        s_.chunk_actors_.add(mob.owner_chunk, mob.runtime_id); // LevelChunk::addEntity
        s_.mobs_.push_back(mob);
        return mob.runtime_id;
    }
    void set_variant(std::uint64_t id, int variant, int mark) override {
        for (auto &mob : s_.mobs_)
            if (mob.runtime_id == id) {
                mob.variant = variant;
                mob.mark_variant = mark;
            }
    }

  private:
    // The vanilla behavior pack's minecraft:entity_spawned event (entities/
    // <name>.json): a weighted adult/baby choice (baby: scale 0.5 where the
    // group sets it), sheep colours, horse/donkey health and speed ranges.
    // Definition events draw from Actor's own Random (+0x2b0).
    void define(MobActor &mob) {
        auto &random = mob.random;
        const auto pick = [&](std::initializer_list<int> weights) {
            int total = 0;
            for (const auto w : weights)
                total += w;
            int roll = random.next_int(total), index = 0;
            for (const auto w : weights) {
                if ((roll -= w) < 0)
                    return index;
                ++index;
            }
            return index - 1;
        };
        const auto type = mob.data->type;
        mob.movement = 0.25f;
        if (type == actor_type::Horse || type == actor_type::Donkey) {
            mob.baby = pick(type == actor_type::Horse ? std::initializer_list<int>{36, 9}
                                                     : std::initializer_list<int>{80, 20}) == 1;
            mob.scale = mob.baby ? 0.5f : 1.0f;
            mob.max_health = mob.health = static_cast<float>(random.next_int(15, 31));
            mob.jump_strength = random.next_float(0.4f, 1.0f);
            mob.movement = type == actor_type::Horse ? random.next_float(0.1125f, 0.3375f) : 0.175f;
            return;
        }
        mob.baby = pick({95, 5}) == 1;
        mob.scale = mob.baby ? 0.5f : 1.0f;
        if (type == actor_type::Sheep) {
            mob.max_health = 8;
            static constexpr std::uint8_t colors[]{0, 15, 7, 8, 12, 6};
            mob.color = colors[pick({81836, 5000, 5000, 5000, 3000, 164})];
        } else if (type == actor_type::Chicken) {
            mob.max_health = 4;
            mob.fall_damage = 0; // minecraft:fall_damage value 0
        } else
            mob.max_health = 10; // pig, cow
        mob.health = mob.max_health;
    }
    Server &s_;
};
std::optional<std::uint64_t> Server::summon(std::uint32_t type, Vec3 at, bool adult) {
    const auto &mobs = plains_mobs();
    const auto data = std::find_if(mobs.begin(), mobs.end(), [&](const MobSpawnerData &d) { return d.type == type; });
    if (data == mobs.end())
        return std::nullopt;
    Region region(*this);
    const auto id = region.spawn_mob(*data, at.x, at.y, at.z, true);
    if (id && adult)
        for (auto &mob : mobs_)
            if (mob.runtime_id == *id && mob.baby) {
                mob.baby = false;
                mob.scale = 1.0f;
                mob.width = data->width;
                mob.height = data->height;
                const float half = mob.width / 2.0f;
                mob.bb = {{at.x - half, at.y, at.z - half}, {at.x + half, at.y + mob.height, at.z + half}};
                mob.brain = make_brain(type, false, mob.width, mob.height);
            }
    return id;
}
bool Server::tick_chunk_state(int chunk_x, int chunk_z) {
    auto &state = chunk_state_[{chunk_x, chunk_z}];
    if (state == 0) {
        // 0x1de403b..0x1de4144: once BlockSource::hasChunksAt covers the chunk
        // bounds grown by 1.0 (the 3x3 neighbourhood), the pending block ticks
        // run (none in BDS-RE: tickPendingTicks returns false) and the state
        // becomes 1. Nothing else happens on this tick.
        bool neighbours = true;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                neighbours = neighbours && world_.chunk(chunk_x + dx, chunk_z + dz) != nullptr;
        if (neighbours)
            state = 1;
        return false;
    }
    if (state == 1) {
        // 0x1de438c..0x1de4418: Spawner::postProcessSpawnMobs with the level
        // Random, WorldGenerator::postProcessMobsAt (no draws for the flat
        // generator), LevelChunk::setUnsaved, state 2; the tick continues.
        Region region(*this);
        spawner_.post_process_spawn_mobs(region, chunk_x, chunk_z, level_random_);
        state = 2;
    }
    return true;
}
class Server::Senses final : public MobSenses {
  public:
    Senses(Server &server, MobActor &mob) : s_(server), mob_(mob) {}
    Random &level_random() override { return s_.level_random_; }
    std::vector<NearbyPlayer> players_in(float grow) override {
        // BlockSource::fetchEntities(mob, mob AABB grown by `grow`): actors
        // whose box intersects (AABB::intersects, strict); the player box is
        // 0.6 x 1.8 from the feet (eye position - 1.62). Sorted by distance
        // (LookAtActorGoal::canUse std::sort on distanceToSqr).
        const auto &d = mob_;
        const float half = d.width / 2.0f;
        const AABB area{{mob_.pos.x - half - grow, mob_.pos.y - grow, mob_.pos.z - half - grow},
                        {mob_.pos.x + half + grow, mob_.pos.y + d.height + grow, mob_.pos.z + half + grow}};
        std::vector<NearbyPlayer> out;
        for (const auto id : s_.player_order_)
            if (const auto p = player(id)) {
                const AABB box{{p->pos.x - 0.3f, p->pos.y - 1.62f, p->pos.z - 0.3f},
                               {p->pos.x + 0.3f, p->pos.y - 1.62f + 1.8f, p->pos.z + 0.3f}};
                if (area.intersects(box))
                    out.push_back(*p);
            }
        std::stable_sort(out.begin(), out.end(),
                         [](const NearbyPlayer &a, const NearbyPlayer &b) { return a.distance_sqr < b.distance_sqr; });
        return out;
    }
    std::optional<NearbyPlayer> player(std::uint64_t id) override {
        const auto found = s_.sessions_.find(id);
        if (found == s_.sessions_.end() || !found->second.active)
            return std::nullopt;
        const auto &p = s_.world_.players().at(found->second.key);
        const Vec3 pos{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
        const float dx = pos.x - mob_.pos.x, dy = pos.y - mob_.pos.y, dz = pos.z - mob_.pos.z;
        return NearbyPlayer{id, pos, dx * dx + dy * dy + dz * dz, p.health > 0};
    }
    BlockId block(World::Position at) override {
        const auto [x, y, z] = at;
        if (y < 0 || y >= static_cast<int>(Chunk::height) || !s_.world_.chunk(x >> 4, z >> 4))
            return 0;
        return s_.world_.block(at);
    }
    std::optional<Vec3> stroll_position(int xz, int y) override {
        return random_stroll_position(s_.world_, s_.lighting_, mob_.pos, mob_.random, xz, y,
                                      clear_sky_darken(static_cast<int>(s_.level_tick_ % 24000)));
    }
    std::optional<Vec3> last_attacker() const override {
        if (!mob_.hurt_by_mob)
            return std::nullopt;
        const auto found = s_.sessions_.find(*mob_.hurt_by_mob);
        if (found == s_.sessions_.end())
            return std::nullopt;
        const auto &p = s_.world_.players().at(found->second.key);
        return Vec3{p.x, p.y, p.z}; // Actor::getPos (eye position for players)
    }
    std::optional<Vec3> avoid_position(int xz, int y, Vec3 avoid) override {
        const Vec3 direction{mob_.pos.x - avoid.x, mob_.pos.y - avoid.y, mob_.pos.z - avoid.z};
        return random_stroll_position(s_.world_, s_.lighting_, mob_.pos, mob_.random, xz, y,
                                      clear_sky_darken(static_cast<int>(s_.level_tick_ % 24000)), &direction);
    }
    bool in_liquid() const override { return travel_medium(mob_, s_.world_) != TravelMedium::Land; }
    void float_jump() override {
        mob_.jump_control.jump();
        const auto p = mob_.pos;
        if (mob_.rider && is_water(s_.world_.block({int(std::floor(p.x)), int(std::floor(p.y + mob_.height * 0.85f)),
                                           int(std::floor(p.z))})))
            s_.dismount(*mob_.rider);
    }
    bool navigation_done() const override { return mob_.navigation.done(); }
    void navigate(Vec3 at, float speed) override {
        if (mob_.on_ground)
            mob_.navigation.move_to(s_.world_, mob_, at, speed, mob_.width, mob_.height);
    }
    void stop_navigation() override { mob_.navigation.stop(); }
    bool navigation_close(float distance) const override {
        return mob_.navigation.close_to_done(mob_.pos, distance);
    }
    std::optional<World::Position> navigation_end() const override {
        const auto &nav = mob_.navigation;
        if (!nav.path || nav.path->nodes.empty())
            return std::nullopt;
        const auto end = nav.path->position(mob_.width, nav.path->nodes.size() - 1);
        return World::Position{int(std::floor(end.x)), int(std::floor(end.y)), int(std::floor(end.z))};
    }
    void eat_start() override {
        // Level::broadcastEntityEvent(mob, 10 EAT_GRASS_ANIMATION, 0).
        s_.send_for_entity(mob_.runtime_id, packet(PacketId::ActorEvent, actor_event(mob_.runtime_id, 10, 0)));
    }
    void eat_grass(World::Position at) override {
        // broadcastDimensionEvent(ParticlesDestroyBlock, pos + 0.5, grass
        // runtime id), then BlockSource::setBlock(pos, dirt, 3).
        const auto [x, y, z] = at;
        s_.broadcast(level_event(LevelEventParticlesDestroyBlock, x + .5f, y + .5f, z + .5f,
                                 static_cast<int>(block_runtime_id(2))));
        s_.world_.set_block(at, 3);
        s_.broadcast(packet(PacketId::UpdateBlock, block_update(at, 3)));
    }

  private:
    Server &s_;
    MobActor &mob_;
};
void Server::tick_mob(MobActor &mob) {
    // Actor::normalTick starts with Mob::baseTick (hurt time, last attacker
    // timers, death and Mob::tickDeath).
    base_tick_mob(mob);
    if (mob.removed)
        return;
    update_water_current(mob, world_);
    // Actor::tick -> Mob::normalTick (0x22b2c60): Actor::normalTick runs the
    // components (SpawnActorComponent::tick, the egg timer), then aiStep ->
    // Mob::newServerAiStep (GoalSelector::tick, LookControl::tick), then the
    // body turn (BodyControl::clientTick for useNewAi mobs).
    if (tick_spawn_actor(mob.brain, level_random_)) {
        // NOT TRANSLATED: SpawnActorComponent::spawnEntity (the egg item and
        // the "plop" sound).
    }
    if(!mob.attributes.dirty.empty()) {
        send_for_entity(mob.runtime_id,packet(PacketId::UpdateAttributes,
            update_attributes(mob.runtime_id,mob.attributes,mob.attributes.dirty)));
        mob.attributes.dirty.clear();
    }
    if (mob.effect_keys) {
        send_for_entity(mob.runtime_id,
                        packet(PacketId::SetActorData,
                               effect_data(mob.runtime_id, mob.effect_color, false, mob.effect_keys)));
        mob.effect_keys = 0;
    }
    tick_effects(mob.runtime_id, mob);
    position_rider(mob); // Actor::_manageRiders precedes Mob::normalTick.
    // Mob::aiStep 0x22b9f85: an immobile mob (Mob::isImmobile: health <= 0)
    // stops jumping, clears its inputs and motion and skips the AI step.
    const bool immobile = mob.attributes[kHealth] <= 0.0f;
    if (immobile) {
        mob.jump_control.jumping = false;
        mob.move_control.forward = 0.0f;
        mob.motion = {};
    } else {
        Senses senses(*this, mob);
        tick_goals(mob.brain, mob.pos, senses);
        // Mob::newServerAiStep: goals, navigation, MoveControl, LookControl,
        // JumpControl, in that order (0x22bad00).
        mob.navigation.tick(world_, mob, mob.width, mob.height, mob.move_control);
        mob.move_control.tick(mob, mob.brain, mob.attributes[kMovement], mob.jump_control);
        tick_look(mob.brain, mob.pos);
    }
    mob.jump_control.tick();
    if (mob.jump_delay > 0)
        --mob.jump_delay;
    const auto medium = travel_medium(mob, world_);
    if (medium == TravelMedium::Land && mob.jump_control.jumping && mob.on_ground && mob.jump_delay == 0) {
        mob.motion.y = 0.42f;
        if (mob.effects[8].id)
            mob.motion.y += 0.1f * static_cast<float>(mob.effects[8].amplifier + 1);
        mob.jump_delay = 10;
    } else if (!mob.jump_control.jumping)
        mob.jump_delay = 0;
    mob.pos_prev=mob.pos;
    TravelOptions options;
    options.medium = medium;
    if (options.medium != TravelMedium::Land && mob.jump_control.jumping)
        mob.motion.y += 0.04f;
    options.underwater_speed = mob.attributes[kUnderwaterMovement];
    options.levitation = mob.effects[24].id ? mob.effects[24].amplifier : -1;
    options.slow_falling = mob.effects[27].id != 0;
    position_rider(mob); // Mob::travel positions riders before moving the vehicle.
    Vec3 input{0, 0, mob.move_control.forward * 0.98f};
    if (mob.rider && mob.tamed && mob.saddled) {
        const auto rider = sessions_.find(*mob.rider);
        if (rider != sessions_.end())
            input = ridden_input(mob, mob.brain, rider->second.rider_input, mob.attributes[kMovement],
                                 mob.move_control.speed, mob.air_speed, mob.jump_power, mob.jump_strength,
                                 mob.effects[8].id ? mob.effects[8].amplifier : -1);
    }
    travel_mob(mob, world_, input, mob.brain.yaw, mob.move_control.speed, mob.air_speed, 1.0f, true, options);
    push_mob_entities(mob); // Mob::aiStep -> pushEntities, 0x22ba628
    tick_body(mob.brain, mob.motion.x, mob.motion.z);
    // Actor::sendMotionPacketIfNeeded: SpatialActorNetworkData::shouldSendUpdate
    // -> MoveActorAbsoluteData::isDifferenceSignificant (0x1919f70): the sum of
    // the three rotation differences (PacketUtil::byteToDegrees of the sent
    // bytes) above 4, or the position thresholds used for items.
    const auto to_byte = [](float degrees) { // PacketUtil::degreesToByte 0x18a0f40
        return static_cast<std::int8_t>(static_cast<std::int32_t>(degrees / 360.0f * 256.0f));
    };
    const auto to_degrees = [](std::int8_t b) { return static_cast<float>(b) / 256.0f * 360.0f; };
    const std::int8_t rotation[3]{to_byte(mob.brain.pitch), to_byte(mob.brain.yaw), to_byte(mob.brain.head_yaw)};
    float turned = 0;
    for (int i = 0; i < 3; ++i)
        turned += std::fabs(to_degrees(rotation[i]) - to_degrees(mob.sent_rotation[i]));
    const Vec3 d{mob.pos.x - mob.sent_pos.x, mob.pos.y - mob.sent_pos.y, mob.pos.z - mob.sent_pos.z};
    const float moved2 = d.x * d.x + d.y * d.y + d.z * d.z;
    const float speed2 = mob.motion.x * mob.motion.x + mob.motion.y * mob.motion.y + mob.motion.z * mob.motion.z;
    const float tiny = std::bit_cast<float>(0x358637beU);
    if (turned > 4.0f || moved2 > 1e-4f || (tiny > speed2 && moved2 > tiny)) {
        ByteWriter out;
        out.varint64(mob.runtime_id);
        const std::int32_t delta[3] = {
            static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(mob.pos.x) - std::bit_cast<std::uint32_t>(mob.sent_pos.x)),
            static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(mob.pos.y) - std::bit_cast<std::uint32_t>(mob.sent_pos.y)),
            static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(mob.pos.z) - std::bit_cast<std::uint32_t>(mob.sent_pos.z))};
        std::uint8_t flags = mob.sent_on_ground ? 0x40 : 0;
        for (int i = 0; i < 3; ++i) {
            if (delta[i] != 0)
                flags |= static_cast<std::uint8_t>(1U << i);
            if (rotation[i] != mob.sent_rotation[i])
                flags |= static_cast<std::uint8_t>(8U << i);
        }
        out.byte(flags);
        for (int i = 0; i < 3; ++i)
            if (flags & (1U << i))
                out.signed_varint(delta[i]);
        for (int i = 0; i < 3; ++i)
            if (flags & (8U << i))
                out.byte(static_cast<std::uint8_t>(rotation[i]));
        send_for_entity(mob.runtime_id, packet(PacketId::MoveActorDelta, out.take()));
        mob.sent_pos = mob.pos;
        std::copy(std::begin(rotation), std::end(rotation), mob.sent_rotation);
        mob.sent_on_ground = mob.on_ground;
    }
    send_mob_motion(mob);
}
void Server::push_mob_entities(MobActor &mob) {
    // Mob::pushEntities 0x22bfa60: grow the box 0.2 in X/Z, fetch chunk
    // actors in region order, then invoke the other actor's push(this,false).
    // Non-riding, pushable mobs only; player/item/vehicle branches are pending.
    const AABB area{{mob.bb.min.x-0.2f,mob.bb.min.y,mob.bb.min.z-0.2f},
                    {mob.bb.max.x+0.2f,mob.bb.max.y,mob.bb.max.z+0.2f}};
    const int x0=chunk_coordinate(static_cast<int>(std::floor(area.min.x-2.0f)));
    const int x1=chunk_coordinate(static_cast<int>(std::floor(area.max.x+2.0f)));
    const int z0=chunk_coordinate(static_cast<int>(std::floor(area.min.z-2.0f)));
    const int z1=chunk_coordinate(static_cast<int>(std::floor(area.max.z+2.0f)));
    // Server-side BlockSource::fetchEntities, 0x1c5be17: chunk bounds
    // include an extra 2 blocks in X/Z, retaining chunk SmallSet order.
    for(int x=x0;x<=x1;++x) for(int z=z0;z<=z1;++z) {
        if(!world_.chunk(x,z)) continue;
        for(const auto id:chunk_actors_.members({x,z})) {
            if(id==mob.runtime_id) continue;
            auto other=std::find_if(mobs_.begin(),mobs_.end(),[&](const MobActor &a){return a.runtime_id==id;});
            if(other!=mobs_.end() && !other->removed && other->bb.intersects(area))
                push_actors(*other,mob,false);
        }
    }
}
void Server::tick_spawner(int chunk_x, int chunk_z) {
    // LevelChunk::tick 0x1de4433..0x1de4501: Spawner::tick runs only when the
    // chunk centre ((min + max) / 2 of its block bounds, Y cleared) is closer
    // than 96 blocks (9216 squared) to the nearest player of the dimension
    // (Dimension::distanceToNearestPlayerSqr2D 0x1d672a0: starts at FLT_MAX,
    // min over forEachPlayer of Vec3::distanceToSqr with the player's Y
    // cleared).
    const auto centre = [](int c) { return static_cast<float>((c * 16 + c * 16 + 15) / 2); };
    const float cx = centre(chunk_x), cz = centre(chunk_z);
    float nearest = std::numeric_limits<float>::max();
    for (const auto id : player_order_)
        if (const auto found = sessions_.find(id); found != sessions_.end()) {
            const auto &p = world_.players().at(found->second.key);
            const float dx = static_cast<float>(p.x) - cx, dz = static_cast<float>(p.z) - cz;
            nearest = std::min(nearest, dx * dx + 0.0f * 0.0f + dz * dz);
        }
    if (nearest < 9216.0f) {
        // Spawner::tick (0x1c6c020): GameRules::getBool(5, doMobSpawning;
        // BDS-RE has no gamerule command, so it is the default true), then
        // nextInt(2000) > 10 returns.
        if (level_random_.next_int(2000) <= 10) {
            Region region(*this);
            spawner_.tick_spawn(region, chunk_x, chunk_z, level_random_);
        }
    }
    // Spawner::tickDespawn (0x1c6c460): nextInt(50) > 0 returns; otherwise a
    // non-empty chunk entity set picks one with nextInt(0, size). Only a Mob
    // (ActorCategory 2) that may despawn (vtable +0x840) with no player within
    // MobSpawnRules::MAX_SPAWN_DISTANCE is removed; items never are. Players
    // are owned by the Level, not by chunks.
    if (level_random_.next_int(50) > 0)
        return;
    const auto members = chunk_actors_.members({chunk_x, chunk_z});
    if (!members.empty())
        level_random_.next_int(0, static_cast<std::int32_t>(members.size()));
}
void Server::tick() {
    // Dimension::_tickEntityChunkMoves (0x1d65130) runs before players.
    // Actor::_updateOwnerChunk queues changes before its normalTick, so an
    // item crossing during normalTick keeps its old owner for the next tick.
    for (const auto id : pending_actor_moves_) {
        for (auto &actor : mobs_)
            if(actor.runtime_id==id && !actor.removed) {
                const auto target=TickChunkPosition{chunk_coordinate(static_cast<int>(std::floor(actor.pos.x))),
                                                    chunk_coordinate(static_cast<int>(std::floor(actor.pos.z)))};
                chunk_actors_.move(actor.owner_chunk,target,id);
                actor.owner_chunk=target;
                break;
            }
        for (auto &actor : items_)
            if (actor.runtime_id == id && !actor.removed) {
                const auto target = item_chunk(actor);
                // INFERRED host boundary for an unloaded target: keep its
                // inactive record in that column. The original saves/transfers
                // through NBT; actor persistence is not implemented here.
                // Missing columns are neither simulated nor announced below.
                chunk_actors_.move(actor.owner_chunk, target, id);
                actor.owner_chunk = target;
                break;
            }
    }
    pending_actor_moves_.clear();
    ticked_mobs_previous_ = ticked_mobs_;
    ticked_mobs_ = 0;
    // Level::tick (0x1c86000): when the current tick is a multiple of 20 it
    // std::shuffles, with the level Random, the two chunk-offset lists
    // (Level +0x8, and +0x20 refreshed from gMasterClientTickingChunksOffsetList;
    // both hold the client ticking offsets) and then the player list
    // (Level +0x50, Level::addPlayer appends, removal keeps the order). The
    // players then tick in that list's order (Level::forEachPlayer ->
    // ServerPlayer::tickWorld). The original's level Random is also drawn by
    // mob spawning/despawning, mob AI, seasons and saves, still missing here.
    if (level_tick_ % 20 == 0) {
        const auto shuffle = [&](auto &list) {
            for (std::size_t i = 1; i < list.size(); ++i) {
                const std::uint64_t buckets = i + 1, scaling = 0xffffffffULL / buckets, past = buckets * scaling;
                std::uint64_t r;
                do
                    r = level_random_.next_uint();
                while (r >= past);
                std::swap(list[i], list[r / scaling]);
            }
        };
        if (chunk_offsets_.empty())
            chunk_offsets_ = ticking_offsets();
        auto client_offsets = ticking_offsets();
        shuffle(chunk_offsets_);
        shuffle(client_offsets);
        shuffle(player_order_);
    }
    // Dimension::tick (0x1d64e50): weather and seasons, then
    // backgroundTickSeasons (level Random), then the entity chunk moves.
    background_tick_seasons();
    ++level_tick_;
    const auto players = player_order_;
    tick_player_worlds(players, chunk_offsets_, {
        [this](std::uint64_t id) -> std::optional<TickChunkPosition> {
            auto player = sessions_.find(id);
            if (player == sessions_.end() || !player->second.active)
                return {};
            return center(id);
        },
        [this](TickChunkPosition chunk) { return world_.chunk(chunk.first, chunk.second) != nullptr; },
        [this](TickChunkPosition chunk) {
            // LevelChunk::tick: Spawner::tick (0x1de4501), tickDespawn
            // (0x1de451d), tickBlocks (0x1de452d), then the actor phase.
            if (!tick_chunk_state(chunk.first, chunk.second))
                return;
            tick_spawner(chunk.first, chunk.second);
            tick_clear_chunk(world_,lighting_,chunk.first,chunk.second,level_random_,block_tick_random_,
                             clear_sky_darken(static_cast<int>(level_tick_%24000)),
                             [this](World::Position at,BlockId id,int) {
                                 broadcast(packet(PacketId::UpdateBlock,block_update(at,id)));
                             });
            world_.tick_water_chunk(chunk.first, chunk.second);
            // LevelChunk::tick snapshots its actor pointers before iterating.
            // Block consumer call site: 0x1de452d.
            const auto actors = chunk_actors_.members(chunk);
            for (const auto id : actors) {
                if (const auto mob = std::find_if(mobs_.begin(), mobs_.end(),
                                                  [&](const MobActor &m) { return m.runtime_id == id; });
                    mob != mobs_.end()) {
                    if (!mob->removed) {
                        const auto current=TickChunkPosition{chunk_coordinate(static_cast<int>(std::floor(mob->pos.x))),
                                                            chunk_coordinate(static_cast<int>(std::floor(mob->pos.z)))};
                        if(!world_.chunk(current.first,current.second)) continue;
                        if(current!=mob->owner_chunk) pending_actor_moves_.push_back(id);
                        ++ticked_mobs_; // INFERRED: counted for getTickedMobCountPrevious
                        tick_mob(*mob);
                    }
                    continue;
                }
                for (auto &actor : items_)
                    if (actor.runtime_id == id && !actor.removed) {
                        // Actor::tick (0x21adc77): no normalTick when the
                        // actor's current position has no loaded block source.
                        const auto current = item_chunk(actor);
                        if (!world_.chunk(current.first, current.second))
                            break;
                        if (current != actor.owner_chunk)
                            pending_actor_moves_.push_back(id); // 0x21aa419
                        tick_item(actor);
                        break;
                    }
            }
        },
        [this](std::uint64_t id) {
            auto found = sessions_.find(id);
            if (found != sessions_.end() && found->second.active) {
                tick_player(id, found->second);
                update_nearby(id, found->second); // ServerPlayer::tickWorld, 0x25f8bd3
            }
        }
    });
    for (const auto &update : world_.take_water_updates())
        broadcast(packet(PacketId::UpdateBlock, block_update(update.position, update.block, update.flags)));
    for (const auto &actor : items_)
        if (actor.removed)
            chunk_actors_.erase(actor.owner_chunk, actor.runtime_id);
    // Removed mobs (Mob::tickDeath -> Actor::remove) leave their chunk and
    // the level after the players' _updateNearbyEntities sent RemoveActor.
    for (const auto &mob : mobs_)
        if (mob.removed)
            chunk_actors_.erase(mob.owner_chunk, mob.runtime_id);
    std::erase_if(mobs_, [](const MobActor &m) { return m.removed; });
    std::erase_if(items_, [](const ItemActor &a) { return a.removed; });
    fresh_items_.clear();
    // Level::tick ends with Level::_saveSomeChunks (after the player worlds).
    save_some_chunks();
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
        return use_item(s.game.runtime_id, s, t);
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
        if (std::get<1>(target) >= 0 && std::get<1>(target) < 256 && block < protocol::internal_block_palette().size())
            out.push_back(packet(PacketId::UpdateBlock, block_update(target, block)));
        return out;
    };
    if (s.game.player_game_type == 2 || s.game.settings.default_player_permission == 0 ||
        item_info(held.block).kind != ItemKind::Block || held != t.held || !world_.block(t.block) ||
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
            if (s.vehicle && (!std::isfinite(m.data.yaw) || !std::isfinite(m.data.pitch)))
                return {};
            s.rider_input.yaw = std::isfinite(m.data.yaw) ? wrap_degrees(m.data.yaw) : 0.0f;
            s.rider_input.pitch =
                std::isfinite(m.data.pitch) ? std::clamp(m.data.pitch, -90.0f, 90.0f) : 0.0f;
            if (s.vehicle) {
                p.yaw = m.data.yaw;
                p.pitch = m.data.pitch;
                p.head_yaw = m.data.head_yaw;
                return {};
            }
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
        if (type == PacketId::PlayerInput) {
            const float strafe = number(in), forward = number(in);
            const bool jumping = in.boolean(), sneaking = in.boolean();
            in.finish();
            if (s.vehicle) {
                s.rider_input.strafe = std::clamp(strafe, -1.0f, 1.0f);
                s.rider_input.forward = std::clamp(forward, -1.0f, 1.0f);
                if (sneaking)
                    dismount(id);
            }
            (void)jumping;
            return {};
        }
        if (type == PacketId::RiderJump) {
            const auto charge = in.signed_varint();
            in.finish();
            if (s.vehicle)
                for (auto &mob : mobs_)
                    if (mob.runtime_id == *s.vehicle && mob.rider == id && mob.tamed && mob.saddled)
                        mob.jump_power = player_jump_power(charge);
            return {};
        }
        if (type == PacketId::Interact) {
            const auto action = in.byte();
            const auto target = in.varint64();
            if (action == 4) {
                number(in);
                number(in);
                number(in);
            }
            in.finish();
            if (action == 3 && s.vehicle && *s.vehicle == target)
                dismount(id);
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
                    if (stack.block && stacks_with(stack, remaining) && stack.count < max_stack(stack)) {
                        const auto count = std::min<unsigned>(max_stack(stack) - stack.count, remaining.count);
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
                set_sprinting(s, action == 9);
                break;
            case 11: // StartSneak: Mob::setSneaking -> status flag 1; not relayed
            case 12: // StopSneak
                if (s.sneaking != (action == 11)) {
                    s.sneaking = action == 11;
                    s.flags_dirty = true;
                    if (s.sneaking && s.vehicle)
                        dismount(id);
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
            if (t.type == 3)
                return use_on_actor(id, s, t);
            if (t.type == 4)
                return release_item(id, s, t);
            require(t.type == 0);
            auto next = p.inventory;
            auto armor = p.armor;
            auto cursor = p.cursor;
            // Items entering and leaving the containers must balance, except
            // through the creative inventory (source 3) or the world (source
            // 2, flags 0: dropped, Player::drop).
            std::map<std::pair<std::uint16_t, std::int16_t>, int> balance;
            std::set<std::pair<int, unsigned>> touched;
            std::vector<ItemStack> dropped;
            bool creative = false;
            for (const auto &a : t.changes) {
                if (a.source == 3) {
                    require(s.game.player_game_type == 1 &&
                            s.game.settings.default_player_permission != 0);
                    creative = true;
                    continue;
                }
                if (a.source == 2) {
                    require(a.window == 0 && !a.before.block && a.after.block);
                    balance[{a.after.block, a.after.aux}] += a.after.count;
                    dropped.push_back(a.after);
                    continue;
                }
                require(a.source == 0 &&
                        ((a.window == 0 && a.slot < 36) || (a.window == 124 && a.slot == 0) ||
                         (a.window == 120 && a.slot < 4)) &&
                        touched.insert({a.window, a.slot}).second);
                auto &slot = a.window == 0 ? next[a.slot] : a.window == 120 ? armor[a.slot] : cursor;
                require(slot == a.before);
                // ArmorContainer: only armor of the slot's kind.
                require(a.window != 120 || !a.after.block ||
                        (item_info(a.after.block).kind == ItemKind::Armor &&
                         item_info(a.after.block).armor_slot == static_cast<int>(a.slot)));
                if (a.before.block)
                    balance[{a.before.block, a.before.aux}] -= a.before.count;
                if (a.after.block)
                    balance[{a.after.block, a.after.aux}] += a.after.count;
                slot = a.after;
            }
            require(creative || std::all_of(balance.begin(), balance.end(), [](const auto &b) { return b.second == 0; }));
            const bool armor_changed = armor != p.armor;
            p.inventory = next;
            p.armor = armor;
            p.cursor = cursor;
            for (const auto &item : dropped)
                drop(id, s, item, false); // Player::drop(item, false)
            if (armor_changed)
                send_for_entity(id, packet(PacketId::MobArmorEquipment, armor_equipment(id, p)));
            if (!dropped.empty() && t.changes.size() == dropped.size() + 1)
                return {}; // a drop from one slot: nothing to correct (original capture)
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
