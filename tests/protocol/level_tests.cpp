// Level packet encoders against golden bytes from an independent Python
// encoder (tests/protocol/make_level_fixtures.py).
#include "bds/network/level_packets.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
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

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: level_tests <reference StartGame>");
        const char *reference = argv[1];
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
        s.achievements_disabled = true;
        s.xbl_broadcast_mode = 0;
        s.platform_broadcast_mode = 0;
        s.game_rules = {{"pvp", true}, {"maxcommandchainlength", std::int32_t{65535}}, {"x", 0.5f}};
        game.premium_template = "";
        game.level_id = "level-id";
        game.level_name = "Bedrock level";
        game.current_tick = 123456789012ULL;
        game.enchantment_seed = -2;
        game.palette = {{"minecraft:air", 0}, {"minecraft:stone", -1}};
        game.multiplayer_correlation_id = "corr";
        const auto golden =
            "090702000084c1713d8f4200409643000020410000b4c2ff880f000402062146d80401e05d00000000803e0000003f010100"
            "010003037076700101156d6178636f6d6d616e64636861696e6c656e67746802feff070178030000003f0000000400060000"
            "00000000000000086c6576656c2d69640d426564726f636b206c6576656c0000141a99be1c00000003020d6d696e65637261"
            "66743a61697200000f6d696e6563726166743a73746f6e65ffff04636f7272";
        check(start_game_body(game) == hex(golden), "StartGame independent golden bytes");

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

        // DIFFERENTIAL: with the values the original BDS 1.6.0.15 sent for a new
        // world, the encoder reproduces its StartGame byte for byte (captured
        // with tools/reference/capture_reference.py; palette in its order).
        {
            std::ifstream input(reference, std::ios::binary);
            check(static_cast<bool>(input), "open reference StartGame");
            const Bytes original((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            StartGame ref;
            ref.unique_id = -12884901887LL;
            ref.runtime_id = 1;
            ref.player_game_type = 0;
            ref.x = 68.5f;
            ref.y = 32769.62109375f;
            ref.z = 4.5f;
            ref.settings.seed = 181554818;
            ref.settings.spawn = {68, 32767, 4};
            ref.settings.time = 4173;
            ref.level_id = "Reference";
            ref.level_name = "Reference";
            ref.current_tick = 4173;
            ref.enchantment_seed = 399804244;
            ref.palette = vanilla_block_palette();
            ref.multiplayer_correlation_id = "02322db7-0cad-4f4b-b8ff-ee61473aa4f3";
            const auto ours = start_game_body(ref);
            std::size_t first_difference = 0;
            while (first_difference < std::min(ours.size(), original.size()) && ours[first_difference] == original[first_difference])
                ++first_difference;
            check(ours == original, "StartGame differs from the original at byte " + std::to_string(first_difference) +
                                        " (ours " + std::to_string(ours.size()) + ", original " + std::to_string(original.size()) + ")");
            check(vanilla_block_palette().size() == 4493 && runtime_id("minecraft:air", 0).has_value() &&
                      vanilla_block_palette()[*runtime_id("minecraft:stone", 0)].name == "minecraft:stone" &&
                      !runtime_id("minecraft:not_a_block", 0),
                  "palette lookup");
        }

        // clamp(requested, 5, view distance)
        check(client_chunk_radius(-1, 10) == 5 && client_chunk_radius(3, 10) == 5 && client_chunk_radius(8, 10) == 8 &&
                  client_chunk_radius(32, 10) == 10 && client_chunk_radius(32, 2) == 5,
              "chunk radius clamp");
        for (const auto &block : internal_block_palette())
            check(runtime_id(block.name, block.data).has_value(), "internal block in the vanilla palette: " + block.name);
        std::cout << "StartGame, level setting and chunk radius encoders match the independent encoder\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
