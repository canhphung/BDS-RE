#include "bds/network/level_packets.hpp"
#include <algorithm>

namespace bds::protocol {
namespace {
// serialize<NetworkBlockPosition>::write (0x1849230): varint x, unsigned varint y, varint z.
void block_position(ByteWriter &out, BlockPosition position) {
    out.signed_varint(position.x);
    out.varint(static_cast<std::uint32_t>(position.y));
    out.signed_varint(position.z);
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
    out.varint(0); // game rules (writeVectorList<GameRule>)
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

const std::vector<PaletteEntry> &internal_block_palette() {
    static const std::vector<PaletteEntry> palette{
        {"minecraft:air", 0}, {"minecraft:stone", 0}, {"minecraft:grass", 0}, {"minecraft:dirt", 0},
        {"minecraft:bedrock", 0}};
    return palette;
}
} // namespace bds::protocol
