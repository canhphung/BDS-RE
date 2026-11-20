// JungleTreeFeature::place against the native corpus.
//
// The subclass only draws nextInt(7)+4 and delegates to the verified base
// TreeFeature::place, so this checks the wrapper AND the whole base path on a
// corpus captured through the subclass (523 placements).
#include "bds/world/tree.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <zlib.h>

namespace {
using Json = nlohmann::json;
using Position = bds::DecorationRegion::Position;
void check(bool condition, const std::string &what) {
    if (!condition)
        throw std::runtime_error("check failed: " + what);
}
std::string decode_base64(const std::string &text) {
    static constexpr signed char table[] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1,
        -1, -2, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28,
        29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
        49, 50, 51, -1, -1, -1, -1, -1};
    std::string out;
    unsigned buffer = 0;
    int bits = 0;
    for (const char ch : text) {
        if (ch == '=') break;
        const int value = table[static_cast<unsigned char>(ch)];
        if (value < 0) continue;
        buffer = (buffer << 6) | static_cast<unsigned>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}
std::string inflate(const std::string &data) {
    z_stream stream{};
    check(inflateInit(&stream) == Z_OK, "inflateInit");
    std::string out(1 << 22, '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int status = inflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    inflateEnd(&stream);
    check(status == Z_STREAM_END, "inflate");
    return out;
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "usage: jungle_tree_tests <tree_place fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open jungle fixture");
        const auto reference = Json::parse(input);
        check(reference.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        const auto &names = reference.at("block_names");
        const auto name_of = [&](int index) { return names.at(index).get<std::string>(); };

        std::vector<std::map<std::tuple<int, int, int>, int>> scenes;
        for (const auto &scene : reference.at("scenes")) {
            const std::string cells = inflate(decode_base64(scene.at("cells").get<std::string>()));
            std::map<std::tuple<int, int, int>, int> blocks;
            for (std::size_t at = 0; at < cells.size(); at += 8) {
                std::int16_t x, y, z;
                std::uint16_t index;
                std::memcpy(&x, cells.data() + at, 2);
                std::memcpy(&y, cells.data() + at + 2, 2);
                std::memcpy(&z, cells.data() + at + 4, 2);
                std::memcpy(&index, cells.data() + at + 6, 2);
                blocks[{x, y, z}] = index;
            }
            scenes.push_back(std::move(blocks));
        }

        std::size_t cases = 0, leaves = 0;
        for (const auto &test : reference.at("cases")) {
            const auto at = test.at("at");
            const Position origin{at.at(0).get<int>(), at.at(1).get<int>(), at.at(2).get<int>()};
            std::size_t next_query = 0;
            const auto recorded = test.at("queries");
            std::set<std::string> interned;
            const auto intern = [&](const std::string &name) {
                return interned.insert(name).first->c_str();
            };
            const auto answer = [&](const std::string &kind, Position p) {
                check(next_query < recorded.size(), "more dependency calls than the original");
                const auto &entry = recorded.at(next_query++);
                check(entry.at(0).get<std::string>() == kind,
                      "dependency kind differs: expected " +
                          entry.at(0).get<std::string>() + ", got " + kind);
                check(entry.at(1).get<int>() == std::get<0>(p) &&
                          entry.at(2).get<int>() == std::get<1>(p) &&
                          entry.at(3).get<int>() == std::get<2>(p),
                      "dependency position differs for " + kind + " at #" +
                          std::to_string(next_query - 1));
                return entry.at(4).get<int>() != 0;
            };
            std::vector<Json> observed;
            bds::TreeEnvironment environment;
            environment.max_height = [] { return 256; };
            environment.may_place_on = [&](Position p, bds::TreeBlock) {
                const bool result = answer("may_place_on", p);
                if (next_query < recorded.size() &&
                    recorded.at(next_query).at(0).get<std::string>() == "get_block")
                    answer("get_block", p);
                return result;
            };
            environment.block = [&](Position p) {
                answer("get_block", p);
                return bds::TreeBlock{intern("air")};
            };
            environment.is_free = [](bds::TreeBlock) { return true; };
            environment.is_empty = [](Position) { return true; };
            environment.material_is = [](Position, int type) { return type == 0; };
            environment.place_block = [&](Position p, bds::TreeBlock block) {
                observed.push_back(Json::array({std::get<0>(p), std::get<1>(p), std::get<2>(p),
                                                std::string(block.name), block.data}));
                return true;
            };
            bds::Random random(0);
            std::vector<std::uint32_t> observed_words;
            random.observe_draws([&](std::uint32_t word) { observed_words.push_back(word); });
            random.set_seed(test.at("seed").get<std::uint32_t>());
            bds::DecorationRegion region;
            const bool result = bds::jungle_tree_place(
                region, origin, random, environment,
                /*manually_placed=*/false, /*type_flags=*/false,
                bds::TreeBlock{intern("log")}, bds::TreeBlock{intern("leaves")},
                bds::TreeBlock{intern("dirt")});
            check(result == test.at("result").get<bool>(), "return value differs");
            check(observed_words == test.at("words").get<std::vector<std::uint32_t>>(),
                  "Random words differ: got " + std::to_string(observed_words.size()) +
                      ", expected " + std::to_string(test.at("words").size()));
            const auto expected = test.at("writes");
            check(observed.size() == expected.size(),
                  "placement count " + std::to_string(observed.size()) + " vs " +
                      std::to_string(expected.size()));
            for (std::size_t i = 0; i < expected.size(); ++i) {
                const auto &want = expected.at(i);
                const std::string entry = name_of(want.at(3).get<int>());
                const auto colon = entry.rfind(':');
                const bool same = observed[i].at(0) == want.at(0) &&
                                  observed[i].at(1) == want.at(1) &&
                                  observed[i].at(2) == want.at(2) &&
                                  observed[i].at(3) == entry.substr(0, colon) &&
                                  observed[i].at(4).get<int>() == std::stoi(entry.substr(colon + 1));
                check(same, "placement " + std::to_string(i) + " differs: got " +
                                observed[i].dump() + ", expected " + want.dump());
            }
            check(next_query == recorded.size(),
                  "consumed " + std::to_string(next_query) + " of " +
                      std::to_string(recorded.size()) + " queries");
            ++cases;
            leaves += observed.size();
        }
        std::cout << cases << " native JungleTreeFeature::place cases match: " << leaves
                  << " placements\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
