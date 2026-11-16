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
