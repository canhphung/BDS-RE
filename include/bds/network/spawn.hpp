#pragma once
#include "bds/network/auth.hpp"
#include "bds/network/level_packets.hpp"
namespace bds::protocol {
Bytes adventure_settings_body(const StartGame &game);
Bytes game_rules_body(const std::vector<GameRule> &rules);
Bytes initial_attributes_body(std::uint64_t runtime_id);
Bytes empty_inventory_body(std::uint32_t window, unsigned slots);
Bytes player_hotbar_body();
Bytes empty_crafting_body();
Bytes empty_commands_body();
Bytes player_list_body(const auth::Identity &identity, std::int64_t unique_id);
} // namespace bds::protocol
