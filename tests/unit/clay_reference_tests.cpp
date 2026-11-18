// ClayFeature against the native corpus.
#include "bds/world/clay.hpp"
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
        if (ch == '=')
            break;
        const int value = table[static_cast<unsigned char>(ch)];
        if (value < 0)
            continue;
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
        check(argc == 2, "usage: clay_reference_tests <fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open clay fixture");
        const auto reference = Json::parse(input);
        check(reference.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        const auto &names = reference.at("block_names");
        const auto name_of = [&](int index) { return names.at(index).get<std::string>(); };
        std::map<std::string, int> index_of;
        for (std::size_t i = 0; i < names.size(); ++i)
            index_of[names.at(i).get<std::string>()] = static_cast<int>(i);

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

        std::size_t cases = 0, writes = 0, words = 0, queries = 0;
        for (const auto &test : reference.at("cases")) {
            auto blocks = scenes.at(test.at("scene").get<std::size_t>());
            std::size_t next_query = 0;
            const auto recorded = test.at("queries");
            std::set<std::string> interned;
            const auto intern = [&](const std::string &name) {
                return interned.insert(name).first->c_str();
            };
            const auto name_at = [&](Position p) -> std::string {
                const auto found = blocks.find(p);
                if (found == blocks.end())
                    return "air";
                const std::string entry = name_of(found->second);
                return entry.substr(0, entry.rfind(':'));
            };
            std::vector<Json> observed;
            bds::ClayEnvironment environment;
            environment.is_water = [&](Position p) {
                check(next_query < recorded.size(), "more queries than the original");
                const auto &entry = recorded.at(next_query++);
                check(entry.at(0).get<std::string>() == "material_is_water",
                      "query kind differs");
                check(entry.at(1).get<int>() == std::get<0>(p) &&
                          entry.at(2).get<int>() == std::get<1>(p) &&
                          entry.at(3).get<int>() == std::get<2>(p),
                      "query position differs");
                return entry.at(4).get<int>() != 0;
            };
            environment.block = [&](Position p) {
                return bds::ClayBlock{intern(name_at(p))};
            };
            environment.place_block = [&](Position p, bds::ClayBlock block) {
                observed.push_back(Json::array({std::get<0>(p), std::get<1>(p), std::get<2>(p),
                                                std::string(block.name), block.data}));
            };

            bds::Random random(0);
            std::vector<std::uint32_t> observed_words;
            random.observe_draws([&](std::uint32_t word) { observed_words.push_back(word); });
            random.set_seed(test.at("seed").get<std::uint32_t>());

            const auto at = test.at("at");
            const Position position{at.at(0).get<int>(), at.at(1).get<int>(), at.at(2).get<int>()};
            bds::DecorationRegion region;
            const bool result = bds::place_clay(region, position, test.at("size").get<int>(),
                                                random, environment, bds::ClayBlock{intern("clay")});
            check(result == test.at("result").get<bool>(), "return value differs");
            check(observed_words == test.at("words").get<std::vector<std::uint32_t>>(),
                  "Random words differ");
            check(next_query == recorded.size(), "consumed " + std::to_string(next_query) + " of " +
                                                    std::to_string(recorded.size()) + " queries");
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
            ++cases;
            writes += observed.size();
            words += observed_words.size();
            queries += recorded.size();
        }
        std::cout << cases << " native ClayFeature cases match: " << writes << " placements, "
                  << words << " Random words, " << queries << " dependency answers\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
