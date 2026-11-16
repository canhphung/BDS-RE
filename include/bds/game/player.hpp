#pragma once
#include <array>
#include <cstdint>
namespace bds {
struct ItemStack {
    std::uint16_t block = 0;
    std::uint8_t count = 0;
    auto operator<=>(const ItemStack &) const = default;
};
struct PlayerData {
    float x = 0.5f, y = 65.62f, z = 0.5f, pitch = 0, yaw = 0, head_yaw = 0;
    unsigned selected = 0;
    ItemStack cursor;
    std::array<ItemStack, 36> inventory{};
};
struct TerrainConfig {
    unsigned kind = 0; // 0 explicit legacy columns, 1 flat, 2 independent rolling terrain
    std::uint64_t seed = 0;
};
} // namespace bds
