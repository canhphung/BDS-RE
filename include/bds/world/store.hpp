#pragma once

#include "bds/world/world.hpp"
#include <filesystem>
#include <span>
#include <vector>

namespace bds {
// Independent snapshot format. Not a Bedrock/LevelDB world reader.
class WorldStore {
public:
    struct Limits {
        std::size_t max_blocks = 1'000'000;
        std::size_t max_sections = 4096;
    };
    static std::vector<std::uint8_t> encode(const World& world);
    static World decode(std::span<const std::uint8_t> bytes, Limits limits);
    static World load(const std::filesystem::path& path);
    // Writes a sibling staging file, then replaces destination atomically.
    // Requires a single writer; no power-loss durability guarantee.
    static void save(const World& world, const std::filesystem::path& path);
};
}
