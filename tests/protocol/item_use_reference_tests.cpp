#include "bds/game/server.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using namespace bds::game;
using Json = nlohmann::json;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
Bytes unhex(const std::string &text) {
    check(text.size() % 2 == 0, "odd reference hex length");
    Bytes bytes;
    for (std::size_t i = 0; i < text.size(); i += 2)
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return bytes;
}
std::string hex(const Bytes &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (auto byte : bytes) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}
Bytes expected_body(const Json &packet, const Json &fixture) {
    auto body = unhex(packet.at("body").get<std::string>());
    if (packet.at("id") != 0x15)
        return body;
    // A runtime ID is an index into that connection's StartGame palette, not a
    // stable block identifier. Translate only this field, retaining all other
    // captured bytes (including flags, layer and duplicate updates) unchanged.
    ByteReader in(body);
    in.signed_varint();
    in.varint();
    in.signed_varint();
    const auto prefix_size = body.size() - in.remaining().size();
    const auto original_id = in.varint();
    for (const auto &entry : fixture.at("runtime_palette")) {
        if (entry.at("runtime_id") != original_id)
            continue;
        const auto &advertised = protocol::vanilla_block_palette();
        for (std::size_t i = 0; i < advertised.size(); ++i) {
            if (advertised[i].name != entry.at("name").get<std::string>() ||
                advertised[i].data != entry.at("data").get<std::int16_t>())
                continue;
            ByteWriter out;
            out.raw(std::span(body).first(prefix_size));
            out.varint(static_cast<std::uint32_t>(i));
            out.raw(in.remaining());
            return out.take();
        }
        throw std::runtime_error("captured block missing from advertised palette");
    }
    throw std::runtime_error("captured runtime ID missing from reference palette");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 2, "expected reference fixture path");
        std::ifstream file(argv[1]);
        const auto fixture = Json::parse(file);
        check(fixture.at("source_binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        check(fixture.at("cases").size() == 37, "incomplete original ItemUse corpus");
        for (const auto &test : fixture.at("cases")) {
            const auto name = test.at("name").get<std::string>();
            World world;
            // The same explicit console-built patch as the reference harness;
            // this test makes no claim about matching original world generation.
            for (int x = -4; x <= 4; ++x)
                for (int z = -4; z <= 4; ++z)
                    for (int y = 63; y <= 64; ++y)
                        world.set_block({x, y, z}, 1);
            world.set_block({2, 64, 1}, 2);
            Properties properties;
            AccessLists access(".");
            Server server(world, properties, access);
            auth::Identity identity;
            identity.identity = "00000000-0000-4000-8000-000000000001";
            identity.display_name = "Audit";
            protocol::StartGame game;
            game.unique_id = game.runtime_id = 1;
            game.player_game_type = 0;
            game.x = fixture.at("player").at("position")[0];
            game.y = fixture.at("player").at("position")[1];
            game.z = fixture.at("player").at("position")[2];
            server.prepare(identity, game);
            server.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
            auto &player = world.players().at("uuid:" + identity.identity);
            player.selected = fixture.at("player").at("selected_slot");
            for (std::size_t i = 0; i < player.inventory.size(); ++i) {
                player.inventory[i].block = fixture.at("player").at("inventory")[i][0];
                player.inventory[i].count = fixture.at("player").at("inventory")[i][1];
            }
            const auto before = player;
            const auto request = test.at("request");
            const auto result = server.handle(1,
                                              {{request.at("id").get<std::uint16_t>(), 0, 0},
                                               unhex(request.at("body").get<std::string>())},
                                              1000);
            const auto &expected = test.at("responses");
            check(result.size() == expected.size(),
                  name + ": response count differs from original");
            for (std::size_t i = 0; i < result.size(); ++i) {
                check(result[i].header.id == expected[i].at("id"), name + ": packet ID differs");
                const auto translated = expected_body(expected[i], fixture);
                check(result[i].body == translated,
                      name + ": response body differs at packet " + std::to_string(i) +
                          ": actual=" + hex(result[i].body) + " expected=" + hex(translated));
            }
            check(server.drain(1).empty(), name + ": unexpected queued correction");
            check(player.inventory == before.inventory && player.cursor == before.cursor &&
                      player.selected == before.selected && player.x == before.x &&
                      player.y == before.y && player.z == before.z,
                  name + ": rejected request changed player state");
            check(world.block({2, 64, 1}) == 2, name + ": rejected request destroyed target");
            for (const auto &pos : test.at("block_update_positions")) {
                const int x = pos[0], y = pos[1], z = pos[2];
                const auto expected_block = x == 2 && y == 64 && z == 1 ? 2 : y <= 64 ? 1 : 0;
                check(world.block({x, y, z}) == expected_block, name + ": world state changed");
            }
        }
        std::cout << "37 original ItemUse cases match after StartGame "
                     "runtime-palette translation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
