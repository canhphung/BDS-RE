#pragma once
// Player attributes translated from BDS 1.6.0.15 (Linux): AttributeInstance
// (instantaneous buffs, _calculateValue 0x24a73a0, _sanitizeValue 0x24a72b0,
// addBuff 0x24a7760, tick 0x24a7950) and the BaseAttributeMap dirty list
// (onAttributeModified 0x24a47d0, one entry per modification, duplicates kept).
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

struct Attributes {
    Attributes();
    std::array<AttributeValue, kAttributeCount> values;
    std::vector<int> dirty;
    // HungerAttributeDelegate (+0x30 heal timer, +0x34 tick counter, +0x38 last food)
    int heal_timer = 0, hunger_ticks = 0;
    float last_food = 20.0f;
    // HealthAttributeDelegate (+0x30 tick counter)
    int health_ticks = 0;

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
