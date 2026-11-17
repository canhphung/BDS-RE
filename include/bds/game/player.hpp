#pragma once
#include <array>
#include <cstdint>
namespace bds {
struct ItemStack {
    std::uint16_t block = 0; // item index (bds::game::item_info); 1-4 are the placeable blocks
    std::uint8_t count = 0;
    std::int16_t aux = 0;    // damage of damageable items (ItemInstance aux value)
    auto operator<=>(const ItemStack &) const = default;
};
struct PlayerData {
    float x = 0.5f, y = 65.62f, z = 0.5f, pitch = 0, yaw = 0, head_yaw = 0;
    bool on_ground = false; // Actor +0x124, from MovePlayer; not persisted
    unsigned selected = 0;
    // Persisted attribute values (minecraft:health, player.hunger, .saturation, .exhaustion).
    float health = 20, hunger = 20, saturation = 20, exhaustion = 0;
    ItemStack cursor;
    std::array<ItemStack, 36> inventory{};
    std::array<ItemStack, 4> armor{}; // armor container (window 120): head, chest, legs, feet
};
struct TerrainConfig {
    unsigned kind = 0; // 0 explicit legacy columns, 1 flat, 2 independent rolling terrain
    std::uint64_t seed = 0;
};
} // namespace bds
