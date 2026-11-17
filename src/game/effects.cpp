#include "bds/game/effects.hpp"
#include <bit>

namespace bds::game {
namespace {
// MobEffect::mMobEffects (0x32f64f0) dumped under gdb: colour (+0x10), harmful
// (+0xc), and the first attribute buff (+0xd8) with its value amplifier
// (+0xb8, MultiplyValueAmplifier scale) and duration amplifier (+0xc8,
// ShiftedDurationAmplifier base). Attribute modifiers (speed, slowness,
// strength, weakness, health boost) and the instantaneous effects are not
// modelled; the original's ShiftedValueAmplifier buffs of instant health and
// harm are left out.
const EffectInfo table[] = {
    {1, {std::bit_cast<float>(0x3ef8f8f9u), std::bit_cast<float>(0x3f2fafb0u), std::bit_cast<float>(0x3f46c6c7u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 1
    {2, {std::bit_cast<float>(0x3eb4b4b5u), std::bit_cast<float>(0x3ed8d8d9u), std::bit_cast<float>(0x3f018182u), std::bit_cast<float>(0x3f800000u)}, true, {}}, // MobEffect 2
    {3, {std::bit_cast<float>(0x3f59d9dau), std::bit_cast<float>(0x3f40c0c1u), std::bit_cast<float>(0x3e868687u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 3
    {4, {std::bit_cast<float>(0x3e949495u), std::bit_cast<float>(0x3e848485u), std::bit_cast<float>(0x3db8b8b9u), std::bit_cast<float>(0x3f800000u)}, true, {}}, // MobEffect 4
    {5, {std::bit_cast<float>(0x3f139394u), std::bit_cast<float>(0x3e109091u), std::bit_cast<float>(0x3e0c8c8du), std::bit_cast<float>(0x3f800000u)}, false, {}}, // AttackDamageMobEffect 5
    {6, {std::bit_cast<float>(0x3f78f8f9u), std::bit_cast<float>(0x3e109091u), std::bit_cast<float>(0x3e0c8c8du), std::bit_cast<float>(0x3f800000u)}, false, {}}, // InstantaneousMobEffect 6
    {7, {std::bit_cast<float>(0x3e868687u), std::bit_cast<float>(0x3d20a0a1u), std::bit_cast<float>(0x3d109091u), std::bit_cast<float>(0x3f800000u)}, true, {}}, // InstantaneousMobEffect 7
    {8, {std::bit_cast<float>(0x3e088889u), std::bit_cast<float>(0x3f800000u), std::bit_cast<float>(0x3e989899u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 8
    {9, {std::bit_cast<float>(0x3eaaaaabu), std::bit_cast<float>(0x3de8e8e9u), std::bit_cast<float>(0x3e949495u), std::bit_cast<float>(0x3f800000u)}, true, {}}, // MobEffect 9
    {10, {std::bit_cast<float>(0x3f4dcdceu), std::bit_cast<float>(0x3eb8b8b9u), std::bit_cast<float>(0x3f2babacu), std::bit_cast<float>(0x3f800000u)}, false, {kHealth, 1.0f, 3, 50, 0.0f, false}}, // MobEffect 10
    {11, {std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e8a8a8bu), std::bit_cast<float>(0x3e68e8e9u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 11
    {12, {std::bit_cast<float>(0x3f64e4e5u), std::bit_cast<float>(0x3f1a9a9bu), std::bit_cast<float>(0x3e68e8e9u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 12
    {13, {std::bit_cast<float>(0x3e38b8b9u), std::bit_cast<float>(0x3ea4a4a5u), std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 13
    {14, {std::bit_cast<float>(0x3efefeffu), std::bit_cast<float>(0x3f038384u), std::bit_cast<float>(0x3f129293u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 14
    {15, {std::bit_cast<float>(0x3df8f8f9u), std::bit_cast<float>(0x3df8f8f9u), std::bit_cast<float>(0x3e0c8c8du), std::bit_cast<float>(0x3f800000u)}, true, {}}, // MobEffect 15
    {16, {std::bit_cast<float>(0x3df8f8f9u), std::bit_cast<float>(0x3df8f8f9u), std::bit_cast<float>(0x3f21a1a2u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 16
    {17, {std::bit_cast<float>(0x3eb0b0b1u), std::bit_cast<float>(0x3eececedu), std::bit_cast<float>(0x3ea6a6a7u), std::bit_cast<float>(0x3f800000u)}, true, {kExhaustion, 15.0f, 0, 1, std::bit_cast<float>(0x3ccccccdu), false}}, // MobEffect 17
    {18, {std::bit_cast<float>(0x3e909091u), std::bit_cast<float>(0x3e9a9a9bu), std::bit_cast<float>(0x3e909091u), std::bit_cast<float>(0x3f800000u)}, true, {}}, // AttackDamageMobEffect 18
    {19, {std::bit_cast<float>(0x3e9c9c9du), std::bit_cast<float>(0x3f139394u), std::bit_cast<float>(0x3e44c4c5u), std::bit_cast<float>(0x3f800000u)}, true, {kHealth, -1.0f, 7, 25, 0.0f, false}}, // MobEffect 19
    {20, {std::bit_cast<float>(0x3e54d4d5u), std::bit_cast<float>(0x3e28a8a9u), std::bit_cast<float>(0x3e1c9c9du), std::bit_cast<float>(0x3f800000u)}, true, {kHealth, -1.0f, 6, 40, 0.0f, false}}, // MobEffect 20
    {21, {std::bit_cast<float>(0x3f78f8f9u), std::bit_cast<float>(0x3efafafbu), std::bit_cast<float>(0x3e0c8c8du), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 21
    {22, {std::bit_cast<float>(0x3e149495u), std::bit_cast<float>(0x3ea4a4a5u), std::bit_cast<float>(0x3f25a5a6u), std::bit_cast<float>(0x3f800000u)}, false, {kAbsorption, 0.0f, 10, 0, std::bit_cast<float>(0x40800000u), true}}, // AbsorptionMobEffect 22
    {23, {std::bit_cast<float>(0x3f78f8f9u), std::bit_cast<float>(0x3e109091u), std::bit_cast<float>(0x3e0c8c8du), std::bit_cast<float>(0x3f800000u)}, false, {kHunger, 1.0f, 1, 0, std::bit_cast<float>(0x3f800000u), false}}, // InstantaneousMobEffect 23
    {24, {std::bit_cast<float>(0x3f4ececfu), std::bit_cast<float>(0x3f800000u), std::bit_cast<float>(0x3f800000u), std::bit_cast<float>(0x3f800000u)}, true, {}}, // MobEffect 24
    {25, {std::bit_cast<float>(0x3e9c9c9du), std::bit_cast<float>(0x3f139394u), std::bit_cast<float>(0x3e44c4c5u), std::bit_cast<float>(0x3f800000u)}, true, {kHealth, -1.0f, 8, 25, 0.0f, false}}, // MobEffect 25
    {26, {std::bit_cast<float>(0x3de8e8e9u), std::bit_cast<float>(0x3f42c2c3u), std::bit_cast<float>(0x3f51d1d2u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 26
    {27, {std::bit_cast<float>(0x3f800000u), std::bit_cast<float>(0x3f6feff0u), std::bit_cast<float>(0x3f51d1d2u), std::bit_cast<float>(0x3f800000u)}, false, {}}, // MobEffect 27
};
} // namespace

const EffectInfo *effect_info(int id) {
    for (const auto &e : table)
        if (e.id == id)
            return &e;
    return nullptr;
}
} // namespace bds::game
