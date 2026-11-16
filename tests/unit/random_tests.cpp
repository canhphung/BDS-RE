// bds::Random against outputs of the original BDS Random functions, executed
// under gdb (tools/reference/oracle_random.py -> tests/reference/random_vectors.json).
#include "bds/world/random.hpp"
#include <nlohmann/json.hpp>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const std::string &what) {
    if (!ok)
        throw std::runtime_error("check failed: " + what);
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: random_tests <random_vectors.json>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open vectors");
        const auto vectors = nlohmann::json::parse(input);
        std::size_t compared = 0;
        for (const auto &c : vectors.at("cases")) {
            bds::Random random(c.at("seed").get<std::uint32_t>());
            const auto kind = c.at("kind").get<std::string>();
            const auto &values = c.at("values");
            for (std::size_t i = 0; i < values.size(); ++i, ++compared) {
                const auto where = kind + " seed " + std::to_string(c.at("seed").get<std::uint32_t>()) + " #" + std::to_string(i);
                if (kind == "next_uint")
                    check(random.next_uint() == values[i].get<std::uint32_t>(), where);
                else if (kind == "next_int")
                    check(random.next_int() == values[i].get<std::int32_t>(), where);
                else if (kind == "next_int_n")
                    check(random.next_int(c.at("n").get<std::int32_t>()) == values[i].get<std::int32_t>(), where);
                else if (kind == "next_int_range")
                    check(random.next_int(c.at("min").get<std::int32_t>(), c.at("max").get<std::int32_t>()) ==
                              values[i].get<std::int32_t>(),
                          where);
                else if (kind == "next_float")
                    check(std::bit_cast<std::uint32_t>(random.next_float()) ==
                              std::bit_cast<std::uint32_t>(values[i].get<float>()),
                          where);
                else if (kind == "next_double")
                    check(random.next_double() == values[i].get<double>(), where);
                else if (kind == "next_boolean")
                    check(random.next_boolean() == values[i].get<bool>(), where);
                else if (kind == "next_gaussian")
                    check(std::bit_cast<std::uint32_t>(random.next_gaussian()) ==
                              std::bit_cast<std::uint32_t>(values[i].get<float>()),
                          where);
                else
                    throw std::runtime_error("unknown case " + kind);
            }
        }
        // Reseeding restores the sequence and drops a cached gaussian.
        bds::Random a(2024);
        a.next_gaussian();
        a.set_seed(2024);
        bds::Random b(2024);
        check(a.next_gaussian() == b.next_gaussian(), "reseed clears the gaussian cache");
        std::cout << compared << " Random outputs match the original BDS functions bit for bit\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
