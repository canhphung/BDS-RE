#pragma once
#include <filesystem>

namespace bds {
void replace_file(const std::filesystem::path& source, const std::filesystem::path& destination);
}
