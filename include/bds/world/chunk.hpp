#pragma once

#include <algorithm>
#include <array>
#include <bitset>
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
    Chunk() { for (auto &column : direct_sky_) column.set(); }
    bool direct_sky(unsigned x, unsigned y, unsigned z) const { return direct_sky_[z*16+x][y]; }

    BlockId block(unsigned x, unsigned y, unsigned z) const {
        validate(x, y, z);
        auto section = sections_.find(y / 16);
        if (section == sections_.end()) return 0;
        return section->second.blocks[index(x, y % 16, z)];
    }

    void set_block(unsigned x, unsigned y, unsigned z, BlockId value) {
        validate(x, y, z);
        auto section = sections_.find(y / 16);
        if (value) allocated_height_ = std::max(allocated_height_, (y / 16 + 1) * 16);
        if (section == sections_.end()) {
            if (value == 0) return;
            section = sections_.try_emplace(y / 16).first;
        }
        auto& data = section->second;
        auto& cell = data.blocks[index(x, y % 16, z)];
        if (cell == value) return;
        if (cell == 0) { ++data.count; ++count_; }
        if (value == 0) { --data.count; --count_; }
        const bool changed_opacity = (cell == 0) != (value == 0);
        cell = value;
        if (changed_opacity) {
            // LevelChunk::_recalcHeight, 0x1de1bf0: old height is masked to
            // 8 bits; scan under the update; write min(new height,255).
            // Direct-sky writes cover the half-open interval between old and
            // new heights (0x1de1f46..0x1de2037). Consequently removing y=255
            // does not reseed that cell as direct sky; lateral light is 14.
            // This palette's non-air blocks all have opacity 15.
            const auto column=z*16+x;
            const unsigned old_height=heightmap_[column];
            unsigned next=old_height;
            if (value && y>=old_height) next=y+1;
            else if (!value && y+1>=old_height) {
                next=std::max(y,old_height);
                while (next>0 && block(x,next-1,z)==0) --next;
            }
            if (next!=old_height) {
                for (unsigned level=std::min(next,old_height);level<std::max(next,old_height);++level)
                    direct_sky_[column].set(level,next<old_height);
                heightmap_[column]=static_cast<std::uint16_t>(std::min(next,255u));
            }
        }
        if (data.count == 0) sections_.erase(section);
    }

    // Original LevelChunk subchunks form a span, not a count of nonempty sections.
    std::uint64_t light_revision() const { return light_revision_; }
    void set_light_revision(std::uint64_t revision) { light_revision_=revision; }
    unsigned allocated_height() const { return allocated_height_; }
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
    std::uint64_t light_revision_ = 0;
    unsigned allocated_height_ = 0;
    std::array<std::uint16_t,256> heightmap_{};
    std::array<std::bitset<256>,256> direct_sky_;
};
}
