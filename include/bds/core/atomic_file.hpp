#pragma once
#include <filesystem>
#include <span>
#include <cstdint>

namespace bds {
void write_file_atomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
void replace_file(const std::filesystem::path& source, const std::filesystem::path& destination);
}
