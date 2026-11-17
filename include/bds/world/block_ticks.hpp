#pragma once
#include <array>
#include <cstdint>

namespace bds {
// Position generator from LevelChunk::tickBlocks (Linux 1.6.0.15, 0x1de5040).
// This is separate from Level::getRandom: _randValue is a process-global atomic
// int at 0x32d753c, initialized to 42184323 in the original data segment.
// The owning simulation thread supplies the state when replaying a capture.
class BlockTickRandom {
  public:
    explicit BlockTickRandom(std::uint32_t state = 42184323U) : state_(state) {}
    std::uint32_t state() const { return state_; }
    // 0x1de5991..0x1de5a1e: truncate 2.5f * number of subchunks.
    static unsigned sample_count(unsigned subchunks);
    // Weather columns (0x1de566a..0x1de56d9); x/z within the chunk.
    std::array<int, 2> next_column();
    // 0x1de5ac0..0x1de5b2d: x/y/z within the chunk. Y can be negative:
    // both shifts and the remainder in the original are signed operations.
    std::array<int, 3> next_position(int allocated_height);

  private:
    std::int32_t advance();
    std::uint32_t state_;
};
} // namespace bds
