#include "bds/game/items.hpp"
#include <cstring>

namespace bds::game {
namespace {
// FoodItemComponent of every food item, dumped from the original's item
// registry under gdb (Item +0x108; use duration Item +0xe0; nutrition +0x8,
// saturation modifier +0xc, using_converts_to +0x38, effects +0x58 with
// duration already in ticks).
const FoodData food_apple{32, 4, 0.300000012f, nullptr, {}, 0};
const FoodData food_golden_apple{32, 4, 1.20000005f, nullptr, {{{10, 100, 1, 1.0f}, {22, 2400, 0, 1.0f}}}, 2};
const FoodData food_appleEnchanted{32, 4, 1.20000005f, nullptr, {{{10, 600, 4, 1.0f}, {22, 2400, 3, 1.0f}, {11, 6000, 0, 1.0f}, {12, 6000, 0, 1.0f}}}, 4};
const FoodData food_mushroom_stew{32, 6, 0.600000024f, "bowl", {{}}, 0};
const FoodData food_bread{32, 5, 0.600000024f, nullptr, {}, 0};
const FoodData food_porkchop{32, 3, 0.300000012f, nullptr, {}, 0};
const FoodData food_cooked_porkchop{32, 8, 0.800000012f, nullptr, {}, 0};
const FoodData food_fish{32, 2, 0.100000001f, nullptr, {}, 0};
const FoodData food_salmon{32, 2, 0.100000001f, nullptr, {}, 0};
const FoodData food_clownfish{32, 1, 0.100000001f, nullptr, {}, 0};
const FoodData food_pufferfish{32, 1, 0.100000001f, nullptr, {{{19, 1200, 3, 1.0f}, {9, 300, 1, 1.0f}, {17, 300, 2, 1.0f}}}, 3};
const FoodData food_cooked_fish{32, 5, 0.600000024f, nullptr, {}, 0};
const FoodData food_cooked_salmon{32, 6, 0.800000012f, nullptr, {}, 0};
const FoodData food_dried_kelp{16, 1, 0.100000001f, nullptr, {}, 0};
const FoodData food_cookie{32, 2, 0.100000001f, nullptr, {}, 0};
const FoodData food_melon{32, 2, 0.300000012f, nullptr, {}, 0};
const FoodData food_beef{32, 3, 0.300000012f, nullptr, {}, 0};
const FoodData food_cooked_beef{32, 8, 0.800000012f, nullptr, {}, 0};
const FoodData food_chicken{32, 2, 0.300000012f, nullptr, {{{17, 600, 0, 0.300000012f}}}, 1};
const FoodData food_cooked_chicken{32, 6, 0.600000024f, nullptr, {}, 0};
const FoodData food_rotten_flesh{32, 4, 0.100000001f, nullptr, {{{17, 600, 0, 0.800000012f}}}, 1};
const FoodData food_spider_eye{32, 2, 0.800000012f, nullptr, {{{19, 100, 0, 1.0f}}}, 1};
const FoodData food_carrot{32, 3, 0.600000024f, nullptr, {}, 0};
const FoodData food_potato{32, 1, 0.300000012f, nullptr, {}, 0};
const FoodData food_baked_potato{32, 5, 0.600000024f, nullptr, {}, 0};
const FoodData food_poisonous_potato{32, 2, 0.300000012f, nullptr, {{{19, 100, 0, 0.600000024f}}}, 1};
const FoodData food_golden_carrot{32, 6, 1.20000005f, nullptr, {}, 0};
const FoodData food_pumpkin_pie{32, 8, 0.300000012f, nullptr, {}, 0};
const FoodData food_beetroot{32, 1, 0.600000024f, nullptr, {}, 0};
const FoodData food_beetroot_soup{32, 6, 0.600000024f, "bowl", {{}}, 0};
const FoodData food_rabbit{32, 3, 0.300000012f, nullptr, {}, 0};
const FoodData food_cooked_rabbit{32, 5, 0.600000024f, nullptr, {}, 0};
const FoodData food_rabbit_stew{32, 10, 0.600000024f, "bowl", {{}}, 0};
const FoodData food_muttonRaw{32, 2, 0.300000012f, nullptr, {}, 0};
const FoodData food_muttonCooked{32, 6, 0.800000012f, nullptr, {}, 0};

// Index 0 is air and 1-4 are the block items of the internal palette; the
// rest were dumped from ItemRegistry::mItemRegistry (id Item+0x70, name
// +0x98, max stack +0x8, max damage +0xd8; WeaponItem damage +0x120 =
// 4 + tier bonus; ArmorItem slot +0x120 and defense +0x124).
const ItemInfo table[] = {
    {0, "air", 0, 0, ItemKind::Air, 0, 0, 0, nullptr},
    {1, "stone", 64, 0, ItemKind::Block, 0, 0, 0, nullptr},
    {2, "grass", 64, 0, ItemKind::Block, 0, 0, 0, nullptr},
    {3, "dirt", 64, 0, ItemKind::Block, 0, 0, 0, nullptr},
    {7, "bedrock", 64, 0, ItemKind::Block, 0, 0, 0, nullptr},
    {268, "wooden_sword", 1, 59, ItemKind::Sword, 4, 0, 0, nullptr},
    {272, "stone_sword", 1, 131, ItemKind::Sword, 5, 0, 0, nullptr},
    {267, "iron_sword", 1, 250, ItemKind::Sword, 6, 0, 0, nullptr},
    {283, "golden_sword", 1, 32, ItemKind::Sword, 4, 0, 0, nullptr},
    {276, "diamond_sword", 1, 1561, ItemKind::Sword, 7, 0, 0, nullptr},
    {298, "leather_helmet", 1, 55, ItemKind::Armor, 0, 0, 1, nullptr},
    {299, "leather_chestplate", 1, 80, ItemKind::Armor, 0, 1, 3, nullptr},
    {300, "leather_leggings", 1, 75, ItemKind::Armor, 0, 2, 2, nullptr},
    {301, "leather_boots", 1, 65, ItemKind::Armor, 0, 3, 1, nullptr},
    {302, "chainmail_helmet", 1, 165, ItemKind::Armor, 0, 0, 2, nullptr},
    {303, "chainmail_chestplate", 1, 240, ItemKind::Armor, 0, 1, 5, nullptr},
    {304, "chainmail_leggings", 1, 225, ItemKind::Armor, 0, 2, 4, nullptr},
    {305, "chainmail_boots", 1, 195, ItemKind::Armor, 0, 3, 1, nullptr},
    {306, "iron_helmet", 1, 165, ItemKind::Armor, 0, 0, 2, nullptr},
    {307, "iron_chestplate", 1, 240, ItemKind::Armor, 0, 1, 6, nullptr},
    {308, "iron_leggings", 1, 225, ItemKind::Armor, 0, 2, 5, nullptr},
    {309, "iron_boots", 1, 195, ItemKind::Armor, 0, 3, 2, nullptr},
    {314, "golden_helmet", 1, 77, ItemKind::Armor, 0, 0, 2, nullptr},
    {315, "golden_chestplate", 1, 112, ItemKind::Armor, 0, 1, 5, nullptr},
    {316, "golden_leggings", 1, 105, ItemKind::Armor, 0, 2, 3, nullptr},
    {317, "golden_boots", 1, 91, ItemKind::Armor, 0, 3, 1, nullptr},
    {310, "diamond_helmet", 1, 363, ItemKind::Armor, 0, 0, 3, nullptr},
    {311, "diamond_chestplate", 1, 528, ItemKind::Armor, 0, 1, 8, nullptr},
    {312, "diamond_leggings", 1, 495, ItemKind::Armor, 0, 2, 6, nullptr},
    {313, "diamond_boots", 1, 429, ItemKind::Armor, 0, 3, 3, nullptr},
    {469, "turtle_helmet", 1, 275, ItemKind::Armor, 0, 0, 2, nullptr},
    {281, "bowl", 64, 0, ItemKind::Plain, 0, 0, 0, nullptr},
    // Animal loot (loot_tables/entities): legacy ids of the original item
    // registry. Wool is a block item (id 35, aux = colour) but has no block
    // in BDS-RE's palette, so it is carried as a plain item.
    {288, "feather", 64, 0, ItemKind::Plain, 0, 0, 0, nullptr},
    {334, "leather", 64, 0, ItemKind::Plain, 0, 0, 0, nullptr},
    {35, "wool", 64, 0, ItemKind::Plain, 0, 0, 0, nullptr},
    {260, "apple", 64, 0, ItemKind::Food, 0, 0, 0, &food_apple},
    {322, "golden_apple", 64, 0, ItemKind::Food, 0, 0, 0, &food_golden_apple},
    {466, "appleEnchanted", 64, 0, ItemKind::Food, 0, 0, 0, &food_appleEnchanted},
    {282, "mushroom_stew", 1, 0, ItemKind::Food, 0, 0, 0, &food_mushroom_stew},
    {297, "bread", 64, 0, ItemKind::Food, 0, 0, 0, &food_bread},
    {319, "porkchop", 64, 0, ItemKind::Food, 0, 0, 0, &food_porkchop},
    {320, "cooked_porkchop", 64, 0, ItemKind::Food, 0, 0, 0, &food_cooked_porkchop},
    {349, "fish", 64, 0, ItemKind::Food, 0, 0, 0, &food_fish},
    {460, "salmon", 64, 0, ItemKind::Food, 0, 0, 0, &food_salmon},
    {461, "clownfish", 64, 0, ItemKind::Food, 0, 0, 0, &food_clownfish},
    {462, "pufferfish", 64, 0, ItemKind::Food, 0, 0, 0, &food_pufferfish},
    {350, "cooked_fish", 64, 0, ItemKind::Food, 0, 0, 0, &food_cooked_fish},
    {463, "cooked_salmon", 64, 0, ItemKind::Food, 0, 0, 0, &food_cooked_salmon},
    {464, "dried_kelp", 64, 0, ItemKind::Food, 0, 0, 0, &food_dried_kelp},
    {357, "cookie", 64, 0, ItemKind::Food, 0, 0, 0, &food_cookie},
    {360, "melon", 64, 0, ItemKind::Food, 0, 0, 0, &food_melon},
    {363, "beef", 64, 0, ItemKind::Food, 0, 0, 0, &food_beef},
    {364, "cooked_beef", 64, 0, ItemKind::Food, 0, 0, 0, &food_cooked_beef},
    {365, "chicken", 64, 0, ItemKind::Food, 0, 0, 0, &food_chicken},
    {366, "cooked_chicken", 64, 0, ItemKind::Food, 0, 0, 0, &food_cooked_chicken},
    {367, "rotten_flesh", 64, 0, ItemKind::Food, 0, 0, 0, &food_rotten_flesh},
    {375, "spider_eye", 64, 0, ItemKind::Food, 0, 0, 0, &food_spider_eye},
    {391, "carrot", 64, 0, ItemKind::Food, 0, 0, 0, &food_carrot},
    {392, "potato", 64, 0, ItemKind::Food, 0, 0, 0, &food_potato},
    {393, "baked_potato", 64, 0, ItemKind::Food, 0, 0, 0, &food_baked_potato},
    {394, "poisonous_potato", 64, 0, ItemKind::Food, 0, 0, 0, &food_poisonous_potato},
    {396, "golden_carrot", 64, 0, ItemKind::Food, 0, 0, 0, &food_golden_carrot},
    {400, "pumpkin_pie", 64, 0, ItemKind::Food, 0, 0, 0, &food_pumpkin_pie},
    {457, "beetroot", 64, 0, ItemKind::Food, 0, 0, 0, &food_beetroot},
    {459, "beetroot_soup", 1, 0, ItemKind::Food, 0, 0, 0, &food_beetroot_soup},
    {411, "rabbit", 64, 0, ItemKind::Food, 0, 0, 0, &food_rabbit},
    {412, "cooked_rabbit", 64, 0, ItemKind::Food, 0, 0, 0, &food_cooked_rabbit},
    {413, "rabbit_stew", 1, 0, ItemKind::Food, 0, 0, 0, &food_rabbit_stew},
    {423, "muttonRaw", 64, 0, ItemKind::Food, 0, 0, 0, &food_muttonRaw},
    {424, "muttonCooked", 64, 0, ItemKind::Food, 0, 0, 0, &food_muttonCooked},
};
} // namespace

std::uint16_t item_count() { return static_cast<std::uint16_t>(std::size(table)); }
const ItemInfo &item_info(std::uint16_t index) { return table[index < std::size(table) ? index : 0]; }
std::uint16_t item_from_legacy(int id) {
    for (std::uint16_t i = 1; i < std::size(table); ++i)
        if (table[i].legacy == id)
            return i;
    return 0;
}
std::uint16_t item_named(const char *name) {
    for (std::uint16_t i = 1; i < std::size(table); ++i)
        if (std::strcmp(table[i].name, name) == 0)
            return i;
    return 0;
}
} // namespace bds::game
