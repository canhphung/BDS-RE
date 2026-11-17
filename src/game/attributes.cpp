#include "bds/game/attributes.hpp"
#include <limits>

namespace bds::game {
namespace {
constexpr float big = std::numeric_limits<float>::max();
}

Attributes::Attributes()
    : values{{{"minecraft:health", 0, 20, 20, 20},
              {"minecraft:attack_damage", 1, 1, 1, 1},
              {"minecraft:player.experience", 0, 1, 0, 0},
              {"minecraft:absorption", 0, 16, 0, 0},
              {"minecraft:knockback_resistance", 0, 1, 0, 0},
              {"minecraft:luck", -1024, 1024, 0, 0},
              {"minecraft:fall_damage", 0, big, 1, 1},
              {"minecraft:movement", 0, big, 0.1f, 0.1f},
              {"minecraft:underwater_movement", 0, big, 0.02f, 0.02f},
              {"minecraft:player.hunger", 0, 20, 20, 20},
              {"minecraft:follow_range", 0, 2048, 16, 16},
              {"minecraft:player.exhaustion", 0, 4, 0, 0},
              {"minecraft:player.saturation", 0, 20, 20, 20},
              {"minecraft:player.level", 0, 24791, 0, 0}}} {}

float Attributes::apply(const AttributeValue &a, float amount) {
    const float v = a.value + amount;
    return v > a.max ? a.max : v > a.min ? v : a.min; // mce::Math::clamp (0x1838170)
}

void Attributes::set(int id, float value) {
    auto &a = values[static_cast<std::size_t>(id)];
    if (a.value != value) {
        a.value = value;
        mark(id);
    }
}

namespace {
// _calculateValue: [min, max, value] from the defaults; additions, then base
// multipliers (+= base * amount), then total multipliers (*= 1 + amount);
// the value is sanitized (clamped).
void recalculate(AttributeValue &a) {
    float base[3] = {a.default_min, a.default_max, a.fallback};
    for (const auto &m : a.modifiers)
        if (m.operation == 0)
            base[m.operand] = m.amount + base[m.operand];
    float out[3] = {base[0], base[1], base[2]};
    for (const auto &m : a.modifiers)
        if (m.operation == 1)
            out[m.operand] = base[m.operand] * m.amount + out[m.operand];
    for (const auto &m : a.modifiers)
        if (m.operation == 2)
            out[m.operand] = (1.0f + m.amount) * out[m.operand];
    a.min = out[0];
    a.max = out[1];
    a.value = out[2] > a.max ? a.max : out[2] > a.min ? out[2] : a.min;
}
} // namespace

void Attributes::add_modifier(int id, const Modifier &m) {
    auto &a = values[static_cast<std::size_t>(id)];
    if (std::none_of(a.modifiers.begin(), a.modifiers.end(), [&](const Modifier &o) { return o.id == m.id; }))
        a.modifiers.push_back(m);
    recalculate(a);
    mark(id);
}
void Attributes::remove_modifier(int id, int modifier) {
    auto &a = values[static_cast<std::size_t>(id)];
    const auto before = a.modifiers.size();
    std::erase_if(a.modifiers, [&](const Modifier &o) { return o.id == modifier; });
    if (a.modifiers.size() == before)
        return;
    recalculate(a);
    mark(id);
}
void Attributes::set_max(int id, float max) {
    auto &a = values[static_cast<std::size_t>(id)];
    a.max = a.default_max = max;
    a.value = a.value > a.max ? a.max : a.value > a.min ? a.value : a.min;
    mark(id);
}

const std::vector<int> &spawn_attribute_order() {
    static const std::vector<int> order{
        kHealth,     kAbsorption, kMovement,   kUnderwaterMovement, kLuck,       kFallDamage,
        kUnderwaterMovement, kMovement, kFallDamage, kLuck,       kAbsorption, kHealth,
        kHealth,     kSaturation, kLevel,      kExperience,         kHunger,     kExhaustion,
        kMovement,   kMovement,   kExhaustion, kSaturation,         kHealth,     kExperience,
        kAbsorption, kLuck,       kFallDamage, kMovement,           kUnderwaterMovement, kHunger,
        kExhaustion, kSaturation, kLevel,      kHealth,             kExperience, kAbsorption,
        kLuck,       kFallDamage, kMovement,   kUnderwaterMovement, kHunger,     kExhaustion,
        kSaturation, kLevel,      kLevel,      kExperience};
    return order;
}
} // namespace bds::game
