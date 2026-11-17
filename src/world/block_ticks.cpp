#include "bds/world/block_ticks.hpp"
#include <bit>
#include <stdexcept>

namespace bds {
unsigned BlockTickRandom::sample_count(unsigned subchunks) {
    if (subchunks > 16)
        throw std::out_of_range("subchunk count outside original height limit");
    return static_cast<unsigned>(2.5f * static_cast<float>(subchunks)); // float at 0x282863c
}
std::int32_t BlockTickRandom::advance() {
    state_ = state_ * 3U + 0x3c6ef35fU; // wrap the original 32-bit imul/add
    return std::bit_cast<std::int32_t>(state_) >> 2;
}
std::array<int, 2> BlockTickRandom::next_column() {
    const auto value = advance();
    return {value & 15, (value >> 8) & 15};
}
std::array<int, 3> BlockTickRandom::next_position(int allocated_height) {
    // getAllocatedHeight (0x1c637f0) is subchunk count * 16. Zero-height
    // chunks have zero samples, so no division is executed in the original.
    if (allocated_height <= 0 || allocated_height > 256 || allocated_height % 16)
        throw std::out_of_range("invalid allocated chunk height");
    const auto value = advance();
    return {value & 15, (value >> 16) % allocated_height, (value >> 8) & 15};
}
} // namespace bds
