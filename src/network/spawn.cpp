#include "bds/network/spawn.hpp"
#include "bds/network/crypto.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <nlohmann/json.hpp>
namespace bds::protocol {
Bytes adventure_settings_body(const StartGame &game) {
    ByteWriter out;
    const auto permission = game.settings.default_player_permission;
    out.varint(0x20U | (permission == 0 ? 1U : 0U) | (game.player_game_type == 1 ? 0x40U : 0U));
    out.varint(permission == 2 ? 1 : 0);    // CommandPermissionLevel
    out.varint(permission == 0 ? 0 : 0x1f); // action permissions, as in the reference
    out.varint(static_cast<std::uint32_t>(permission));
    out.varint(0); // custom stored permissions
    out.little_endian(static_cast<std::uint64_t>(game.unique_id), 8);
    return out.take();
}
Bytes game_rules_body(const std::vector<GameRule> &rules) {
    ByteWriter out;
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
    return out.take();
}
Bytes initial_attributes_body(std::uint64_t runtime_id) {
    struct Attribute {
        const char *name;
        float minimum, maximum, current, initial;
    };
    const auto maximum = std::numeric_limits<float>::max();
    const std::array<Attribute, 14> attributes{{Attribute{"health", 0, 20, 20, 20},
                                                {"attack_damage", 1, 1, 1, 1},
                                                {"player.experience", 0, 1, 0, 0},
                                                {"absorption", 0, 16, 0, 0},
                                                {"knockback_resistance", 0, 1, 0, 0},
                                                {"luck", -1024, 1024, 0, 0},
                                                {"fall_damage", 0, maximum, 1, 1},
                                                {"movement", 0, maximum, 0.1f, 0.1f},
                                                {"underwater_movement", 0, maximum, 0.02f, 0.02f},
                                                {"player.hunger", 0, 20, 20, 20},
                                                {"follow_range", 0, 2048, 16, 16},
                                                {"player.exhaustion", 0, 4, 0, 0},
                                                {"player.saturation", 0, 20, 20, 20},
                                                {"player.level", 0, 24791, 0, 0}}};
    ByteWriter out;
    out.varint64(runtime_id);
    out.varint(static_cast<std::uint32_t>(attributes.size()));
    for (const auto &a : attributes) {
        out.float32(a.minimum);
        out.float32(a.maximum);
        out.float32(a.current);
        out.float32(a.initial);
        out.string(std::string("minecraft:") + a.name);
    }
    return out.take();
}
Bytes empty_inventory_body(std::uint32_t window, unsigned slots) {
    if (slots > 256)
        throw ProtocolError("inventory too large");
    ByteWriter out;
    out.varint(window);
    out.varint(slots);
    for (unsigned i = 0; i < slots; ++i)
        out.signed_varint(0); // air item: no further fields
    return out.take();
}
Bytes player_hotbar_body() { return {0, 0, 1}; } // slot 0, inventory window 0, select=true
Bytes empty_crafting_body() { return {0, 1}; }   // no implemented recipes; clear old list
Bytes empty_commands_body() {
    return {0, 0, 0, 0, 0};
} // five vector lists, AvailableCommandsPacket::write
Bytes player_list_body(const auth::Identity &identity, std::int64_t unique_id) {
    using Json = nlohmann::json;
    auto data = Json::parse(identity.client_data_json.empty() ? "{}" : identity.client_data_json);
    const auto text = [&](const char *key) {
        auto i = data.find(key);
        return i != data.end() && i->is_string() ? i->get<std::string>() : std::string{};
    };
    std::string uuid;
    for (const auto c : identity.identity)
        if (c != '-')
            uuid.push_back(c);
    if (uuid.size() != 32)
        throw ProtocolError("invalid player UUID");
    ByteWriter out;
    out.byte(0);
    out.varint(1);
    for (unsigned half = 0; half < 2; ++half) {
        std::uint64_t value = 0;
        const auto *start = uuid.data() + half * 16;
        const auto result = std::from_chars(start, start + 16, value, 16);
        if (result.ec != std::errc{} || result.ptr != start + 16)
            throw ProtocolError("invalid player UUID");
        out.little_endian(value, 8);
    }
    out.signed_varint64(unique_id);
    out.string(identity.display_name);
    out.string(text("ThirdPartyName"));
    const auto os = data.find("DeviceOS");
    out.signed_varint(os != data.end() && os->is_number_integer() ? os->get<std::int32_t>() : 0);
    auto skin_id = text("SkinId");
    if (skin_id.empty())
        skin_id = "Standard_Custom";
    out.string(skin_id);
    const auto binary = [&](const char *field, bool skin) {
        auto decoded = crypto::base64_decode(text(field));
        if (skin && text(field).empty()) { // synthetic protocol clients may omit a skin
            decoded = crypto::Bytes(64 * 64 * 4, 0x7f);
            for (unsigned i = 3; i < decoded->size(); i += 4)
                (*decoded)[i] = 0xff;
        }
        if (!decoded ||
            (skin && decoded->size() != 8192 && decoded->size() != 16384 &&
             decoded->size() != 65536) ||
            (!skin && !decoded->empty() && decoded->size() != 8192))
            throw ProtocolError("invalid player skin/cape");
        out.varint(static_cast<std::uint32_t>(decoded->size()));
        out.raw(*decoded);
    };
    binary("SkinData", true);
    binary("CapeData", false);
    out.string(text("SkinGeometryName"));
    out.string(text("SkinGeometry"));
    out.string(identity.xuid);
    out.string(text("PlatformChatId"));
    return out.take();
}
} // namespace bds::protocol
