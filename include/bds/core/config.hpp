#pragma once
#include <cstdint>
#include <span>
#include <string_view>
namespace bds {
struct Config { std::uint64_t ticks=0; unsigned tick_ms=50; unsigned max_catch_up=5; };
Config parse_config(std::span<const std::string_view> args);
}
