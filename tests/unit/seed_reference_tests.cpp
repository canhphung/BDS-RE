// Level seed resolution against original LevelSettings::parseSeedString cases.
#include "bds/world/seed.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const std::string &what) {
    if (!condition)
        throw std::runtime_error("check failed: " + what);
}
std::string hex32(std::uint32_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(8, '0');
    for (int i = 7; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[value & 15];
        value >>= 4;
    }
    return out;
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "usage: seed_reference_tests <fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open seed fixture");
        const auto reference = nlohmann::json::parse(input);
        check(reference.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        check(reference.at("address") == "0x1c69910", "unexpected function address");

        std::size_t hashed = 0, parsed = 0, fell_back = 0;
        for (const auto &test : reference.at("cases")) {
            const auto text = test.at("input").get<std::string>();
            const auto fallback = test.at("default").get<std::uint32_t>();
            const auto expected = test.at("unsigned").get<std::uint32_t>();
            const auto actual = bds::parse_seed_string(text, fallback);
            if (actual != expected)
                throw std::runtime_error("seed parse differs from the original for " +
                                         std::string(text) + ": got 0x" + hex32(actual) +
                                         " expected 0x" + hex32(expected));
            if (expected == fallback && fallback != 0)
                ++fell_back;
            else if (actual == static_cast<std::uint32_t>(bds::string_hash_code(bds::string_trim(text))))
                ++hashed;
            else
                ++parsed;
        }

        // The native helpers individually, with values from the same oracle.
        check(bds::string_hash_code("abc") == 96354, "hashCode(\"abc\")");
        check(bds::string_hash_code("bedrock") == static_cast<std::int32_t>(0xf232d366U),
              "hashCode(\"bedrock\")");
        check(bds::string_hash_code("Bedrock level") == 1201671722, "hashCode(\"Bedrock level\")");
        check(bds::string_hash_code("hello world") == 1794106052, "hashCode(\"hello world\")");
        // movsbl sign-extends the bytes of a multi-byte UTF-8 string.
        check(bds::string_hash_code("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") == 1439989690,
              "hashCode(multibyte)");
        // Util::isIntegral erases a leading '-' and then requires every
        // remaining character to be a digit; an empty remainder passes because
        // the loop does not run (the original relies on its caller's non-empty
        // check before indexing element 0).
        check(bds::is_integral("123") && bds::is_integral("-123") && bds::is_integral("") &&
                  !bds::is_integral("12a") && !bds::is_integral("1.5"),
              "isIntegral digits and signs");
        check(bds::string_trim("  -42\t\n") == "-42", "stringTrim");
        check(bds::string_trim(" \t\n\r ") == "", "stringTrim of the trim set");

        // Level::createRandomSeed must be able to produce the full unsigned
        // range; the original draws 0..UINT_MAX from std::random_device.
        bool saw_large = false, saw_differing = false;
        const auto first = bds::create_random_seed();
        for (int i = 0; i < 64; ++i) {
            const auto value = bds::create_random_seed();
            saw_large = saw_large || value > 0x10000U;
            saw_differing = saw_differing || value != first;
        }
        check(saw_large && saw_differing, "createRandomSeed draws a wide random value");

        // The bug this slice fixes: a one-character seed is *not* hashed, it
        // falls back to the runtime's random default.
        check(bds::parse_seed_string("1", 424242) == 424242,
              "one-character seed keeps the fallback");
        check(bds::parse_seed_string("12", 424242) == 12, "two-character numeric seed parses");
        check(bds::parse_seed_string("abc", 0) == 96354, "text seed hashes");

        std::cout << reference.at("cases").size() << " original parseSeedString cases match ("
                  << hashed << " hashed, " << parsed << " parsed, " << fell_back
                  << " fell back to the random default)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
