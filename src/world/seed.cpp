// Level seed resolution, translated from BDS 1.6.0.15 (Linux).
//
//   LevelSettings::parseSeedString 0x1c69910
//   Util::hashCode                0x1949300
//   Util::isIntegral              0x194a340
//   Util::stringTrim              0x270fa50 / 0x270fac0
//   Level::createRandomSeed       0x1c7ff30
//   DedicatedServer::run call site 0x1808d3d
#include "bds/world/seed.hpp"
#include <cstddef>
#include <random>

namespace bds {
namespace {
// Util::stringTrim 0x270fac0 compares each character against the trim set the
// non-overload stringTrim builds at 0x270fa7f; the literal is " \t\n\r".
constexpr std::string_view trim_set = " \t\n\r";

// glibc's sscanf("%d", &value) as the original relies on it: skip whitespace,
// accept one sign, consume decimal digits, saturate the intermediate at the
// 64-bit long bounds on overflow, then store the low 32 bits in an int.
// Returns false when no conversion happens (sscanf returns 0), which is the
// only case where the original keeps the fallback.
bool scan_decimal_int(std::string_view text, std::uint32_t &out) {
    std::size_t at = 0;
    const auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    };
    while (at < text.size() && is_space(text[at]))
        ++at;
    bool negative = false;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        negative = text[at] == '-';
        ++at;
    }
    const auto digits_start = at;
    std::uint64_t magnitude = 0;
    bool saturated = false;
    constexpr std::uint64_t long_max = 0x7fffffffffffffffULL;
    constexpr std::uint64_t long_min_magnitude = 0x8000000000000000ULL;
    for (; at < text.size() && text[at] >= '0' && text[at] <= '9'; ++at) {
        const auto digit = static_cast<std::uint64_t>(text[at] - '0');
        const std::uint64_t limit = negative ? long_min_magnitude : long_max;
        if (saturated || magnitude > (limit - digit) / 10) {
            saturated = true; // strtol clamps, it does not wrap
            magnitude = limit;
            continue;
        }
        magnitude = magnitude * 10 + digit;
    }
    if (at == digits_start)
        return false;
    const std::uint64_t clamped = negative ? (~magnitude + 1) : magnitude;
    out = static_cast<std::uint32_t>(clamped); // the native int truncation
    return true;
}
} // namespace

std::uint32_t create_random_seed() {
    // Level::createRandomSeed 0x1c7ff30 constructs std::random_device("default")
    // and draws uniform_int_distribution<unsigned int>(0, UINT_MAX).
    std::random_device device{"default"};
    return std::uniform_int_distribution<std::uint32_t>{}(device);
}

std::int32_t string_hash_code(std::string_view text) {
    // Util::hashCode 0x1949300: movsbl loads a signed char, and the sum is kept
    // in a 32-bit accumulator (shl $5 / sub = *31, then add).
    std::int32_t hash = 0;
    for (const char ch : text)
        hash = static_cast<std::int32_t>(static_cast<std::uint32_t>(hash) * 31U +
                                        static_cast<std::uint32_t>(static_cast<std::int32_t>(ch)));
    return hash;
}

bool is_integral(std::string_view text) {
    // Util::isIntegral 0x194a340 takes the string by value, erases a leading
    // '-', then rejects any character outside '0'..'9'. An empty string yields
    // true (the loop body never runs); parse_seed_string never passes one
    // because the trimmed string is checked first.
    if (!text.empty() && text.front() == '-')
        text.remove_prefix(1);
    for (const char ch : text)
        if (ch < '0' || ch > '9')
            return false;
    return true;
}

std::string string_trim(std::string_view text) {
    const auto first = text.find_first_not_of(trim_set);
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(trim_set);
    return std::string(text.substr(first, last - first + 1));
}

std::uint32_t parse_seed_string(std::string_view text, std::uint32_t fallback) {
    // 0x1c69923..0x1c6992c: a string shorter than two characters keeps the
    // default. This is why level-seed=1 creates a *random* world: the original
    // falls back to createRandomSeed() and does not hash "1".
    if (text.size() < 2)
        return fallback;
    // 0x1c69942..0x1c69958: "-" followed by nothing meaningful (size == 2 and
    // text[0] == '-') also keeps the default, before any trimming.
    if (text.size() == 2 && text[0] == '-')
        return fallback;
    const std::string trimmed = string_trim(text);
    // 0x1c6997a..0x1c69983: an empty trimmed string keeps the default.
    if (trimmed.empty())
        return fallback;
    // 0x1c69996..0x1c699d1: a leading '0', or "-0", is treated as text and
    // hashed. This is what stops "0123" from losing its leading zero.
    if (trimmed[0] == '0' || (trimmed[0] == '-' && trimmed[1] == '0'))
        return static_cast<std::uint32_t>(string_hash_code(trimmed));
    // 0x1c699f7..0x1c69a2c: non-integral text is hashed.
    if (!is_integral(trimmed))
        return static_cast<std::uint32_t>(string_hash_code(trimmed));
    // 0x1c69a3b..0x1c69a60: sscanf("%d") decides whether a number was read.
    std::uint32_t parsed = 0;
    if (!scan_decimal_int(trimmed, parsed))
        return fallback;
    // 0x1c69a66..0x1c69a82: a parsed -1 is the "no seed" sentinel and falls
    // back, unless the text is literally "-1" (the native comparison against
    // the string constant at 0x2836996 goes the other way).
    if (parsed == 0xffffffffU && trimmed != "-1")
        return fallback;
    return parsed;
}

std::uint32_t resolve_level_seed(std::string_view configured, std::uint32_t random_fallback) {
    return parse_seed_string(configured, random_fallback);
}
} // namespace bds
