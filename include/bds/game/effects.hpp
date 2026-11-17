#pragma once
// Mob effects of BDS 1.6.0.15 (MobEffect::initEffects 0x1b0e010).
#include "bds/game/attributes.hpp"

namespace bds::game {
struct EffectBuff {
    int attribute = -1;      // AttributeInstance receiving the buff; -1: none
    float base = 0;          // AttributeBuff base amount
    int type = 0;            // AttributeBuffType
    int interval = 0;        // ShiftedDurationAmplifier base: applied every max(1, interval >> amp) ticks; 0: none
    float multiply = 0;      // MultiplyValueAmplifier: amount = multiply * (amp + 1); 0: none
    bool instant = false;    // InstantaneousAttributeBuff (absorption)
    int shifted = 0;         // ShiftedValueAmplifier base (instant health/harm); 0: none
    float shifted_scale = 0; // ShiftedValueAmplifier scale
};
struct EffectInfo {
    int id;
    float color[4]; // r, g, b, a
    bool harmful;
    EffectBuff buff;
};
const EffectInfo *effect_info(int id);

enum EffectId : int {
    kRegeneration = 10,
    kResistance = 11,
    kBlindness = 15,
    kHungerEffect = 17,
    kPoison = 19,
    kAbsorptionEffect = 22,
    kStrength = 5,
    kWeakness = 18,
    kInstantHealth = 6,
};
} // namespace bds::game
