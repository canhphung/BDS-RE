#pragma once
// Entity loot tables translated from BDS 1.6.0.15 (Linux): LootTable::
// getRandomItems 0x1d0faa0, LootPool::addRandomItems 0x1d018d0 / addRandomItem
// 0x1d01570, LootItem::_createItem 0x1d128a0, RandomValueBounds::getInt
// 0x1d01400, SetItemCountFunction 0x1d0adb0, LootingEnchantFunction 0x1d05830,
// SmeltItemFunction 0x1d08f20. The tables are the vanilla behavior pack's
// loot_tables/entities/*.json for the animals BDS-RE spawns.
#include "bds/game/player.hpp"
#include "bds/world/random.hpp"
#include <cstdint>
#include <vector>

namespace bds::game {
struct LootContext {
    bool on_fire = false; // entity_properties {"on_fire": true}
    int looting = 0;      // the killer's best Looting level (no enchantments here)
    float luck = 0.0f;    // Player::getLuck of the killer
    std::uint8_t color = 0; // set_data_from_color_index (sheep wool)
};
// Mob::resolveDeathLoot 0x22b4da0: the table of the mob's minecraft:loot
// component (adults only); `random` is the copy of the actor's Random.
std::vector<ItemStack> death_loot(std::uint32_t actor_type, bool baby, const LootContext &context, Random &random);
} // namespace bds::game
