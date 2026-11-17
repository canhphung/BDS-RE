#include "bds/game/loot.hpp"
#include "bds/game/items.hpp"
#include "bds/game/spawner.hpp"
#include <cmath>
#include <optional>

namespace bds::game {
namespace {
struct Range {
    float min, max;
};
struct Entry {
    const char *item; // without the "minecraft:" namespace
    int weight = 1, quality = 0;
    std::optional<Range> count;   // set_count
    const char *smelted = nullptr; // furnace_smelt when on fire
    std::optional<Range> looting;  // looting_enchant
    bool color = false;            // set_data_from_color_index
};
struct Pool {
    Range rolls{1, 1}, bonus_rolls{0, 0};
    std::vector<Entry> entries;
};
using Table = std::vector<Pool>;

const Table &table_for(std::uint32_t type) {
    // loot_tables/entities/{chicken,cow,pig,sheep,horse}.json (donkeys use horse.json).
    static const Table chicken{
        {{1, 1}, {0, 0}, {{"feather", 1, 0, Range{0, 2}, nullptr, Range{0, 1}}}},
        {{1, 1}, {0, 0}, {{"chicken", 1, 0, std::nullopt, "cooked_chicken", Range{0, 1}}}}};
    static const Table cow{
        {{1, 1}, {0, 0}, {{"leather", 1, 0, Range{0, 2}, nullptr, Range{0, 1}}}},
        {{1, 1}, {0, 0}, {{"beef", 1, 0, Range{1, 3}, "cooked_beef", Range{0, 1}}}}};
    static const Table pig{{{1, 1}, {0, 0}, {{"porkchop", 1, 0, Range{1, 3}, "cooked_porkchop", Range{0, 1}}}}};
    static const Table sheep{
        {{1, 1}, {0, 0}, {{"wool", 1, 0, std::nullopt, nullptr, std::nullopt, true}}},
        {{1, 1}, {0, 0}, {{"muttonRaw", 1, 0, Range{1, 2}, "muttonCooked", Range{0, 1}}}}};
    static const Table horse{{{1, 1}, {0, 0}, {{"leather", 1, 0, Range{0, 2}, nullptr, Range{0, 1}}}}};
    static const Table none{};
    switch (type) {
    case actor_type::Chicken: return chicken;
    case actor_type::Cow: return cow;
    case actor_type::Pig: return pig;
    case actor_type::Sheep: return sheep;
    case actor_type::Horse:
    case actor_type::Donkey: return horse;
    default: return none;
    }
}
int floor_int(float v) { return static_cast<int>(std::floor(v)); } // mce::Math::floor
int bounds_int(Range r, Random &random) {
    // RandomValueBounds::getInt: Random::nextIntInclusive(floor(min), floor(max)).
    return random.next_int(floor_int(r.min), floor_int(r.max) + 1);
}
float bounds_float(Range r, Random &random) { return random.next_float(r.min, r.max); } // getFloat
void create_item(const Entry &e, const LootContext &c, Random &random, std::vector<ItemStack> &out) {
    // LootItem::_createItem: an ItemInstance of count 1, then the functions
    // in order (their conditions first), then whole stacks of at most the
    // item's maximum stack size.
    ItemStack item{item_named(e.item), 1, 0};
    int count = 1;
    if (e.count)
        count = bounds_int(*e.count, random); // SetItemCountFunction::apply
    if (e.smelted && c.on_fire)
        item.block = item_named(e.smelted); // SmeltItemFunction::apply
    if (e.looting && c.looting > 0) {
        // LootingEnchantFunction::apply: count + round(level * getFloat).
        const float extra = static_cast<float>(c.looting) * bounds_float(*e.looting, random);
        count += static_cast<int>(std::round(static_cast<double>(extra)));
    }
    if (e.color)
        item.aux = c.color;
    const int max = std::max<int>(1, item_info(item.block).max_stack);
    while (count > 0) {
        const int n = std::min(count, max);
        item.count = static_cast<std::uint8_t>(n);
        out.push_back(item);
        count -= n;
    }
}
} // namespace

std::vector<ItemStack> death_loot(std::uint32_t type, bool baby, const LootContext &c, Random &random) {
    std::vector<ItemStack> out;
    if (baby) // the minecraft:loot component is in the adult groups only
        return out;
    for (const auto &pool : table_for(type)) {
        // LootPool::addRandomItems: rolls, then bonus rolls times luck.
        const int rolls = bounds_int(pool.rolls, random);
        const float bonus = bounds_float(pool.bonus_rolls, random);
        const int n = rolls + floor_int(bonus * c.luck);
        for (int i = 0; i < n; ++i) {
            // LootPool::addRandomItem: weights floor(weight + quality * luck),
            // at least 0, then nextInt(total).
            int total = 0;
            for (const auto &e : pool.entries)
                total += std::max(0, floor_int(static_cast<float>(e.weight) + static_cast<float>(e.quality) * c.luck));
            if (total == 0 || pool.entries.empty())
                continue;
            int pick = random.next_int(total);
            for (const auto &e : pool.entries) {
                pick -= std::max(0, floor_int(static_cast<float>(e.weight) + static_cast<float>(e.quality) * c.luck));
                if (pick < 0) {
                    create_item(e, c, random, out);
                    break;
                }
            }
        }
    }
    return out;
}
} // namespace bds::game
