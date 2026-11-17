#pragma once
// Player attributes translated from BDS 1.6.0.15 (Linux): AttributeInstance
// (instantaneous buffs, _calculateValue 0x24a73a0, _sanitizeValue 0x24a72b0,
// addBuff 0x24a7760, tick 0x24a7950) and the BaseAttributeMap dirty list
// (onAttributeModified 0x24a47d0, one entry per modification, duplicates kept).
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace bds::game {
// Instances in the iteration order of the original's attribute map
// (getSyncableAttributes in the join capture); Mob::tickAttributes visits
// them in this order.
enum AttributeId : int {
    kHealth,
    kAttackDamage,
    kExperience,
    kAbsorption,
    kKnockbackResistance,
    kLuck,
    kFallDamage,
    kMovement,
    kUnderwaterMovement,
    kHunger,
    kFollowRange,
    kExhaustion,
    kSaturation,
    kLevel,
    kAttributeCount
};

// AttributeBuffType values used by the translated code.
enum BuffType : int { kBuffStarve = 0, kBuffHeal = 3, kBuffNone = 9, kBuffDamage = 10 };

struct AttributeValue {
    const char *name;
    float min, max, value, fallback; // fallback: the default value sent with it
};

// TemporalAttributeBuff created by MobEffect::_createTemporalBuff (0x1b0ff70).
struct TemporalBuff {
    int effect = 0;          // owning effect (MobEffect::removeEffects removes by buff id)
    float amount = 0;        // AttributeBuff +0x8 (= base when a duration amplifier is set)
    int type = 0;            // AttributeBuffType
    int duration = 0;        // +0x54
    int life = -1;           // +0x58 life timer
    int amplification = 0;   // +0x44
    int interval = 0;        // ShiftedDurationAmplifier base (0: none)
    float multiply = 0;      // MultiplyValueAmplifier scale (0: none)
    // AttributeBuff::getAmount (0x24aaea0)
    float value() const { return multiply != 0.0f ? multiply * static_cast<float>(amplification + 1) : amount; }
    // TemporalAttributeBuff::isComplete / shouldBuff (0x24ab440, 0x24ab380)
    bool complete() const { return life >= duration; }
    bool should_buff() const {
        if (complete())
            return false;
        if (!interval)
            return true;
        const int every = std::max(1, interval >> amplification); // ShiftedDurationAmplifier::getTickInterval
        return (duration - life) % every == 0;
    }
};

struct Attributes {
    Attributes();
    std::array<AttributeValue, kAttributeCount> values;
    std::vector<int> dirty;
    // HungerAttributeDelegate (+0x30 heal timer, +0x34 tick counter, +0x38 last food)
    int heal_timer = 0, hunger_ticks = 0;
    float last_food = 20.0f;
    // HealthAttributeDelegate (+0x30 tick counter)
    int health_ticks = 0;
    std::array<std::vector<TemporalBuff>, kAttributeCount> temporal; // AttributeInstance +0x30

    float operator[](int id) const { return values[static_cast<std::size_t>(id)].value; }
    // _calculateValue + _sanitizeValue: value += amount, then mce::Math::clamp
    // to [min, max] (no maximum-capping modifiers exist here).
    static float apply(const AttributeValue &a, float amount);
    void set(int id, float value); // direct value change (modifiers/resets): marks dirty if changed
    void mark(int id) { dirty.push_back(id); }
};

// The order of the first dirty UpdateAttributes list the original sends to a
// joining player (join capture packet 26): 46 entries with duplicates.
const std::vector<int> &spawn_attribute_order();
} // namespace bds::game
