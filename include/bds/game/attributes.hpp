#pragma once
// Player attributes translated from BDS 1.6.0.15 (Linux): AttributeInstance
// (instantaneous buffs, _calculateValue 0x24a73a0, _sanitizeValue 0x24a72b0,
// addBuff 0x24a7760, tick 0x24a7950) and the BaseAttributeMap dirty list
// (onAttributeModified 0x24a47d0, one entry per modification, duplicates kept).
#include <algorithm>
#include <cmath>
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

// AttributeModifier (+0x8 amount, +0xc operation, +0x10 operand).
struct Modifier {
    int id;        // identity (mce::UUID in the original): kSprintModifier or an effect id
    float amount;
    int operation; // 0 addition, 1 multiply base, 2 multiply total
    int operand;   // 0 min, 1 max, 2 current value
};
constexpr int kSprintModifier = 1000; // Mob::SPRINT_SPEED_BOOST

struct AttributeValue {
    AttributeValue(const char *n, float lo, float hi, float v, float d)
        : name(n), min(lo), max(hi), value(v), fallback(d), default_min(lo), default_max(hi) {}
    const char *name;
    float min, max, value, fallback; // fallback: the default value sent with it
    // Default minimum and maximum (+0x70, +0x74); _calculateValue starts from these.
    float default_min, default_max;
    std::vector<Modifier> modifiers;
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
    int shifted = 0;         // ShiftedValueAmplifier base (0: none)
    float shifted_scale = 0;
    // AttributeBuff::getAmount (0x24aaea0); ShiftedValueAmplifier::getAmount
    // (0x24a3c80): floor(1.0 * (base << min(amp, 32)) + 0.5) * scale.
    float value() const {
        if (shifted) {
            const auto shifted_value = static_cast<std::int64_t>(shifted) << std::min(amplification, 32);
            return static_cast<float>(std::floor(1.0f * static_cast<float>(shifted_value) + 0.5f)) * shifted_scale;
        }
        return multiply != 0.0f ? multiply * static_cast<float>(amplification + 1) : amount;
    }
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
    // AttributeInstance::addModifier / removeModifier (0x24a65d0, 0x24a6a00):
    // recompute from the defaults (_calculateValue 0x24a6690) and mark dirty.
    void add_modifier(int id, const Modifier &m);
    void remove_modifier(int id, int modifier);
    // AttributeInstance::setMaxValue (0x24a7c50): maximum and default maximum,
    // value clamped, always dirty.
    void set_max(int id, float max);
};

// The order of the first dirty UpdateAttributes list the original sends to a
// joining player (join capture packet 26): 46 entries with duplicates.
const std::vector<int> &spawn_attribute_order();
} // namespace bds::game
