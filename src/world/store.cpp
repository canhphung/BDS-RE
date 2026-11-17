#include "bds/world/store.hpp"
#include "bds/game/items.hpp"
#include "bds/core/atomic_file.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>

namespace bds {
namespace {
constexpr std::array<std::uint8_t, 8> magic{'B', 'D', 'S', 'R', 'E', 'W', '\r', '\n'};
constexpr std::size_t header_size = 28, record_size = 12, checksum_size = 4;
constexpr std::size_t max_file_size = header_size + 1'000'000 * record_size + 16 * 1024 * 1024 + 8;

void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void append(std::vector<std::uint8_t> &out, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
std::uint64_t read(std::span<const std::uint8_t> bytes, std::size_t offset, unsigned width) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
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
};
} // namespace

std::vector<std::uint8_t> WorldStore::encode(const World &world) {
    const bool extended = world.terrain_.kind != 0 || !world.players_.empty() || !world.water_ticks_.empty();
    std::vector<std::uint8_t> records;
    std::size_t count = 0, sections = 0;
    const auto record = [&](World::Position position, BlockId block) {
        const auto [x, y, z] = position;
        append(records, static_cast<std::uint32_t>(x), 4);
        append(records, static_cast<unsigned>(y), 2);
        append(records, static_cast<std::uint32_t>(z), 4);
        append(records, block, 2);
        ++count;
    };
    for (const auto &[key, column] : world.columns_) {
        if (world.terrain_.kind && !world.legacy_columns_.contains(key))
            continue;
        sections += column.allocated_sections();
        column.for_each_block([&](unsigned x, unsigned y, unsigned z, BlockId id) {
            record({static_cast<int>(std::int64_t(key.first) * 16 + x), static_cast<int>(y),
                    static_cast<int>(std::int64_t(key.second) * 16 + z)},
                   id);
        });
    }
    require(count <= Limits{}.max_blocks, "world exceeds snapshot block limit");
    require(sections <= Limits{}.max_sections, "world exceeds snapshot section limit");
    std::vector<std::uint8_t> bytes(magic.begin(), magic.end());
    append(bytes, extended ? 2 : 1, 4);
    append(bytes, world.age(), 8);
    append(bytes, count, 8);
    bytes.insert(bytes.end(), records.begin(), records.end());
    if (extended) {
        using Json = nlohmann::json;
        Json metadata = {{"kind", world.terrain_.kind},
                         {"seed", world.terrain_.seed},
                         {"legacy", Json::array()},
                         {"edits", Json::array()},
                         {"players", Json::object()}};
        for (const auto &key : world.legacy_columns_)
            metadata["legacy"].push_back({key.first, key.second});
        for (const auto &[key, column] : world.columns_)
            if (!world.terrain_.kind && !world.legacy_columns_.contains(key))
                metadata["legacy"].push_back({key.first, key.second});
        for (const auto &[position, id] : world.edits_) {
            const auto [x, y, z] = position;
            metadata["edits"].push_back({x, y, z, id});
        }
        metadata["water_ticks"] = Json::array();
        metadata["water_clocks"] = Json::array();
        for (const auto &[chunk, queue] : world.water_ticks_) {
            metadata["water_clocks"].push_back({chunk.first, chunk.second, queue.clock});
            // Preserve heap layout, including duplicate positions.
            for (const auto &tick : queue.heap) {
                const auto [x, y, z] = tick.position;
                metadata["water_ticks"].push_back({x, y, z, tick.due, tick.serial});
            }
        }
        metadata["water_sequence"] = world.water_sequence_;
        require(world.players_.size() <= 1024, "player limit exceeded");
        for (const auto &[key, p] : world.players_) {
            // [item, count] or [item, count, damage] for a damaged item.
            const auto stack = [](const ItemStack &item) {
                return item.aux ? Json{item.block, item.count, item.aux} : Json{item.block, item.count};
            };
            Json inventory = Json::array(), armor = Json::array();
            for (const auto &item : p.inventory)
                inventory.push_back(stack(item));
            for (const auto &item : p.armor)
                armor.push_back(stack(item));
            metadata["players"][key] = {{"pos", {p.x, p.y, p.z}},
                                        {"rot", {p.pitch, p.yaw, p.head_yaw}},
                                        {"selected", p.selected},
                                        {"inventory", inventory},
                                        {"cursor", stack(p.cursor)},
                                        {"armor", armor},
                                        {"attributes", {p.health, p.hunger, p.saturation, p.exhaustion}}};
        }
        const auto text = metadata.dump();
        require(text.size() <= 16 * 1024 * 1024, "world metadata limit exceeded");
        append(bytes, text.size(), 4);
        bytes.insert(bytes.end(), text.begin(), text.end());
    }
    append(bytes, crc32(bytes), 4);
    return bytes;
}

World WorldStore::decode(std::span<const std::uint8_t> bytes, Limits limits) {
    require(bytes.size() >= header_size + checksum_size, "truncated world snapshot");
    require(std::equal(magic.begin(), magic.end(), bytes.begin()), "invalid world magic");
    const auto version = read(bytes, 8, 4);
    require(version == 1 || version == 2, "unsupported world snapshot version");
    const auto count = read(bytes, 20, 8);
    require(count <= limits.max_blocks && count <= 1'000'000,
            "world snapshot block limit exceeded");
    const auto end = header_size + record_size * count;
    if (version == 1)
        require(bytes.size() == end + checksum_size, "world snapshot size mismatch");
    else {
        require(bytes.size() >= end + 8, "truncated world metadata");
        const auto length = read(bytes, end, 4);
        require(length <= 16 * 1024 * 1024 && bytes.size() == end + length + 8,
                "invalid world metadata size");
    }
    require(crc32(bytes.first(bytes.size() - 4)) == read(bytes, bytes.size() - 4, 4),
            "world snapshot checksum mismatch");
    World world;
    world.age_ = read(bytes, 12, 8);
    std::size_t sections = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = header_size + record_size * i;
        const auto x =
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(bytes, offset, 4)));
        const auto y = static_cast<int>(read(bytes, offset + 4, 2));
        const auto z =
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(bytes, offset + 6, 4)));
        const auto block = static_cast<BlockId>(read(bytes, offset + 10, 2));
        require(y < static_cast<int>(Chunk::height) && block != 0, "invalid world block record");
        const World::Position position{x, y, z};
        require(world.block(position) == 0, "duplicate world block record");
        // Count only the touched column's sections, avoiding quadratic scans.
        const auto key = std::pair{chunk_coordinate(x), chunk_coordinate(z)};
        auto column = world.columns_.find(key);
        const auto before =
            column == world.columns_.end() ? 0 : column->second.allocated_sections();
        world.set_block(position, block,
                        0); // restoring must not wake adjacent still water
        sections += world.columns_.at(key).allocated_sections() - before;
        require(sections <= limits.max_sections, "world snapshot section limit exceeded");
    }
    if (version == 2) {
        using Json = nlohmann::json;
        const auto metadata =
            Json::parse(bytes.begin() + static_cast<std::ptrdiff_t>(end + 4), bytes.end() - 4);
        const auto integer = [&](const Json &j, std::int64_t low, std::int64_t high) {
            require(j.is_number_integer(), "invalid world metadata integer");
            if (j.is_number_unsigned())
                require(j.get<std::uint64_t>() <= static_cast<std::uint64_t>(high),
                        "world integer overflow");
            auto value = j.get<std::int64_t>();
            require(value >= low && value <= high, "world integer out of range");
            return value;
        };
        world.terrain_.kind = static_cast<unsigned>(integer(metadata.at("kind"), 0, 2));
        require(metadata.at("seed").is_number_unsigned(), "invalid terrain seed");
        world.terrain_.seed = metadata.at("seed").get<std::uint64_t>();
        const auto &legacy = metadata.at("legacy"), &edits = metadata.at("edits"),
                   &players = metadata.at("players");
        require(legacy.is_array() && legacy.size() <= 65536 && edits.is_array() &&
                    edits.size() <= 100000 && players.is_object() && players.size() <= 1024,
                "world metadata count limit");
        for (const auto &key : legacy) {
            require(key.is_array() && key.size() == 2, "invalid legacy column");
            auto x = static_cast<int>(integer(key[0], INT32_MIN / 16, INT32_MAX / 16)),
                 z = static_cast<int>(integer(key[1], INT32_MIN / 16, INT32_MAX / 16));
            require(world.legacy_columns_.insert({x, z}).second, "duplicate legacy column");
        }
        for (const auto &[key, column] : world.columns_)
            require(world.legacy_columns_.contains(key), "unmarked legacy column");
        for (const auto &edit : edits) {
            require(edit.is_array() && edit.size() == 4, "invalid terrain edit");
            World::Position p{static_cast<int>(integer(edit[0], INT32_MIN, INT32_MAX)),
                              static_cast<int>(integer(edit[1], 0, 255)),
                              static_cast<int>(integer(edit[2], INT32_MIN, INT32_MAX))};
            const auto [x, y, z] = p;
            require(world.terrain_.kind &&
                        !world.legacy_columns_.contains({chunk_coordinate(x), chunk_coordinate(z)}),
                    "terrain edit overlaps legacy column");
            require(
                world.edits_.emplace(p, static_cast<BlockId>(integer(edit[3], 0, 65535))).second,
                "duplicate terrain edit");
        }
        for (auto it = players.begin(); it != players.end(); ++it) {
            require(!it.key().empty() && it.key().size() <= 128, "invalid player key");
            const auto &value = it.value(), &pos = value.at("pos"), &rot = value.at("rot"),
                       &inventory = value.at("inventory");
            require(pos.is_array() && pos.size() == 3 && rot.is_array() && rot.size() == 3 &&
                        inventory.is_array() && inventory.size() == 36,
                    "invalid player state");
            PlayerData p;
            const auto number = [&](const Json &j) {
                require(j.is_number(), "invalid player coordinate");
                const auto n = j.get<float>();
                require(std::isfinite(n) && std::abs(n) <= 30000000, "invalid player coordinate");
                return n;
            };
            p.x = number(pos[0]);
            p.y = number(pos[1]);
            p.z = number(pos[2]);
            require(p.y >= -64 && p.y <= 320, "invalid player height");
            p.pitch = number(rot[0]);
            p.yaw = number(rot[1]);
            p.head_yaw = number(rot[2]);
            p.selected = static_cast<unsigned>(integer(value.at("selected"), 0, 8));
            const auto stack = [&](const Json &j) {
                require(j.is_array() && (j.size() == 2 || j.size() == 3), "invalid item stack");
                ItemStack item{static_cast<std::uint16_t>(integer(j[0], 0, game::item_count() - 1)),
                               static_cast<std::uint8_t>(integer(j[1], 0, 64))};
                const auto &info = game::item_info(item.block);
                if (j.size() == 3)
                    item.aux = static_cast<std::int16_t>(integer(j[2], 0, info.max_damage));
                require((item.block == 0) == (item.count == 0) && item.count <= info.max_stack,
                        "invalid item stack");
                return item;
            };
            for (unsigned i = 0; i < 36; ++i)
                p.inventory[i] = stack(inventory[i]);
            if (value.contains("cursor"))
                p.cursor = stack(value["cursor"]);
            if (value.contains("armor")) {
                const auto &armor = value["armor"];
                require(armor.is_array() && armor.size() == 4, "invalid armor");
                for (unsigned i = 0; i < 4; ++i) {
                    p.armor[i] = stack(armor[i]);
                    const auto &info = game::item_info(p.armor[i].block);
                    require(!p.armor[i].block || (info.kind == game::ItemKind::Armor && info.armor_slot == static_cast<int>(i)),
                            "invalid armor");
                }
            }
            if (value.contains("attributes")) {
                const auto &a = value["attributes"];
                require(a.is_array() && a.size() == 4, "invalid player attributes");
                const auto ranged = [&](const Json &j, float hi) {
                    require(j.is_number(), "invalid player attribute");
                    const auto n = j.get<float>();
                    require(std::isfinite(n) && n >= 0 && n <= hi, "invalid player attribute");
                    return n;
                };
                p.health = ranged(a[0], 20);
                p.hunger = ranged(a[1], 20);
                p.saturation = ranged(a[2], 20);
                p.exhaustion = ranged(a[3], 4);
            }
            world.players_.emplace(it.key(), p);
        }
        // Optional host queue metadata keeps older snapshots readable.
        if (metadata.contains("water_ticks")) {
            const auto &ticks = metadata.at("water_ticks");
            require(ticks.is_array() && ticks.size() <= 1'000'000, "invalid water tick queue");
            require(metadata.at("water_sequence").is_number_unsigned(), "invalid water sequence");
            world.water_sequence_ = metadata.at("water_sequence").get<std::uint64_t>();
            world.water_ticks_.clear();
            std::set<std::uint64_t> orders;
            const auto &clocks = metadata.at("water_clocks");
            require(clocks.is_array() && clocks.size() <= 1'000'000, "invalid water clocks");
            for (const auto &clock : clocks) {
                require(clock.is_array() && clock.size() == 3 && clock[0].is_number_integer() &&
                            clock[1].is_number_integer() && clock[2].is_number_unsigned(),
                        "invalid water clock");
                const int x = static_cast<int>(integer(clock[0], INT32_MIN / 16, INT32_MAX / 16));
                const int z = static_cast<int>(integer(clock[1], INT32_MIN / 16, INT32_MAX / 16));
                require(clock[2].get<std::uint64_t>() <= world.age_,
                        "water clock exceeds world age");
                require(world.water_ticks_
                            .emplace(std::pair{x, z},
                                     World::WaterQueue{clock[2].get<std::uint64_t>(), {}})
                            .second,
                        "duplicate water clock");
            }
            for (const auto &tick : ticks) {
                require(tick.is_array() && tick.size() == 5, "invalid water tick");
                const int x = static_cast<int>(integer(tick[0], INT32_MIN, INT32_MAX));
                const int y = static_cast<int>(integer(tick[1], 0, 255));
                const int z = static_cast<int>(integer(tick[2], INT32_MIN, INT32_MAX));
                require(tick[3].is_number_unsigned() && tick[4].is_number_unsigned(),
                        "invalid water deadline/order");
                const auto due = tick[3].get<std::uint64_t>(), order = tick[4].get<std::uint64_t>();
                require(order < world.water_sequence_ && orders.insert(order).second,
                        "duplicate water tick order");
                const auto queue =
                    world.water_ticks_.find({chunk_coordinate(x), chunk_coordinate(z)});
                require(queue != world.water_ticks_.end(), "missing water clock");
                auto &heap = queue->second.heap;
                heap.push_back(World::WaterTick{{x, y, z}, due, order});
                require(heap.size() == 1 || heap[(heap.size() - 2) / 2].due <= due,
                        "invalid water heap");
            }
        }
    }
    return world; // Caller replaces live state only after complete validation.
}

World WorldStore::load(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(input.is_open(), "cannot open world snapshot");
    const auto size = input.tellg();
    require(size >= 0 && size <= static_cast<std::streamoff>(max_file_size),
            "invalid world snapshot file size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(input) && input.peek() == std::char_traits<char>::eof(),
            "cannot read complete world snapshot");
    return decode(bytes, Limits{});
}

void WorldStore::save(const World &world, const std::filesystem::path &path) {
    const auto bytes = encode(world);
    write_file_atomically(path, bytes);
}
} // namespace bds
