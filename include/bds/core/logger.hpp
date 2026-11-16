#pragma once
#include <iostream>
#include <string_view>
#include <syncstream>
namespace bds { inline void log(std::string_view text) { std::osyncstream(std::cout) << "[bds] " << text << '\n'; } }
