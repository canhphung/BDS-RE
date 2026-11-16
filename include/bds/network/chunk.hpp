#pragma once
#include "bds/network/binary.hpp"
#include "bds/world/world.hpp"
#include <array>
#include <functional>

namespace bds::protocol {
// Protocol 282, subchunk format 8. Block index is (x << 8) | (z << 4) | y.
struct ChunkStorage {
    unsigned bits = 1;
    std::vector<std::uint32_t> words;
    std::vector<std::uint32_t> palette;
    std::uint32_t block(unsigned x, unsigned y, unsigned z) const;
};
struct NetworkChunk {
    std::int32_t x = 0, z = 0;
    std::vector<std::vector<ChunkStorage>> sections;
    std::array<std::uint16_t, 256> heights{}; // index (z << 4) | x
    std::array<std::uint8_t, 256> biomes{};
    // Border blocks and block-entity network NBT are retained verbatim for
    // reference round trips; the internal World currently emits neither.
    Bytes suffix{0};
};
Bytes full_chunk_body(const NetworkChunk &chunk);
NetworkChunk decode_full_chunk(std::span<const std::uint8_t> body);
ChunkStorage pack_storage(std::span<const std::uint32_t, 4096> blocks);
NetworkChunk network_chunk(const World &world, std::int32_t x, std::int32_t z);
} // namespace bds::protocol
