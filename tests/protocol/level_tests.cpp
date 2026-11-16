// Level packet encoders against golden bytes from an independent Python
// encoder (tests/protocol/make_level_fixtures.py).
#include "bds/network/level_packets.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace bds::protocol;
namespace {
void check(bool ok, const std::string &what) {
    if (!ok)
        throw std::runtime_error("check failed: " + what);
}
Bytes hex(std::string_view text) {
    Bytes out;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16)));
    return out;
}
std::string hex_of(const Bytes &bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}
} // namespace

int main() {
    try {
        ByteWriter w;
        w.signed_varint(-1);
        w.signed_varint(1);
        w.signed_varint(-2147483647 - 1);
        w.signed_varint64(-5);
        w.varint64(0xffffffffffffffffULL);
        check(hex_of(w.bytes()) == "0102ffffffff0f09ffffffffffffffffff01", "zigzag and 64-bit varints");
        for (std::int32_t value : {0, 1, -1, 63, -64, 2147483647, -2147483647 - 1}) {
            ByteWriter out;
            out.signed_varint(value);
            ByteReader in(out.bytes());
            check(in.signed_varint() == value, "signed varint round trip " + std::to_string(value));
        }

        StartGame game;
        game.unique_id = -5;
        game.runtime_id = 7;
        game.player_game_type = 1;
        game.x = -16.5f;
        game.y = 71.62f;
        game.z = 300.5f;
        game.pitch = 10.0f;
        game.yaw = -90.0f;
        auto &s = game.settings;
        s.seed = -123456;
        s.generator = Generator::Flat;
        s.game_type = 1;
        s.difficulty = 3;
        s.spawn = {-17, 70, 300};
        s.time = 6000;
        s.rain_level = 0.25f;
        s.lightning_level = 0.5f;
        s.commands_enabled = true;
        s.default_player_permission = 2;
        s.server_chunk_tick_range = 6;
        game.level_id = "level-id";
        game.level_name = "Bedrock level";
        game.current_tick = 123456789012ULL;
        game.enchantment_seed = -2;
        game.palette = {{"minecraft:air", 0}, {"minecraft:stone", -1}};
        game.multiplayer_correlation_id = "corr";
        const auto golden =
            "090702000084c1713d8f4200409643000020410000b4c2ff880f000402062146d80401e05d00000000803e0000003f0101000100"
            "00000000040006000000000000000000086c6576656c2d69640d426564726f636b206c6576656c0000141a99be1c0000000302"
            "0d6d696e6563726166743a61697200000f6d696e6563726166743a73746f6e65ffff04636f7272";
        check(hex_of(start_game_body(game)) == golden, "StartGame matches the independent encoder:\n" + hex_of(start_game_body(game)));
        check(start_game_body(game).size() == hex(golden).size(), "StartGame length");

        check(hex_of(set_spawn_position_body(0, {-17, 70, 300}, false)) == "002146d80400", "SetSpawnPosition");
        check(hex_of(set_time_body(6000)) == "e05d", "SetTime");
        check(hex_of(set_difficulty_body(3)) == "03", "SetDifficulty");
        check(hex_of(set_commands_enabled_body(true)) == "01", "SetCommandsEnabled");
        check(hex_of(chunk_radius_updated_body(12)) == "18", "ChunkRadiusUpdated");
        check(decode_request_chunk_radius(hex("01")) == -1 && decode_request_chunk_radius(hex("18")) == 12,
              "RequestChunkRadius decode");
        bool trailing = false;
        try {
            decode_request_chunk_radius(hex("1800"));
        } catch (const std::invalid_argument &) {
            trailing = true;
        }
        check(trailing, "RequestChunkRadius trailing bytes rejected");

        // clamp(requested, 5, view distance)
        check(client_chunk_radius(-1, 10) == 5 && client_chunk_radius(3, 10) == 5 && client_chunk_radius(8, 10) == 8 &&
                  client_chunk_radius(32, 10) == 10 && client_chunk_radius(32, 2) == 5,
              "chunk radius clamp");
        check(!internal_block_palette().empty() && internal_block_palette()[0].name == "minecraft:air",
              "palette starts with air (internal id 0)");
        std::cout << "StartGame, level setting and chunk radius encoders match the independent encoder\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
