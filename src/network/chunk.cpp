#include "bds/network/chunk.hpp"
#include "bds/network/level_packets.hpp"
#include <algorithm>
#include <limits>
#include <map>

namespace bds::protocol {
namespace {
constexpr std::size_t max_chunk_bytes = 1024 * 1024;
bool valid_bits(unsigned bits) {
    return bits == 1 || bits == 2 || bits == 3 || bits == 4 || bits == 5 || bits == 6 ||
           bits == 8 || bits == 16;
}
std::size_t word_count(unsigned bits) { return (4096 + 32 / bits - 1) / (32 / bits); }
void require(bool ok) {
    if (!ok)
        throw ProtocolError("invalid protocol-282 chunk");
}
std::uint32_t read_word(ByteReader &in, unsigned count) {
    std::uint32_t value = 0;
    const auto bytes = in.take(count);
    for (unsigned i = 0; i < count; ++i)
        value |= std::uint32_t(bytes[i]) << (8 * i);
    return value;
}
void validate(const ChunkStorage &storage) {
    require(valid_bits(storage.bits));
    require(storage.words.size() == word_count(storage.bits) && !storage.palette.empty() &&
            storage.palette.size() <= std::min(4096U, 1U << storage.bits));
    for (const auto id : storage.palette)
        require(id < vanilla_block_palette().size());
    const auto per_word = 32 / storage.bits, mask = (1U << storage.bits) - 1;
    for (unsigned i = 0; i < 4096; ++i)
        require(((storage.words[i / per_word] >> ((i % per_word) * storage.bits)) & mask) <
                storage.palette.size());
}
} // namespace
std::uint32_t ChunkStorage::block(unsigned x, unsigned y, unsigned z) const {
    require(x < 16 && y < 16 && z < 16 && valid_bits(bits));
    const auto index = (x << 8) | (z << 4) | y, per_word = 32 / bits;
    return palette.at((words.at(index / per_word) >> ((index % per_word) * bits)) &
                      ((1U << bits) - 1));
}
ChunkStorage pack_storage(std::span<const std::uint32_t, 4096> blocks) {
    ChunkStorage out;
    std::map<std::uint32_t, std::uint32_t> indices;
    std::array<std::uint32_t, 4096> values{};
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        require(blocks[i] < vanilla_block_palette().size());
        auto [entry, fresh] =
            indices.try_emplace(blocks[i], static_cast<std::uint32_t>(indices.size()));
        if (fresh)
            out.palette.push_back(blocks[i]);
        values[i] = entry->second;
    }
    while ((1U << out.bits) < out.palette.size())
        ++out.bits;
    if (out.bits == 7)
        out.bits = 8;
    if (out.bits > 8)
        out.bits = 16;
    out.words.resize(word_count(out.bits));
    const auto per_word = 32 / out.bits;
    for (unsigned i = 0; i < 4096; ++i)
        out.words[i / per_word] |= values[i] << ((i % per_word) * out.bits);
    return out;
}
Bytes full_chunk_body(const NetworkChunk &chunk) {
    require(chunk.sections.size() <= 16 && !chunk.suffix.empty() &&
            chunk.suffix.size() <= max_chunk_bytes);
    ByteWriter data;
    data.byte(static_cast<std::uint8_t>(chunk.sections.size()));
    for (const auto &layers : chunk.sections) {
        require(!layers.empty() && layers.size() <= 2);
        data.byte(8);
        data.byte(static_cast<std::uint8_t>(layers.size()));
        for (const auto &storage : layers) {
            validate(storage);
            data.byte(static_cast<std::uint8_t>((storage.bits << 1) | 1));
            for (const auto word : storage.words)
                data.little_endian(word, 4);
            data.signed_varint(static_cast<std::int32_t>(storage.palette.size()));
            for (const auto id : storage.palette)
                data.signed_varint(static_cast<std::int32_t>(id));
        }
    }
    for (const auto height : chunk.heights)
        data.little_endian(height, 2);
    data.raw(chunk.biomes);
    data.raw(chunk.suffix);
    require(data.bytes().size() <= max_chunk_bytes);
    ByteWriter body;
    body.signed_varint(chunk.x);
    body.signed_varint(chunk.z);
    body.varint(static_cast<std::uint32_t>(data.bytes().size()));
    body.raw(data.bytes());
    return body.take();
}
NetworkChunk decode_full_chunk(std::span<const std::uint8_t> body) {
    require(body.size() <= max_chunk_bytes + 15);
    ByteReader envelope(body);
    NetworkChunk chunk;
    chunk.x = envelope.signed_varint();
    chunk.z = envelope.signed_varint();
    ByteReader data(envelope.take(envelope.varint()));
    envelope.finish();
    const auto count = data.byte();
    require(count <= 16);
    for (unsigned i = 0; i < count; ++i) {
        require(data.byte() == 8);
        const auto layers = data.byte();
        require(layers > 0 && layers <= 2);
        std::vector<ChunkStorage> section;
        for (unsigned j = 0; j < layers; ++j) {
            const auto header = data.byte();
            ChunkStorage storage;
            storage.bits = header >> 1;
            require((header & 1) && valid_bits(storage.bits));
            for (std::size_t k = 0; k < word_count(storage.bits); ++k)
                storage.words.push_back(read_word(data, 4));
            const auto size = data.signed_varint();
            require(size > 0 && size <= 4096);
            for (int k = 0; k < size; ++k) {
                const auto id = data.signed_varint();
                require(id >= 0);
                storage.palette.push_back(static_cast<std::uint32_t>(id));
            }
            validate(storage);
            section.push_back(std::move(storage));
        }
        chunk.sections.push_back(std::move(section));
    }
    for (auto &height : chunk.heights)
        height = static_cast<std::uint16_t>(read_word(data, 2));
    const auto biomes = data.take(256);
    std::copy(biomes.begin(), biomes.end(), chunk.biomes.begin());
    require(!data.empty());
    chunk.suffix.assign(data.remaining().begin(), data.remaining().end());
    return chunk;
}
NetworkChunk network_chunk(const World &world, std::int32_t x, std::int32_t z) {
    require(x >= std::numeric_limits<int>::min() / 16 &&
            x <= std::numeric_limits<int>::max() / 16 &&
            z >= std::numeric_limits<int>::min() / 16 && z <= std::numeric_limits<int>::max() / 16);
    const auto &internal = internal_block_palette();
    std::vector<std::uint32_t> ids;
    for (const auto &block : internal)
        ids.push_back(runtime_id(block.name, block.data).value());
    NetworkChunk out;
    out.x = x;
    out.z = z;
    out.biomes.fill(1); // Plains, independent World has no biome simulation.
    const auto *column = world.chunk(x, z);
    if (!column)
        return out;
    unsigned last = 0;
    column->for_each_block([&](unsigned, unsigned y, unsigned, BlockId id) {
        require(id < ids.size());
        last = std::max(last, y / 16 + 1);
    });
    std::vector<std::array<std::uint32_t, 4096>> blocks(last);
    for (auto &section : blocks)
        section.fill(ids[0]);
    column->for_each_block([&](unsigned bx, unsigned y, unsigned bz, BlockId id) {
        blocks[y / 16][(bx << 8) | (bz << 4) | (y % 16)] = ids[id];
        auto &height = out.heights[(bz << 4) | bx];
        height = std::max(height, static_cast<std::uint16_t>(y + 1));
    });
    for (const auto &section : blocks)
        out.sections.push_back({pack_storage(section)});
    return out;
}
} // namespace bds::protocol
