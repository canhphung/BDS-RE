#include "bds/world/store.hpp"
#include "bds/core/atomic_file.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>

namespace bds {
namespace {
constexpr std::array<std::uint8_t, 8> magic{'B','D','S','R','E','W','\r','\n'};
constexpr std::size_t header_size = 28, record_size = 12, checksum_size = 4;
constexpr std::size_t max_file_size = header_size + 1'000'000 * record_size + checksum_size;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void append(std::vector<std::uint8_t>& out, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8*i)));
}
std::uint64_t read(std::span<const std::uint8_t> bytes, std::size_t offset, unsigned width) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= static_cast<std::uint64_t>(bytes[offset+i]) << (8*i);
    return value;
}
std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t value = 0xffffffffU;
    for (auto byte : bytes) {
        value ^= byte;
        for (unsigned i = 0; i < 8; ++i)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320U : 0U);
    }
    return value ^ 0xffffffffU;
}
;
}

std::vector<std::uint8_t> WorldStore::encode(const World& world) {
    require(world.stored_blocks() <= Limits{}.max_blocks, "world exceeds snapshot block limit");
    require(world.allocated_sections() <= Limits{}.max_sections, "world exceeds snapshot section limit");
    std::vector<std::uint8_t> bytes(magic.begin(), magic.end());
    bytes.reserve(header_size + record_size * world.stored_blocks() + checksum_size);
    append(bytes, 1, 4);
    append(bytes, world.age(), 8);
    append(bytes, world.stored_blocks(), 8);
    world.for_each_block([&](World::Position position, BlockId block) {
        const auto [x,y,z] = position;
        append(bytes, static_cast<std::uint32_t>(x), 4);
        append(bytes, static_cast<unsigned>(y), 2);
        append(bytes, static_cast<std::uint32_t>(z), 4);
        append(bytes, block, 2);
    });
    append(bytes, crc32(bytes), 4);
    return bytes;
}

World WorldStore::decode(std::span<const std::uint8_t> bytes, Limits limits) {
    require(bytes.size() >= header_size + checksum_size, "truncated world snapshot");
    require(std::equal(magic.begin(), magic.end(), bytes.begin()), "invalid world magic");
    require(read(bytes, 8, 4) == 1, "unsupported world snapshot version");
    const auto count = read(bytes, 20, 8);
    require(count <= limits.max_blocks && count <= 1'000'000, "world snapshot block limit exceeded");
    require(bytes.size() == header_size + record_size * count + checksum_size, "world snapshot size mismatch");
    require(crc32(bytes.first(bytes.size()-4)) == read(bytes, bytes.size()-4, 4), "world snapshot checksum mismatch");
    World world;
    world.age_ = read(bytes, 12, 8);
    std::size_t sections = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = header_size + record_size * i;
        const auto x = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(bytes,offset,4)));
        const auto y = static_cast<int>(read(bytes,offset+4,2));
        const auto z = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(bytes,offset+6,4)));
        const auto block = static_cast<BlockId>(read(bytes,offset+10,2));
        require(y < static_cast<int>(Chunk::height) && block != 0, "invalid world block record");
        const World::Position position{x,y,z};
        require(world.block(position) == 0, "duplicate world block record");
        // Count only the touched column's sections, avoiding quadratic scans.
        const auto key = std::pair{chunk_coordinate(x), chunk_coordinate(z)};
        auto column = world.columns_.find(key);
        const auto before = column == world.columns_.end() ? 0 : column->second.allocated_sections();
        world.set_block(position, block);
        sections += world.columns_.at(key).allocated_sections() - before;
        require(sections <= limits.max_sections, "world snapshot section limit exceeded");
    }
    return world; // Caller replaces live state only after complete validation.
}

World WorldStore::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(input.is_open(), "cannot open world snapshot");
    const auto size = input.tellg();
    require(size >= 0 && size <= static_cast<std::streamoff>(max_file_size), "invalid world snapshot file size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(input) && input.peek() == std::char_traits<char>::eof(), "cannot read complete world snapshot");
    return decode(bytes, Limits{});
}

void WorldStore::save(const World& world, const std::filesystem::path& path) {
    const auto bytes = encode(world);
    write_file_atomically(path, bytes);
}
}
