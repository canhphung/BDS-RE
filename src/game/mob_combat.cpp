// Mob damage, death and loot translated from BDS 1.6.0.15 (Linux).
//
// Player::attack 0x22f1570 against a Mob target, Mob::_hurt 0x22b37f0,
// Mob::actuallyHurt 0x22b44c0, Mob::hurtEffects 0x22b3b60, Mob::knockback
// 0x22b4880, Mob::die 0x22b4a00 -> Mob::resolveDeathLoot 0x22b4da0 ->
// Actor::spawnAtLocation 0x21b8e40, Mob::baseTick 0x22b1770 (hurt time,
// last-hurt-by timers, death) and Mob::tickDeath 0x22b2970. Packet order was
// checked against the original (tools/re/scenarios.py mob_combat capture).
#include "bds/game/effects.hpp"
#include "bds/game/loot.hpp"
#include "bds/game/mce_math.hpp"
#include "bds/game/server.hpp"
#include <cmath>

namespace bds::game {
using namespace protocol;
namespace {
constexpr int kPlayerType = 319; // ActorType Player
constexpr int kEntityAttack = 2; // ActorDamageCause ENTITY_ATTACK

Bytes mob_killed_event(std::int64_t player, std::int64_t killer, std::int64_t victim, int cause) {
    // EventPacket(Player const*, Actor const*, Mob const*, ActorDamageCause)
    // 0x18f7710: the player's unique id, type 4 (MobKilled, written as the
    // raw varint 8 like player_died_event's 12), uses player id 1, then
    // killer, victim and cause (capture: 08 01 <killer> <victim> 04).
    ByteWriter out;
    out.signed_varint64(player);
    out.varint(8);
    out.byte(1);
    out.signed_varint64(killer);
    out.signed_varint64(victim);
    out.signed_varint(cause);
    return out.take();
}
} // namespace

void Server::attack_mob(std::uint64_t id, Session &s, MobActor &mob) {
    // Player::attack (0x22f1570) for a Mob target: the same damage as against
    // a player (attack damage attribute + weapon, 1 becomes 2, strength and
    // weakness), the sound at the target's AABB centre with the player's
    // actor type, critical hits, Actor::hurt with ActorDamageByActorSource
    // ENTITY_ATTACK, food exhaustion, the sprint knockback push and weapon
    // wear.
    if (mob.dead || mob.attributes[kHealth] <= 0.0f)
        return;
    auto &p = world_.players().at(s.key);
    const auto held = p.inventory[p.selected];
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
    const int knockback_bonus = s.sprinting ? 1 : 0;
    const Vec3 body{(mob.bb.min.x + mob.bb.max.x) / 2.0f, (mob.bb.min.y + mob.bb.max.y) / 2.0f,
                    (mob.bb.min.z + mob.bb.max.z) / 2.0f};
    if (damage <= 0) {
        broadcast(packet(PacketId::LevelSoundEvent, level_sound(42, body, -1, kPlayerType))); // ATTACK_NODAMAGE
        return;
    }
    broadcast(packet(PacketId::LevelSoundEvent, level_sound(43, body, -1, kPlayerType))); // ATTACK_STRONG
    const bool crit = s.fall_distance > 0.0f && !p.on_ground && !s.effects[kBlindness].id;
    if (crit)
        damage = static_cast<int>(static_cast<float>(damage) * 1.5f);
    if (!hurt_mob(mob, kEntityAttack, damage, id, true))
        return;
    cause_food_exhaustion(id, s, 0.3f); // FoodConstants::EXHAUSTION_ATTACK
    if (knockback_bonus > 0) {
        // Actor::push on the mob (no immediate SetActorMotion for a mob; the
        // motion goes out with Actor::sendMotionPacketIfNeeded), the attacker
        // slows down and stops sprinting.
        float dx = mob.pos.x - p.x, dy = 0.1f, dz = mob.pos.z - p.z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz); // Vec3::normalized
        dx /= length;
        dz /= length;
        mob.motion.x += dx * static_cast<float>(knockback_bonus) * 0.5f;
        mob.motion.y += 0.1f;
        mob.motion.z += dz * static_cast<float>(knockback_bonus) * 0.5f;
        s.motion[0] *= 0.6f;
        s.motion[2] *= 0.6f;
        set_sprinting(s, false);
    }
    if (crit)
        broadcast(packet(PacketId::Animate, animate(4, mob.runtime_id))); // Player::_crit
    auto &slot = p.inventory[p.selected];
    if (slot.block && s.game.player_game_type != 1 && item_info(slot.block).kind == ItemKind::Sword) {
        slot.aux = static_cast<std::int16_t>(slot.aux + 1);
        if (slot.aux > item_info(slot.block).max_damage)
            slot = {};
    }
}

bool Server::hurt_mob(MobActor &mob, int cause, int damage, std::optional<std::uint64_t> attacker, bool knock) {
    // Mob::_hurt 0x22b37f0 (server side).
    if (mob.dead || mob.attributes[kHealth] <= 0.0f) // Actor::hurt: no damage to a dead mob
        return false;
    if (cause == 13) // OVERRIDE
        mob.last_hurt = 0;
    else if (mob.hurt_time > 5) {
        // (+0x20c == 10 with chained damage effects adds the last hurt; no
        // actor here has getChainedDamageEffects.)
        if (damage <= mob.last_hurt)
            return false;
    } else if (mob.hurt_time > 0)
        return false;
    else
        mob.last_hurt = 0;
    // DamageSensorComponent::recordDamage: the vanilla animals' sensors only
    // react to lightning; an attack by a player deals damage.
    // +0x126c (no-action time) = 0, +0x11fc = 1.5 (walk animation speed).
    if (attacker) {
        // The source entity is a player: Mob::setLastHurtByPlayer (0x22bf2f0)
        // stores it with 400 ticks and calls setLastHurtByMob (0x22bf120),
        // 60 ticks. EnchantUtils::doPostHurtEffects: no enchantments.
        mob.hurt_by_player = *attacker;
        mob.hurt_by_player_time = 400;
        mob.hurt_by_mob = *attacker;
        mob.hurt_by_mob_time = 60;
    }
    actually_hurt_mob(mob, cause, damage - mob.last_hurt);
    return hurt_effects_mob(mob, cause, damage, attacker, knock);
}

void Server::actually_hurt_mob(MobActor &mob, int cause, int damage) {
    // Mob::actuallyHurt 0x22b44c0 with bypassArmor false, as for players
    // (Server::actually_hurt). Animals wear no armour: armour value 0 keeps
    // the full damage and Mob::hurtArmor has nothing to wear.
    mob.absorb_remainder = 0;
    const auto absorb = [&](int d, int factor) { // Mob::_getDamageAfterAbsorb
        const int t = (25 * d + mob.absorb_remainder) * factor / 25;
        mob.absorb_remainder = t % 25;
        return t / 25;
    };
    actor_random_.next_float(0.5f, 1.0f); // EnchantUtils::getDamageReduction (thread-local Random)
    {
        const int t = static_cast<int>(static_cast<float>(25 * damage + mob.absorb_remainder) * 1.0f);
        damage = t / 25;
        mob.absorb_remainder = t % 25;
    }
    if (cause != 0 && cause != 14 && cause != 13 && cause != 12 && cause != 9 && cause != 7 && cause != 5 &&
        cause != 4)
        damage = absorb(damage, 25); // getDamageAfterArmorAbsorb: armour 0
    if (mob.effects[kResistance].id && cause != 0 && cause != 12 && cause != 13) {
        const int amp = mob.effects[kResistance].amplifier;
        damage = amp >= 5 ? 0 : absorb(damage, std::clamp(25 - (amp + 1) * 5, 5, 25));
    }
    if (damage < 1) {
        const int t = 25 * damage + mob.damage_remainder + mob.absorb_remainder;
        damage = t / 25;
        mob.damage_remainder = t % 25;
    } else
        mob.damage_remainder = 0;
    mob.absorb_remainder = 0;
    const int before = damage;
    damage = static_cast<int>(std::max(static_cast<float>(damage) - mob.attributes[kAbsorption], 0.0f));
    add_buff(mob.runtime_id, mob, kAbsorption, static_cast<float>(-(before - damage)), kBuffNone);
    if (damage != 0)
        add_buff(mob.runtime_id, mob, kHealth, static_cast<float>(-damage), kBuffDamage);
}

bool Server::hurt_effects_mob(MobActor &mob, int cause, int damage, std::optional<std::uint64_t> attacker,
                              bool knock) {
    // Mob::hurtEffects 0x22b3b60.
    bool animate = false;
    if (cause == 13 || mob.hurt_time <= 5) {
        animate = true;
        mob.last_hurt = damage;
        mob.hurt_time = 10;
    } else if (mob.hurt_time > 5) {
        if (damage <= mob.last_hurt)
            return false;
        mob.last_hurt = damage;
    }
    if (mob.effects[kInstantHealth].id)
        return false;
    if (animate) {
        // Level::broadcastEntityEvent(HURT, cause) while alive.
        if (mob.attributes[kHealth] > 0.0f)
            send_for_entity(mob.runtime_id, packet(PacketId::ActorEvent, actor_event(mob.runtime_id, 2, cause)));
        const auto by = attacker ? sessions_.find(*attacker) : sessions_.end();
        if (attacker && by != sessions_.end()) {
            const auto &bp = world_.players().at(by->second.key);
            float dx = bp.x - mob.pos.x, dz = bp.z - mob.pos.z;
            if (knock) {
                while (static_cast<double>(dx * dx + dz * dz) < 0.0001) {
                    dx = (actor_random_.next_float() - actor_random_.next_float()) * 0.01f;
                    dz = (actor_random_.next_float() - actor_random_.next_float()) * 0.01f;
                }
                // Mob::knockback 0x22b4880 (power 1, height 0.4, cap 0.4):
                // the mob's own Random against its knockback resistance; a
                // mob sends no SetActorMotion here.
                if (!(mob.attributes[kKnockbackResistance] > mob.random.next_float())) {
                    const float inv = mce_inv_sqrt(dx * dx + dz * dz);
                    const float power = 0.4f;
                    mob.motion.x = 0.5f * mob.motion.x - dx * inv * power;
                    mob.motion.y = 0.4f + 0.5f * mob.motion.y;
                    mob.motion.z = 0.5f * mob.motion.z - dz * inv * power;
                    if (mob.motion.y > 0.4f)
                        mob.motion.y = 0.4f;
                }
            }
        } else
            actor_random_.next_float(); // hurt direction
    }
    if (mob.attributes[kHealth] <= 0.0f) {
        die_mob(mob, cause, attacker);
        return true;
    }
    return damage > 0 && animate;
}

void Server::die_mob(MobActor &mob, int cause, std::optional<std::uint64_t> killer) {
    // Mob::die 0x22b4a00.
    if (mob.dead)
        return;
    mob.dead = true; // +0x133a
    // TransformationComponent: none for these animals. The killer gets
    // awardKillScore (no scoreboard here).
    if (mob.rider) // Actor::removeAllRiders is part of the death of a vehicle
        mob.rider.reset();
    // doMobLoot (GameRuleId 4) and the default gamerules: drop the loot of
    // Mob::resolveDeathLoot with the killer's Looting level (0: no enchantments).
    {
        // resolveDeathLoot: deathLootSeed (+0x1208) is 0, so the table rolls on
        // a copy of the actor's own Random (+0x2b0), which stays unchanged.
        Random random = mob.random;
        LootContext context;
        context.color = mob.color;
        context.on_fire = false; // no fire in BDS-RE yet
        if (killer)
            context.luck = 0.0f; // Player::getLuck: the luck attribute is 0
        for (const auto &stack : death_loot(mob.data->type, mob.baby, context, random)) {
            // Actor::spawnAtLocation(item, -0.05): Spawner::spawnItem at the
            // position 0.05 lower, then ItemActor::setDefaultPickUpDelay.
            auto a = drop_item({mob.pos.x, mob.pos.y - 0.05f, mob.pos.z}, stack, actor_random_);
            const auto entity = allocate_entity_id();
            a.runtime_id = static_cast<std::uint64_t>(entity);
            a.unique_id = entity;
            add_item(std::move(a));
        }
    }
    // Mob::dropContainer / dropBags: no chests or saddle bags here.
    send_for_entity(mob.runtime_id, packet(PacketId::ActorEvent, actor_event(mob.runtime_id, 3, 0))); // DEATH
    if (killer) {
        // EventPacket(player, this, ...) to the killing player (the last
        // hurt-by player, Mob::getLastHurtByPlayer).
        const auto by = sessions_.find(*killer);
        if (by != sessions_.end())
            enqueue(*killer, packet(PacketId::Event, mob_killed_event(by->second.game.unique_id,
                                                                      by->second.game.unique_id, mob.unique_id,
                                                                      cause)));
    }
}

void Server::base_tick_mob(MobActor &mob) {
    // Mob::baseTick 0x22b1770 (the parts that apply to these animals).
    if (mob.hurt_time > 0)
        --mob.hurt_time;
    // The last attacker is forgotten when its timer ran out or it is gone.
    const auto alive = [&](std::uint64_t id) {
        const auto found = sessions_.find(id);
        return found != sessions_.end() && found->second.active && !found->second.dead;
    };
    if (mob.hurt_by_mob && (mob.hurt_by_mob_time <= 0 || !alive(*mob.hurt_by_mob))) {
        mob.hurt_by_mob.reset(); // setLastHurtByMob(nullptr): timer 0
        mob.hurt_by_mob_time = 0;
    }
    if (mob.hurt_by_player && (mob.hurt_by_player_time <= 0 || !alive(*mob.hurt_by_player))) {
        mob.hurt_by_player.reset(); // setLastHurtByPlayer(nullptr)
        mob.hurt_by_player_time = 0;
    }
    if (mob.attributes[kHealth] <= 0.0f) {
        if (!mob.dead)
            die_mob(mob, 0, std::nullopt); // ActorDamageSource(NONE)
        // Mob::tickDeath 0x22b2970.
        ++mob.death_time;
        if (!mob.death_handled && mob.death_time > 20) {
            // Animal/Chicken::getExperienceReward (vtable +0xa10): 1 + nextInt(2)
            // from the level Random when a player hurt it within 400 ticks and
            // it is not a baby, else 0. ExperienceOrb::spawnOrbs at the mob's
            // position with DropType 2 (3 for a player).
            int reward = 0;
            if (mob.hurt_by_player_time > 0 && !mob.baby)
                reward = level_random_.next_int(1, 3);
            spawn_orbs(mob.pos, reward);
            mob.death_handled = true;
            mob.removed = true; // Actor::remove: RemoveActor at the next _updateNearbyEntities
            // Actor::spawnDeathParticles: 20 x 6 nextFloat of the actor's
            // own Random for Level::addParticle (client-side only here).
            for (int i = 0; i < 20 * 6; ++i)
                mob.random.next_float();
        }
    }
    if (mob.hurt_by_mob_time > 0)
        --mob.hurt_by_mob_time;
    if (mob.hurt_by_player_time > 0)
        --mob.hurt_by_player_time;
}

void Server::send_mob_motion(MobActor &mob) {
    // Actor::sendMotionPacketIfNeeded: SetActorMotion when the motion changed
    // by more than 0.2 since the last one, or stopped (as for items).
    const Vec3 dm{mob.motion.x - mob.sent_motion.x, mob.motion.y - mob.sent_motion.y,
                  mob.motion.z - mob.sent_motion.z};
    const float changed2 = dm.x * dm.x + dm.y * dm.y + dm.z * dm.z;
    if (changed2 > 0.04f ||
        (changed2 > 0.0f && mob.motion.x == 0.0f && mob.motion.y == 0.0f && mob.motion.z == 0.0f)) {
        mob.sent_motion = mob.motion;
        send_for_entity(mob.runtime_id, packet(PacketId::SetActorMotion, set_actor_motion(mob.runtime_id, mob.motion)));
    }
}
} // namespace bds::game
