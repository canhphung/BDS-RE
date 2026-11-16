#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace bds {
using BlockId = std::uint16_t; // Internal IDs; no wire palette assigned yet.

// C++ division truncates toward zero; chunk coordinates need floor division.
constexpr int chunk_coordinate(int block) {
    return block / 16 - (block % 16 < 0 ? 1 : 0);
}
constexpr unsigned local_coordinate(int block) {
    const int remainder = block % 16;
    return static_cast<unsigned>(remainder < 0 ? remainder + 16 : remainder);
}

class Chunk {
public:
    static constexpr unsigned height = 256;

    BlockId block(unsigned x, unsigned y, unsigned z) const {
        validate(x, y, z);
        auto section = sections_.find(y / 16);
        if (section == sections_.end()) return 0;
        return section->second.blocks[index(x, y % 16, z)];
    }

    void set_block(unsigned x, unsigned y, unsigned z, BlockId value) {
        validate(x, y, z);
        auto section = sections_.find(y / 16);
        if (section == sections_.end()) {
            if (value == 0) return;
            section = sections_.try_emplace(y / 16).first;
        }
        auto& data = section->second;
        auto& cell = data.blocks[index(x, y % 16, z)];
        if (cell == value) return;
        if (cell == 0) { ++data.count; ++count_; }
        if (value == 0) { --data.count; --count_; }
        cell = value;
        if (data.count == 0) sections_.erase(section);
    }

    std::size_t stored_blocks() const { return count_; }
    std::size_t allocated_sections() const { return sections_.size(); }

    template<class Visitor> void for_each_block(Visitor visit) const {
        for (const auto& [section, data] : sections_)
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned z = 0; z < 16; ++z)
                    for (unsigned x = 0; x < 16; ++x) {
                        const auto value = data.blocks[index(x, y, z)];
                        if (value != 0) visit(x, section * 16 + y, z, value);
                    }
    }

private:
    struct Section {
        std::array<BlockId, 4096> blocks{};
        std::size_t count = 0;
    };
    static void validate(unsigned x, unsigned y, unsigned z) {
        if (x >= 16 || z >= 16 || y >= height)
            throw std::out_of_range("block outside chunk bounds");
    }
    // Independent in-memory ordering; not a Bedrock serialization assertion.
    static constexpr std::size_t index(unsigned x, unsigned y, unsigned z) {
        return (y * 16 + z) * 16 + x;
    }
    std::map<unsigned, Section> sections_;
    std::size_t count_ = 0;
};
}
