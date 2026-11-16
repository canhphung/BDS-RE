#include "bds/network/crypto.hpp"
#include "bds/network/spawn.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using namespace bds::protocol;
using Json = nlohmann::json;
Bytes hex(const std::string &s) {
    Bytes out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return out;
}
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("spawn reference mismatch");
}
int main(int argc, char **argv) {
    try {
        check(argc == 2);
        std::ifstream input(argv[1]);
        const auto ref = Json::parse(input);
        const auto original = [&](unsigned id) {
            return hex(ref["packets"][std::to_string(id)].get<std::string>());
        };
        StartGame game;
        game.unique_id = ref["unique_id"];
        check(adventure_settings_body(game) == original(0x37));
        check(game_rules_body(default_game_rules()) == original(0x48));
        check(initial_attributes_body(1) == original(0x1d));
        check(player_hotbar_body() == original(0x30));
        crypto::Bytes skin(16384, 0x7f);
        for (std::size_t i = 3; i < skin.size(); i += 4)
            skin[i] = 0xff;
        auth::Identity identity;
        identity.identity = ref["player_uuid"];
        identity.display_name = ref["player_name"];
        identity.client_data_json = Json{
            {"SkinId", "Standard_Custom"},
            {"SkinData", crypto::base64_encode(skin)},
            {"SkinGeometryName", "geometry.humanoid.custom"},
            {"ThirdPartyName", "Reference"},
            {"DeviceOS", 7}}.dump();
        check(player_list_body(identity, game.unique_id) == original(0x3f));
        check(empty_inventory_body(0, 36) ==
              Bytes({0, 36, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                     0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
        check(empty_inventory_body(120, 4) == Bytes({120, 4, 0, 0, 0, 0}));
        check(empty_crafting_body() == Bytes({0, 1}) &&
              empty_commands_body() == Bytes({0, 0, 0, 0, 0}));
        std::cout << "AdventureSettings, rules, attributes, PlayerList and hotbar match original "
                     "reference packets\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
