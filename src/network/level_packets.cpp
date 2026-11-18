#include "bds/network/level_packets.hpp"
#include "bds/world/random.hpp"
#include <algorithm>
#include <map>
#include <utility>

namespace bds::protocol {
namespace {
// serialize<NetworkBlockPosition>::write (0x1849230): varint x, unsigned varint y, varint z.
void block_position(ByteWriter &out, BlockPosition position) {
    out.signed_varint(position.x);
    out.varint(static_cast<std::uint32_t>(position.y));
    out.signed_varint(position.z);
}

void game_rules(ByteWriter &out, const std::vector<GameRule> &rules) {
    out.varint(static_cast<std::uint32_t>(rules.size()));
    for (const auto &rule : rules) {
        out.string(rule.name);
        if (const auto *flag = std::get_if<bool>(&rule.value)) {
            out.varint(1);
            out.boolean(*flag);
        } else if (const auto *number = std::get_if<std::int32_t>(&rule.value)) {
            out.varint(2);
            out.signed_varint(*number);
        } else {
            out.varint(3);
            out.float32(std::get<float>(rule.value));
        }
    }
}

void level_settings(ByteWriter &out, const LevelSettings &s) {
    out.signed_varint(s.seed);
    out.signed_varint(s.dimension);
    out.signed_varint(static_cast<std::int32_t>(s.generator));
    out.signed_varint(s.game_type);
    out.signed_varint(s.difficulty);
    block_position(out, s.spawn);
    out.boolean(s.achievements_disabled);
    out.signed_varint(s.time);
    out.boolean(s.education_world);
    out.boolean(s.education_features);
    out.float32(s.rain_level);
    out.float32(s.lightning_level);
    out.boolean(s.multiplayer);
    out.boolean(s.lan_broadcast);
    out.boolean(s.xbl_broadcast);
    out.boolean(s.commands_enabled);
    out.boolean(s.texture_packs_required);
    game_rules(out, s.game_rules);
    out.boolean(s.bonus_chest);
    out.boolean(s.start_with_map);
    out.boolean(false);
    out.signed_varint(s.default_player_permission);
    out.signed_varint(s.xbl_broadcast_mode);
    out.little_endian(static_cast<std::uint32_t>(s.server_chunk_tick_range), 4);
    out.boolean(s.platform_broadcast);
    out.signed_varint(s.platform_broadcast_mode);
    out.boolean(s.xbl_broadcast_intent);
    out.boolean(s.locked_behavior_pack);
    out.boolean(s.locked_resource_pack);
    out.boolean(s.from_locked_template);
}
} // namespace

Bytes start_game_body(const StartGame &p) {
    ByteWriter out;
    out.signed_varint64(p.unique_id);
    out.varint64(p.runtime_id);
    out.signed_varint(p.player_game_type);
    out.float32(p.x);
    out.float32(p.y);
    out.float32(p.z);
    out.float32(p.pitch);
    out.float32(p.yaw);
    level_settings(out, p.settings);
    out.string(p.level_id);
    out.string(p.level_name);
    out.string(p.premium_template);
    out.boolean(p.trial);
    out.little_endian(p.current_tick, 8);
    out.signed_varint(p.enchantment_seed);
    out.varint(static_cast<std::uint32_t>(p.palette.size()));
    for (const auto &entry : p.palette) {
        out.string(entry.name);
        out.little_endian(static_cast<std::uint16_t>(entry.data), 2);
    }
    out.string(p.multiplayer_correlation_id);
    return out.take();
}

Bytes set_spawn_position_body(std::int32_t type, BlockPosition position, bool forced) {
    ByteWriter out;
    out.signed_varint(type);
    block_position(out, position);
    out.boolean(forced);
    return out.take();
}

Bytes set_time_body(std::int32_t time) {
    ByteWriter out;
    out.signed_varint(time);
    return out.take();
}

Bytes set_difficulty_body(std::uint32_t difficulty) {
    ByteWriter out;
    out.varint(difficulty);
    return out.take();
}

Bytes set_commands_enabled_body(bool enabled) {
    ByteWriter out;
    out.boolean(enabled);
    return out.take();
}

Bytes chunk_radius_updated_body(std::int32_t radius) {
    ByteWriter out;
    out.signed_varint(radius);
    return out.take();
}

std::int32_t decode_request_chunk_radius(std::span<const std::uint8_t> body) {
    ByteReader reader(body);
    const auto radius = reader.signed_varint();
    reader.finish();
    return radius;
}

std::int32_t client_chunk_radius(std::int32_t requested, std::int32_t view_distance) {
    const auto upper = std::max(5, view_distance);
    return std::clamp(requested, 5, upper);
}

std::vector<GameRule> default_game_rules() {
    return {{"commandblockoutput", true},     {"dodaylightcycle", true},     {"doentitydrops", true},
            {"dofiretick", true},             {"domobloot", true},           {"domobspawning", true},
            {"dotiledrops", true},            {"doweathercycle", true},      {"drowningdamage", true},
            {"falldamage", true},             {"firedamage", true},          {"keepinventory", false},
            {"mobgriefing", true},            {"pvp", true},                 {"showcoordinates", false},
            {"naturalregeneration", true},    {"tntexplodes", true},         {"sendcommandfeedback", true},
            {"experimentalgameplay", false},  {"maxcommandchainlength", std::int32_t{65535}}, {"doinsomnia", true}};
}

namespace {
#include "block_palette_282.inc"
}

namespace {
// The network palette and its lookup: the registry order until
// shuffle_block_palette() runs (BlockPalette::initFromBlockDefinitions).
struct Palette {
    std::vector<PaletteEntry> entries;
    std::map<std::pair<std::string, std::int16_t>, std::uint32_t> index;
    void reindex() {
        index.clear();
        for (std::uint32_t i = 0; i < entries.size(); ++i)
            index.emplace(std::pair{entries[i].name, entries[i].data}, i);
    }
};
std::vector<PaletteEntry> registry_order() {
    std::vector<PaletteEntry> out;
    out.reserve(std::size(palette_entries));
    for (const auto &entry : palette_entries)
        out.push_back({std::string(palette_names[entry.name]), entry.data});
    return out;
}
Palette &palette_state() {
    static Palette palette = [] {
        Palette p{registry_order(), {}};
        p.reindex();
        return p;
    }();
    return palette;
}
} // namespace

const std::vector<PaletteEntry> &vanilla_block_palette() {
    return palette_state().entries;
}

void shuffle_block_palette(Random &level_random) {
    // BlockPalette::initFromBlockDefinitions (0x1d3c0a0): the block states in
    // registry order, std::shuffle with Level::getRandom() (libstdc++'s
    // classic Fisher-Yates over uniform_int_distribution<unsigned long> with
    // Random::operator() = _genRandInt32 in [0, 2^32 - 1]), then
    // assignBlockRuntimeIds: runtime id = index.
    auto &p = palette_state();
    p.entries = registry_order();
    for (std::size_t i = 1; i < p.entries.size(); ++i) {
        const std::uint64_t buckets = i + 1, scaling = 0xffffffffULL / buckets, past = buckets * scaling;
        std::uint64_t x;
        do
            x = level_random.next_uint();
        while (x >= past);
        std::swap(p.entries[i], p.entries[x / scaling]);
    }
    p.reindex();
}

std::optional<std::uint32_t> runtime_id(std::string_view name, std::int16_t data) {
    const auto &index = palette_state().index;
    const auto found = index.find({std::string(name), data});
    if (found == index.end())
        return std::nullopt;
    return found->second;
}

const std::vector<PaletteEntry> &internal_block_palette() {
    static const std::vector<PaletteEntry> palette = [] {
        std::vector<PaletteEntry> result{
            {"minecraft:air", 0}, {"minecraft:stone", 0}, {"minecraft:grass", 0}, {"minecraft:dirt", 0},
            {"minecraft:bedrock", 0}, {"minecraft:water", 0}, {"minecraft:lava", 0}};
        for (unsigned depth=0; depth<16; ++depth)
            result.push_back({"minecraft:flowing_water", static_cast<std::int16_t>(depth)});
        for (unsigned depth=1; depth<16; ++depth)
            result.push_back({"minecraft:water", static_cast<std::int16_t>(depth)});
        // Decoration blocks, appended so every existing internal id keeps its
        // meaning. These are the names the translated features place; see
        // terrain.cpp internal_id and the decoration corpora.
        for (const char *name : {"minecraft:gravel", "minecraft:coal_ore",
                                 "minecraft:iron_ore", "minecraft:gold_ore",
                                 "minecraft:redstone_ore", "minecraft:diamond_ore",
                                 "minecraft:lapis_ore", "minecraft:clay",
                                 "minecraft:sand"})
            result.push_back({name, 0});
        // Stone variants the ore decorator also uses (data 1, 3 and 5).
        for (const std::int16_t data : {1, 3, 5})
            result.push_back({"minecraft:stone", data});
        // Tree blocks (ids 51..53): the base TreeFeature places these.
        result.push_back({"minecraft:log", 0});
        result.push_back({"minecraft:leaves", 0});
        result.push_back({"minecraft:sapling", 0});
        return result;
    }();
    return palette;
}
} // namespace bds::protocol
