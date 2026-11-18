// Effects, eating, armor, player combat and dropping, translated from BDS
// 1.6.0.15. Addresses are cited per function; the packets were compared with
// the original (tests/reference/combat_bds_1.6.0.15.json, tools/reference/
// combat_oracle.py).
#include "bds/game/effects.hpp"
#include "bds/game/mce_math.hpp"
#include "bds/game/server.hpp"
#include "bds/network/spawn.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace bds::game {
namespace {
// Actor::getHeightOffset of a player (0x3fcf5c7d, read under gdb).
constexpr float kHeightOffset = std::bit_cast<float>(0x3fcf5c7du);
constexpr int kPlayerType = 319; // ActorType Player

// The client's predicted container actions applied to a copy of the
// inventory; the transaction is accepted when they produce what the server
// did (InventoryTransaction verification). World-interaction and creative
// sources are not part of these transactions.
bool predicted(const PlayerData &before, const PlayerData &after, const Transaction &t) {
    auto inv = before.inventory;
    auto armor = before.armor;
    for (const auto &a : t.changes) {
        if (a.source != 0)
            return false;
        ItemStack *slot = nullptr;
        if (a.window == 0 && a.slot < 36)
            slot = &inv[a.slot];
        else if (a.window == 120 && a.slot < 4)
            slot = &armor[a.slot];
        if (!slot || *slot != a.before)
            return false;
        *slot = a.after;
    }
    return inv == after.inventory && armor == after.armor;
}
} // namespace

std::vector<GamePacket> Server::inventory_resend(const Session &s) const {
    // InventoryTransaction error: the containers and the hotbar again.
    const auto &p = world_.players().at(s.key);
    std::vector<GamePacket> out{packet(PacketId::InventoryContent, inventory(p)),
                                packet(PacketId::InventoryContent, armor_inventory(p)),
                                packet(PacketId::InventoryContent, cursor_inventory(p)),
                                packet(PacketId::InventoryContent, protocol::empty_inventory_body(119, 1))};
    ByteWriter hotbar;
    hotbar.varint(p.selected);
    hotbar.byte(0);
    hotbar.boolean(true);
    out.push_back(packet(PacketId::PlayerHotbar, hotbar.take()));
    return out;
}

void Server::set_sprinting(Session &s, bool sprinting) {
    // Mob::setSprinting (0x22bc1f0): status flag 3 and the movement modifier.
    if (s.sprinting == sprinting)
        return;
    s.sprinting = sprinting;
    s.flags_dirty = true;
    // The SPRINT_SPEED_BOOST modifier: +30% total on minecraft:movement.
    if (sprinting)
        s.attributes.add_modifier(kMovement, {kSprintModifier, 0.3f, 2, 2});
    else
        s.attributes.remove_modifier(kMovement, kSprintModifier);
}

// --- Effects ---------------------------------------------------------------

namespace {
// The attribute modifier of an effect (MobEffect +0xf0, dumped under gdb):
// attribute, base amount, operation, operand. The amount applied is
// MobEffect::getAttributeModifierValue = base * (amplifier + 1);
// AttackDamageMobEffect (0x1b180d0) uses 1.3 (strength) or -0.5 (weakness)
// instead of the stored base.
struct EffectModifier {
    int effect, attribute;
    float base;
    int operation, operand;
};
constexpr EffectModifier kEffectModifiers[] = {
    {1, kMovement, 0.2f, 2, 2},       // speed
    {2, kMovement, -0.15f, 2, 2},     // slowness
    {5, kAttackDamage, 1.3f, 2, 2},   // strength
    {18, kAttackDamage, -0.5f, 0, 2}, // weakness
    {21, kHealth, 4.0f, 0, 1},        // health boost: the maximum
};
} // namespace

void Server::apply_effect(std::uint64_t id, EffectState &s, const EffectState::Effect &e) {
    // MobEffect::applyEffects (0x1b0fb90): buffs, then modifiers;
    // AbsorptionMobEffect::applyEffects (0x1b183f0) first resets absorption.
    if (s.dead)
        return;
    const auto modifiers = [&] {
        for (const auto &m : kEffectModifiers)
            if (m.effect == e.id)
                s.attributes.add_modifier(m.attribute,
                                          {e.id, m.base * static_cast<float>(e.amplifier + 1), m.operation, m.operand});
    };
    const auto *info = effect_info(e.id);
    if (!info || info->buff.attribute < 0)
        return modifiers();
    const auto &b = info->buff;
    if (e.id == kAbsorptionEffect) {
        auto &v = s.attributes.values[kAbsorption];
        v.value = v.fallback; // AttributeInstance::resetToDefaultValue: always dirty
        s.attributes.mark(kAbsorption);
    }
    if (b.instant) {
        // _createInstantBuff: amount from the value amplifier (scale 1).
        const float amount = b.multiply != 0.0f ? b.multiply * static_cast<float>(e.amplifier + 1) : b.base;
        add_buff(id, s, b.attribute, amount, b.type);
        return modifiers();
    }
    // _createTemporalBuff (0x1b0ff70): TemporalAttributeBuff(base, duration),
    // amount base / duration, or base once a duration amplifier is set.
    TemporalBuff t;
    t.effect = e.id;
    t.amount = b.interval ? b.base : b.base / static_cast<float>(e.duration);
    t.type = b.type;
    t.duration = e.duration;
    t.amplification = e.amplifier;
    t.interval = b.interval;
    t.multiply = b.multiply;
    t.shifted = b.shifted;
    t.shifted_scale = b.shifted_scale;
    s.attributes.temporal[static_cast<std::size_t>(b.attribute)].push_back(t); // addBuff: push_back
    modifiers();
}

void Server::remove_effect_buffs(std::uint64_t, EffectState &s, int effect) {
    // MobEffect::removeEffects (0x1b10260): the effect's buffs go;
    // AbsorptionMobEffect::removeEffects (0x1b18390) resets absorption.
    for (auto &list : s.attributes.temporal)
        std::erase_if(list, [&](const TemporalBuff &b) { return b.effect == effect; });
    for (const auto &m : kEffectModifiers)
        if (m.effect == effect)
            s.attributes.remove_modifier(m.attribute, effect);
    if (effect == kAbsorptionEffect) {
        auto &v = s.attributes.values[kAbsorption];
        v.value = v.fallback;
        s.attributes.mark(kAbsorption);
    }
}

void Server::add_effect(std::uint64_t id, EffectState &s, int effect, int duration, int amplifier) {
    // Mob::addEffect (0x22b0630). ServerPlayer::onEffectAdded / Updated send
    // MobEffectPacket ADD (1) / MODIFY (2) through the packet sender (every
    // client).
    if (effect <= 0 || effect >= static_cast<int>(s.effects.size()))
        return;
    auto &e = s.effects[static_cast<std::size_t>(effect)];
    if (e.id) {
        // MobEffectInstance::update (0x1b0c380)
        if (amplifier > e.amplifier) {
            e.amplifier = amplifier;
            e.duration = duration;
        } else if (amplifier == e.amplifier && e.duration < duration)
            e.duration = duration;
        else if (e.ambient)
            e.ambient = false;
        e.visible = true;
        s.effects_dirty = true; // Mob::onEffectUpdated (0x22c0770)
        remove_effect_buffs(id, s, effect);
        apply_effect(id, s, e);
        broadcast(packet(PacketId::MobEffect, mob_effect(id, 2, effect, e.amplifier, e.visible, e.duration)));
        return;
    }
    e = {effect, duration, amplifier, false, true};
    s.effects_dirty = true; // Mob::onEffectAdded (0x22c0590)
    apply_effect(id, s, e);
    broadcast(packet(PacketId::MobEffect, mob_effect(id, 1, effect, amplifier, true, duration)));
}

void Server::remove_effect(std::uint64_t id, EffectState &s, int effect) {
    // Mob::removeEffect -> ServerPlayer::onEffectRemoved (0x25fec20): REMOVE (3).
    auto &e = s.effects[static_cast<std::size_t>(effect)];
    if (!e.id)
        return;
    e = {};
    s.effects_dirty = true;
    remove_effect_buffs(id, s, effect);
    broadcast(packet(PacketId::MobEffect, mob_effect(id, 3, effect, 0, false, 0)));
}

bool Server::give_effect(std::uint64_t runtime, int effect, int seconds, int amplifier) {
    const auto found = sessions_.find(runtime);
    if (seconds < 0 || seconds > 1000000 || amplifier < 0 || amplifier > 255 || !effect_info(effect))
        return false;
    if (found == sessions_.end() || !found->second.active) {
        if (effect != 1 && effect != 2 && effect != 8 && effect != 24 && effect != 27)
            return false;
        for (auto &mob : mobs_)
            if (mob.runtime_id == runtime && !mob.removed) {
                add_effect(runtime, mob, effect, seconds * 20, amplifier);
                return true;
            }
        return false;
    }
    add_effect(runtime, found->second, effect, seconds * 20, amplifier);
    return true;
}
bool Server::clear_effects(std::uint64_t runtime) {
    const auto found = sessions_.find(runtime);
    if (found == sessions_.end() || !found->second.active) {
        for (auto &mob : mobs_)
            if (mob.runtime_id == runtime && !mob.removed) {
                remove_all_effects(runtime, mob);
                return true;
            }
        return false;
    }
    remove_all_effects(runtime, found->second);
    return true;
}

void Server::remove_all_effects(std::uint64_t id, EffectState &s) {
    // Mob::removeAllEffects (0x22b1580): removeEffectParticles (data 8 and 9
    // to 0, dirty only where they change; 9 is never set here), every effect
    // removed in id order, removeEffectParticles again.
    if (s.effect_color != 0) {
        s.effect_color = 0;
        s.effect_keys |= 1;
    }
    for (std::size_t e = 1; e < s.effects.size(); ++e)
        if (s.effects[e].id)
            remove_effect(id, s, static_cast<int>(e));
}

void Server::tick_effects(std::uint64_t id, EffectState &s) {
    // Mob::tickEffects (0x22b2050): MobEffectInstance::tick counts the
    // duration down and the effect is removed when it reaches 0.
    for (std::size_t i = 1; i < s.effects.size(); ++i)
        if (s.effects[i].id && --s.effects[i].duration <= 0)
            remove_effect(id, s, static_cast<int>(i));
    if (!s.effects_dirty)
        return;
    // Mob::updateInvisibilityStatus (0x22c0380): synched data 8 is the
    // average colour of the visible effects, each counted amplifier + 1 times
    // (MobEffectInstance::getColorValue 0x1b0d660), as Color::toARGB; 9 is
    // whether all of them are ambient. Both are marked dirty.
    float sum[4] = {0, 0, 0, 0};
    float count = 0;
    bool any = false;
    for (const auto &e : s.effects) {
        if (!e.id)
            continue;
        any = true;
        const auto *info = effect_info(e.id);
        if (!e.visible || !info)
            continue;
        for (int j = 0; j <= e.amplifier; ++j) {
            for (int c = 0; c < 4; ++c)
                sum[c] += info->color[c];
            count += 1.0f;
        }
    }
    std::int32_t argb = 0;
    if (any && count != 0.0f) {
        int ch[4];
        for (int c = 0; c < 4; ++c)
            ch[c] = static_cast<int>(255.0f * std::clamp(sum[c] / count, 0.0f, 1.0f));
        argb = static_cast<std::int32_t>((static_cast<std::uint32_t>(ch[3]) << 24) + (static_cast<std::uint32_t>(ch[0]) << 16) +
                                         (static_cast<std::uint32_t>(ch[1]) << 8) + static_cast<std::uint32_t>(ch[2]));
    }
    s.effect_color = argb;
    s.effect_keys = 3; // set, then markDirty(9) and markDirty(8)
    s.effects_dirty = false;
}

// --- Using items -----------------------------------------------------------

std::vector<GamePacket> Server::use_item(std::uint64_t id, Session &s, const Transaction &t) {
    // ItemUseInventoryTransaction::handle, click air -> GameMode::useItem ->
    // Item::use.
    auto &p = world_.players().at(s.key);
    const PlayerData before = p;
    const auto held = p.inventory[p.selected];
    const auto &info = item_info(held.block);
    const bool creative = s.game.player_game_type == 1;
    if (info.kind == ItemKind::Food) {
        // FoodItemComponent::use (0x211fb50): not for invulnerable (creative)
        // players; hungry, or a golden apple of either kind ->
        // Player::startUsingItem (0x22ea990) with the use duration.
        const bool apple = held.block == item_named("golden_apple") || held.block == item_named("appleEnchanted");
        if (!creative && (s.attributes[kHunger] < s.attributes.values[kHunger].max || apple) && s.using_item != held) {
            s.using_item = held;
            s.use_duration = info.food->use_duration;
            s.flags_dirty = true;
        }
    } else if (info.kind == ItemKind::Armor) {
        // ArmorItem::use (0x20b82d0): into the empty armor slot; the stack
        // is cleared unless creative.
        auto &slot = p.armor[static_cast<std::size_t>(info.armor_slot)];
        if (!slot.block) {
            slot = held;
            if (!creative)
                p.inventory[p.selected] = {};
            send_for_entity(id, packet(PacketId::MobArmorEquipment, armor_equipment(id, p)));
        }
    }
    if (!predicted(before, p, t))
        return reject_item_use(s, t);
    return {};
}

std::vector<GamePacket> Server::release_item(std::uint64_t id, Session &s, const Transaction &t) {
    // ItemReleaseInventoryTransaction::handle (0x1ae9170): the selected item
    // must be the transaction's; consume (1) -> Player::completeUsingItem
    // (0x22eacc0), release (0) -> GameMode::releaseUsingItem (bows only).
    auto &p = world_.players().at(s.key);
    if (t.held != p.inventory[p.selected] || t.slot != static_cast<int>(p.selected))
        return inventory_resend(s);
    if (t.action != 1)
        return {};
    const PlayerData before = p;
    const auto item = p.inventory[p.selected];
    const auto &info = item_info(item.block);
    if (info.kind == ItemKind::Food)
        eat(id, s, item);
    // Player::stopUsingItem (0x22eaa30)
    if (s.using_item.block) {
        s.using_item = {};
        s.use_duration = 0;
        s.flags_dirty = true;
    }
    if (!predicted(before, p, t))
        return inventory_resend(s);
    return {};
}

void Server::eat(std::uint64_t id, Session &s, const ItemStack &item) {
    // FoodItemComponent::useTimeDepleted (0x211fc70).
    auto &p = world_.players().at(s.key);
    const auto &food = *item_info(item.block).food;
    // _applyEatEffects (0x211fde0): an effect with chance below 1 needs
    // Level::getRandom().nextFloat() <= chance.
    for (int i = 0; i < food.effect_count; ++i) {
        const auto &e = food.effects[static_cast<std::size_t>(i)];
        if (e.chance != 1.0f && e.chance < level_random_.next_float())
            continue;
        add_effect(id, s, e.effect, e.duration, e.amplifier);
    }
    // Player::eat(int, float) (0x22f6040): food += nutrition; saturation's
    // maximum becomes the food level (setMaxValue clamps and always marks
    // dirty); saturation += nutrition * modifier * 2.
    add_buff(id, s, kHunger, static_cast<float>(food.nutrition), kBuffNone);
    auto &sat = s.attributes.values[kSaturation];
    sat.max = s.attributes[kHunger];
    sat.value = std::clamp(sat.value, sat.min, sat.max);
    s.attributes.mark(kSaturation);
    add_buff(id, s, kSaturation, static_cast<float>(food.nutrition) * food.saturation_modifier * 2.0f, kBuffNone);
    // Player::useItem (0x22eafc0): one item less unless creative.
    auto &slot = p.inventory[p.selected];
    if (s.game.player_game_type != 1 && slot.count) {
        slot.count = static_cast<std::uint8_t>(slot.count - 1);
        if (!slot.count)
            slot = {};
    }
    // Level::broadcastSoundEvent BURP (89) at the eater, extra -1.
    broadcast(packet(PacketId::LevelSoundEvent, level_sound(89, {p.x, p.y - 0.1f, p.z}, -1, 1)));
    // using_converts_to: the bowl of a stew takes the emptied slot.
    if (food.converts_to && !slot.block)
        slot = {item_named(food.converts_to), 1};
}

// --- Combat ----------------------------------------------------------------

void Server::player_move(Session &s, PlayerData &p) {
    // Player::move (0x22eb210) -> Actor::move (0x21a6b50) by the motion, with
    // the player's 0.6 x 1.8 box resting on its feet (pos.y - heightOffset):
    // the same collision code as items (a box a hair inside the ground is
    // pushed out, which clears an upward motion, as traced in the original).
    ItemActor body;
    const float feet = p.y - kHeightOffset;
    body.pos = {p.x, p.y, p.z};
    body.bb = {{p.x - 0.3f, feet, p.z - 0.3f}, {p.x + 0.3f, feet + 1.8f, p.z + 0.3f}};
    body.height_offset = kHeightOffset;
    body.on_ground = p.on_ground;
    body.motion = {s.motion[0], s.motion[1], s.motion[2]};
    move(body, world_, body.motion);
    p.x = body.pos.x;
    p.y = body.pos.y;
    p.z = body.pos.z;
    s.motion[0] = body.motion.x;
    s.motion[1] = body.motion.y;
    s.motion[2] = body.motion.z;
}

std::vector<GamePacket> Server::use_on_actor(std::uint64_t id, Session &s, const Transaction &t) {
    // ItemUseOnActorInventoryTransaction::handle (0x1ae9560).
    auto &p = world_.players().at(s.key);
    if (t.held != p.inventory[p.selected] || t.slot != static_cast<int>(p.selected))
        return inventory_resend(s);
    if (t.action == 0) {
        const float dx = p.x - t.from[0], dy = p.y - t.from[1], dz = p.z - t.from[2];
        if (dx * dx + dy * dy + dz * dz > 36.0f)
            return inventory_resend(s);
        if (!s.sneaking)
            mount(id, t.target);
    }
    const auto distance3 = [](float ax, float ay, float az, float bx, float by, float bz) {
        return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz));
    };
    if (const auto mob = std::find_if(mobs_.begin(), mobs_.end(),
                                      [&](const MobActor &m) { return m.runtime_id == t.target && !m.removed; });
        mob != mobs_.end()) {
        // Level::fetchEntity finds the mob; the same reach checks (error 7)
        // against its position (Actor::getPos: the feet for a mob).
        if (distance3(p.x, p.y, p.z, t.from[0], t.from[1], t.from[2]) > 6.0f ||
            distance3(p.x, p.y, p.z, mob->pos.x, mob->pos.y, mob->pos.z) > 12.0f)
            return inventory_resend(s);
        const PlayerData before = p;
        const float saved[3] = {p.x, p.y, p.z};
        p.x = t.from[0];
        p.y = t.from[1];
        p.z = t.from[2];
        if (t.action == 1)
            attack_mob(id, s, *mob);
        p.x = saved[0];
        p.y = saved[1];
        p.z = saved[2];
        if (!predicted(before, p, t))
            return inventory_resend(s);
        return {};
    }
    const auto target = sessions_.find(t.target);
    if (target == sessions_.end() || !target->second.active || t.target == id)
        return {};
    const auto &tp = world_.players().at(target->second.key);
    const auto distance = [](float ax, float ay, float az, float bx, float by, float bz) {
        return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz));
    };
    // Error 7: the client's position more than 6 from the server's, or the
    // target more than 12 away.
    if (distance(p.x, p.y, p.z, t.from[0], t.from[1], t.from[2]) > 6.0f ||
        distance(p.x, p.y, p.z, tp.x, tp.y, tp.z) > 12.0f)
        return inventory_resend(s);
    const PlayerData before = p;
    // $_1: the player stands at fromPosition for the action.
    const float saved[3] = {p.x, p.y, p.z};
    p.x = t.from[0];
    p.y = t.from[1];
    p.z = t.from[2];
    if (t.action == 1) // GameMode::attack -> Player::attack (no adventure restrictions here)
        attack(id, s, t.target);
    p.x = saved[0];
    p.y = saved[1];
    p.z = saved[2];
    if (!predicted(before, p, t))
        return inventory_resend(s);
    return {};
}

void Server::attack(std::uint64_t id, Session &s, std::uint64_t target) {
    // Player::attack (0x22f1570).
    auto &p = world_.players().at(s.key);
    auto &ts = sessions_.at(target);
    auto &tp = world_.players().at(ts.key);
    const auto held = p.inventory[p.selected];
    // Attack damage attribute plus the item's (WeaponItem); 1 becomes 2.
    int damage = static_cast<int>(s.attributes[kAttackDamage]) + item_info(held.block).attack_damage;
    if (damage == 1)
        damage = 2;
    if (s.effects[kStrength].id)
        for (int i = 0; i <= s.effects[kStrength].amplifier; ++i)
            damage = static_cast<int>(static_cast<float>(damage) * 1.3f + 1.0f);
    if (s.effects[kWeakness].id)
        for (int i = 0; i <= s.effects[kWeakness].amplifier; ++i) {
            damage = static_cast<int>(static_cast<float>(damage) * 0.8f - 0.5f);
            if (damage < 0) {
                damage = 0;
                break;
            }
        }
    // Mob::getMeleeKnockbackBonus (enchantments: none) + 1 while sprinting.
    const int knockback_bonus = s.sprinting ? 1 : 0;
    // Actor::playSynchronizedSound of the target at its body:
    // ATTACK_STRONG (43), or ATTACK_NODAMAGE (42) for no damage.
    const Vec3 body{tp.x, (tp.y - kHeightOffset) + 0.9f, tp.z};
    if (damage <= 0) {
        broadcast(packet(PacketId::LevelSoundEvent, level_sound(42, body, -1, kPlayerType)));
        return;
    }
    broadcast(packet(PacketId::LevelSoundEvent, level_sound(43, body, -1, kPlayerType)));
    // Critical hit: falling, not on the ground, not blind (ladders, water and
    // riding do not occur here); the target is a mob.
    const bool crit = s.fall_distance > 0.0f && !p.on_ground && !s.effects[kBlindness].id;
    if (crit)
        damage = static_cast<int>(static_cast<float>(damage) * 1.5f);
    if (!hurt(target, ts, 2, damage, id, true)) // ActorDamageByActorSource ENTITY_ATTACK
        return;
    cause_food_exhaustion(id, s, 0.3f); // FoodConstants::EXHAUSTION_ATTACK
    if (knockback_bonus > 0) {
        // Push the target (ServerPlayer::push 0x25f88d0: Actor::push, then
        // Player::move, then SetActorMotion); the attacker slows and stops
        // sprinting.
        float dx = tp.x - p.x, dy = 0.1f, dz = tp.z - p.z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz); // Vec3::normalized
        dx /= length;
        dz /= length;
        ts.motion[0] += dx * static_cast<float>(knockback_bonus) * 0.5f;
        ts.motion[1] += 0.1f;
        ts.motion[2] += dz * static_cast<float>(knockback_bonus) * 0.5f;
        player_move(ts, tp);
        send_for_entity(target, packet(PacketId::SetActorMotion,
                                       set_actor_motion(target, {ts.motion[0], ts.motion[1], ts.motion[2]})));
        s.motion[0] *= 0.6f;
        s.motion[2] *= 0.6f;
        set_sprinting(s, false);
    }
    // Player::_crit (0x22f6170): AnimatePacket CRITICAL_HIT through the packet sender.
    if (crit)
        broadcast(packet(PacketId::Animate, animate(4, target)));
    // Item::hurtEnemy unless creative: WeaponItem wears by 1
    // (ItemInstance::hurtAndBreak; broken items disappear).
    auto &slot = p.inventory[p.selected];
    if (slot.block && s.game.player_game_type != 1 && item_info(slot.block).kind == ItemKind::Sword) {
        slot.aux = static_cast<std::int16_t>(slot.aux + 1);
        if (slot.aux > item_info(slot.block).max_damage)
            slot = {};
    }
}

// --- Dropping --------------------------------------------------------------

void Server::drop(std::uint64_t, Session &s, ItemStack stack, bool randomly) {
    // Actor::drop (0x21bfbc0) through Spawner::spawnItem at getAttachPos(4)
    // (0.3 below the eyes, 0.15 ahead, measured under gdb) plus the motion;
    // pickup delay 40, throw time 10. The ItemActor constructor draws from
    // the thread-local Random, the throw from the player's own (+0x2b0).
    const auto &p = world_.players().at(s.key);
    const float degrad = std::numbers::pi_v<float> / 180.0f; // mce::Math::DEGRAD
    const float yaw = p.yaw * degrad;
    const Vec3 at{p.x - mce_sin(yaw) * 0.15f + s.motion[0], p.y - 0.3f + s.motion[1],
                  p.z + mce_cos(yaw) * 0.15f + s.motion[2]};
    auto a = drop_item(at, stack, actor_random_);
    const float pi = std::numbers::pi_v<float>;
    if (randomly) {
        const float speed = s.random.next_float() * 0.5f;
        const float angle = s.random.next_float() * pi * 2.0f;
        a.motion = {-mce_sin(angle) * speed, 0.2f, mce_cos(angle) * speed};
    } else {
        // Thrown forward: 0.3 along the view, then a random 0.02 spread.
        const float pitch = p.pitch * degrad;
        float f = 0.3f;
        a.motion.x = -mce_sin(yaw) * mce_cos(pitch) * f;
        a.motion.z = mce_cos(yaw) * mce_cos(pitch) * f;
        a.motion.y = -mce_sin(pitch) * f;
        f = 0.02f;
        const float angle = s.random.next_float() * pi * 2.0f;
        f = s.random.next_float() * f;
        a.motion.x = mce_cos(angle) * f + a.motion.x;
        a.motion.z = mce_sin(angle) * f + a.motion.z;
    }
    const auto entity = allocate_entity_id();
    a.runtime_id = static_cast<std::uint64_t>(entity);
    a.unique_id = entity;
    add_item(std::move(a));
}
} // namespace bds::game
