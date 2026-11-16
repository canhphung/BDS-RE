#pragma once
#include "bds/network/binary.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Server-to-client level data sent when a player joins
// (ServerNetworkHandler::_sendLevelData 0x182fd90). Layouts follow the
// protocol-282 writers; evidence in reverse/evidence/network_constants.json
// ("start_game").
namespace bds::protocol {
struct BlockPosition {
    std::int32_t x = 0, y = 0, z = 0;
};

// GeneratorType from LevelSettings::generatorTypeToString (0x1c697c0).
enum class Generator : std::int32_t { Legacy = 0, Overworld = 1, Flat = 2, Nether = 3, TheEnd = 4 };

// serialize<LevelSettings>::write (0x184b8b0), in wire order.
struct LevelSettings {
    std::int32_t seed = 0;
    std::int32_t dimension = 0;
    Generator generator = Generator::Overworld;
    std::int32_t game_type = 0;
    std::int32_t difficulty = 1;
    BlockPosition spawn{0, 64, 0};
    bool achievements_disabled = true;
    std::int32_t time = 0;
    bool education_world = false;
    bool education_features = false;
    float rain_level = 0.0f;
    float lightning_level = 0.0f;
    bool multiplayer = true;
    bool lan_broadcast = true;
    bool xbl_broadcast = false;
    bool commands_enabled = false;
    bool texture_packs_required = false;
    // Game rules: GameRulesChangedPacketData, unsigned-varint counted; none sent.
    bool bonus_chest = false;
    bool start_with_map = false;
    // One constant false is written here (xor %esi at 0x184bbf2).
    std::int32_t default_player_permission = 1; // PlayerPermissionLevel: visitor 0, member 1, operator 2
    std::int32_t xbl_broadcast_mode = 0;
    std::int32_t server_chunk_tick_range = 4; // signed 32-bit little-endian
    bool platform_broadcast = false;
    std::int32_t platform_broadcast_mode = 0;
    bool xbl_broadcast_intent = false;
    bool locked_behavior_pack = false;
    bool locked_resource_pack = false;
    bool from_locked_template = false;
};

// BlockPaletteSerializationData: name string then signed 16-bit data value.
struct PaletteEntry {
    std::string name;
    std::int16_t data = 0;
};

// StartGamePacket::write (0x184a2b0), in wire order.
struct StartGame {
    std::int64_t unique_id = 1;   // ActorUniqueID, zigzag varint64
    std::uint64_t runtime_id = 1; // ActorRuntimeID, unsigned varint64
    std::int32_t player_game_type = 0;
    float x = 0.5f, y = 65.62f, z = 0.5f;
    float pitch = 0.0f, yaw = 0.0f;
    LevelSettings settings;
    std::string level_id;
    std::string level_name;
    std::string premium_template;
    bool trial = false;
    std::uint64_t current_tick = 0;
    std::int32_t enchantment_seed = 0;
    std::vector<PaletteEntry> palette;
    std::string multiplayer_correlation_id;
};

Bytes start_game_body(const StartGame &packet);
Bytes set_spawn_position_body(std::int32_t type, BlockPosition position, bool forced); // 0x184f690
Bytes set_time_body(std::int32_t time);                                               // 0x184f820
Bytes set_difficulty_body(std::uint32_t difficulty);                                 // 0x184f960
Bytes set_commands_enabled_body(bool enabled);                                       // 0x184faa0
Bytes chunk_radius_updated_body(std::int32_t radius);                                // 0x18cde50
std::int32_t decode_request_chunk_radius(std::span<const std::uint8_t> body);        // 0x18cd780

// ServerPlayer::findClientChunkRadius (0x25fd620): clamp(requested, 5,
// min(view distance, dimension limit)). The dimension limit comes from an
// unresolved virtual call and is not applied here.
std::int32_t client_chunk_radius(std::int32_t requested, std::int32_t view_distance);

// The blocks this server's internal World can contain, by internal id.
// Not the original's complete palette (built at runtime by
// BlockPalette::initFromBlockDefinitions); compatibility with a real client
// is unverified.
const std::vector<PaletteEntry> &internal_block_palette();
} // namespace bds::protocol
