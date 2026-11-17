#pragma once
// Item definitions from the original 1.6.0.15 item registry (VanillaItems,
// ArmorItem, WeaponItem, FoodItemComponent), see src/game/items.cpp.
// ItemStack::block is an index into this table; 1-4 are the placeable blocks.
#include <array>
#include <cstdint>

namespace bds::game {
enum class ItemKind : std::uint8_t { Air, Block, Plain, Food, Sword, Armor };

struct FoodEffect {
    int effect, duration, amplifier; // MobEffectInstance(id, duration ticks, amplifier)
    float chance;
};
struct FoodData {
    int use_duration, nutrition;
    float saturation_modifier;
    const char *converts_to; // using_converts_to (bowl) or nullptr
    std::array<FoodEffect, 4> effects;
    int effect_count;
};
struct ItemInfo {
    std::int16_t legacy;
    const char *name;
    std::uint8_t max_stack;
    std::int16_t max_damage; // durability; 0: not damageable
    ItemKind kind;
    int attack_damage;       // WeaponItem::getAttackDamage
    int armor_slot, defense; // ArmorItem slot (0 head .. 3 feet), getArmorValue
    const FoodData *food;
};

std::uint16_t item_count();
const ItemInfo &item_info(std::uint16_t index);
std::uint16_t item_from_legacy(int id); // 0 when unknown
std::uint16_t item_named(const char *name);
} // namespace bds::game
