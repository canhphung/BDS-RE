#pragma once
#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <vector>
namespace bds {
struct Properties {
 std::string server_name="Dedicated Server", level_name="level", seed, level_type="DEFAULT";
 unsigned gamemode=0, difficulty=1, max_players=10, port=19132, port_v6=19133;
 unsigned view_distance=10, tick_distance=4, idle_minutes=30, max_threads=8;
 bool online_mode=true, whitelist=false, cheats=false, texturepack_required=false;
 std::string permission="member";
 std::vector<std::string> diagnostics;
 static Properties parse(std::istream& stream);
 static Properties load(const std::filesystem::path& file);
 std::filesystem::path world_directory(const std::filesystem::path& root) const;
};
unsigned parse_difficulty(std::string_view value);
bool parse_boolean(std::string_view value);
}
