#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
namespace bds {
struct Config {
 std::uint64_t ticks=0;
 unsigned tick_ms=50;
 unsigned max_catch_up=5;
 std::filesystem::path world_file;
 std::filesystem::path root=".";
 bool network=true;
};
Config parse_config(std::span<const std::string_view> args);
}
