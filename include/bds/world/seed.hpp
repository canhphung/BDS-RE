#pragma once
// Level seed resolution translated from BDS 1.6.0.15 (Linux).
//
// DedicatedServer::run() (call site 0x1808d3d) calls
// LevelSettings::parseSeedString(getLevelSeed(), Level::createRandomSeed()),
// so an absent or unusable level-seed falls back to a fresh std::random_device
// draw instead of a fixed value. parseSeedString is 0x1c69910.
#include <cstdint>
#include <string>
#include <string_view>

namespace bds {
// The seed used when the configured level-seed is absent or unusable:
// Level::createRandomSeed 0x1c7ff30 draws uniform_int_distribution<unsigned
// int>(0, numeric_limits<unsigned int>::max()) from std::random_device
// "default". BDS-RE keeps the same source and range.
std::uint32_t create_random_seed();

// Util::hashCode 0x1949300: h = 31 * h + (signed char) c over the bytes, with
// 32-bit wraparound (the original accumulates in an int register).
std::int32_t string_hash_code(std::string_view text);

// Util::isIntegral 0x194a340 (takes the string by value): a leading '-' is
// erased, then every remaining character must be an ASCII digit. An empty
// string returns true, and the original then indexes element 0 anyway, so the
// caller must never pass an empty string.
bool is_integral(std::string_view text);

// Util::stringTrim 0x270fa50 trims the character set " \t\n\r".
std::string string_trim(std::string_view text);

// LevelSettings::parseSeedString 0x1c69910. Returns the resolved seed; the
// second argument is the default the original passes (createRandomSeed()).
//
// Native control flow, in order:
//  1. if text.size() < 2 return default;
//  2. if text[0] == '-' and text.size() == 2 return default;
//  3. trimmed = stringTrim(text); if trimmed is empty return default;
//  4. if trimmed[0] == '0' or (trimmed[0] == '-' and trimmed[1] == '0')
//     return string_hash_code(trimmed);
//  5. if !isIntegral(trimmed) return string_hash_code(trimmed);
//  6. value = sscanf(trimmed, "%d") as glibc parses it; on a parse failure
//     return default;
//  7. if trimmed != "-1" and value == -1 return default;
//  8. return value (an unsigned int holding the parsed int's bits).
std::uint32_t parse_seed_string(std::string_view text, std::uint32_t fallback);

// Convenience wrapper used by the runtime and the packet layer: an empty
// configured seed means the original passes createRandomSeed()'s value.
std::uint32_t resolve_level_seed(std::string_view configured,
                                 std::uint32_t random_fallback = create_random_seed());
} // namespace bds
