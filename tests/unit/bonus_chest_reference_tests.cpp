// BonusChestFeature against the native corpus.
//
// Each case replays the original's own dependency answers into the translated
// feature, then compares the local Random's words, every placement and the
// caller's stream (which must stay empty). Nothing is computed from an expected
// result.
#include "bds/world/bonus_chest.hpp"
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
        check(argc == 2, "usage: bonus_chest_reference_tests <fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open bonus chest fixture");
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
            check(cells.size() % 8 == 0, "scene framing");
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

        std::size_t cases = 0, writes = 0, words = 0, queries = 0, spawned = 0;
        for (const auto &test : reference.at("cases")) {
            auto blocks = scenes.at(test.at("scene").get<std::size_t>());
            struct Query {
                std::string kind;
                Position position;
                bool result;
            };
            std::vector<Query> recorded;
            for (const auto &query : test.at("queries"))
                recorded.push_back({query.at(0).get<std::string>(),
                                    {query.at(1).get<int>(), query.at(2).get<int>(),
                                     query.at(3).get<int>()},
                                    query.at(4).get<int>() != 0});
            std::size_t next_query = 0;
            const auto answer = [&](const std::string &kind, Position p) {
                check(next_query < recorded.size(), "more queries than the original");
                const auto &entry = recorded[next_query++];
                check(entry.kind == kind, "query kind differs: expected " + entry.kind + ", got " + kind);
                check(entry.position == p, "query position differs");
                return entry.result;
            };

            std::set<std::string> interned;
            const auto intern = [&](const std::string &name) {
                return interned.insert(name).first->c_str();
            };
            std::vector<Json> observed;
            std::vector<std::uint32_t> loot_seeds;
            std::vector<std::uint32_t> loot_raw;
            bool spawned_now = false;
            bds::BonusChestEnvironment environment;
            // findNextTopSolidBlockUnder mutates the position to the found cell.
            environment.find_top_solid_under = [&](Position &p) {
                const bool found = answer("top_solid", p);
                if (found) {
                    // The oracle recorded the position it was called with; the
                    // native function rewrites Y to the top solid block. The
                    // corpus stores the situation in the scene, so resolve the
                    // top solid cell directly from it.
                    int top = std::get<1>(p);
                    for (int y = 25; y >= 2; --y) {
                        const auto found_block = blocks.find({std::get<0>(p), y, std::get<2>(p)});
                        if (found_block != blocks.end()) {
                            const std::string entry = name_of(found_block->second);
                            const auto colon = entry.rfind(':');
                            const std::string base = entry.substr(0, colon);
                            if (base != "air") {
                                top = y;
                                break;
                            }
                        }
                    }
                    std::get<1>(p) = top;
                }
                return found;
            };
            environment.is_empty = [&](Position p) { return answer("is_empty", p); };
            environment.is_considered_solid = [&](Position p) { return answer("solid_below", p); };
            environment.place_block = [&](Position p, bds::BonusChestBlock block) {
                observed.push_back(Json::array({std::get<0>(p), std::get<1>(p), std::get<2>(p),
                                                std::string(block.name), block.data}));
                const std::string entry = std::string(block.name) + ":" + std::to_string(block.data);
                const auto found = index_of.find(entry);
                if (found != index_of.end())
                    blocks[p] = found->second;
            };
            environment.set_loot_table = [&](Position, std::uint32_t seed) {
                loot_seeds.push_back(seed);
            };
            bds::set_bonus_chest_spawned_observer([&] { spawned_now = true; });

            bds::Random caller(0);
            std::vector<std::uint32_t> caller_words;
            caller.observe_draws([&](std::uint32_t word) { caller_words.push_back(word); });
            caller.set_seed(test.at("seed").get<std::uint32_t>());

            const auto at = test.at("at");
            const Position position{at.at(0).get<int>(), at.at(1).get<int>(), at.at(2).get<int>()};
            bds::DecorationRegion region;
            const bool result = bds::place_bonus_chest(
                region, position, caller, environment,
                bds::BonusChestBlock{intern("chest"), 0}, bds::BonusChestBlock{intern("torch"), 0});
            bds::set_bonus_chest_spawned_observer({});

            check(result == test.at("result").get<bool>(), "return value differs");
            // The caller's stream must be untouched: the feature uses its own copy.
            check(caller_words.empty(), "the caller's Random was consumed");
            // The corpus stores the RAW _genRandInt32 word; the loot seed is
            // that word >> 1 because the native call is nextInt() 0x187d470.
            std::vector<std::uint32_t> expected_raw;
            for (const auto seed : loot_seeds)
                expected_raw.push_back(seed << 1);
            const auto recorded_raw = test.at("local_words").get<std::vector<std::uint32_t>>();
            check(recorded_raw.size() == loot_seeds.size(), "loot seed count differs");
            for (std::size_t i = 0; i < loot_seeds.size(); ++i) {
                // nextInt() == raw >> 1, so raw >> 1 must equal what we passed.
                check((recorded_raw[i] >> 1) == loot_seeds[i],
                      "the local Random's loot seed differs");
            }
            const auto expected = test.at("writes");
            check(observed.size() == expected.size(), "placement count differs");
            for (std::size_t i = 0; i < expected.size(); ++i) {
                const auto &want = expected.at(i);
                const std::string entry = name_of(want.at(3).get<int>());
                const auto colon = entry.rfind(':');
                const bool same = observed[i].at(0) == want.at(0) && observed[i].at(1) == want.at(1) &&
                                  observed[i].at(2) == want.at(2) &&
                                  observed[i].at(3) == entry.substr(0, colon) &&
                                  observed[i].at(4).get<int>() ==
                                      std::stoi(entry.substr(colon + 1));
                check(same, "placement " + std::to_string(i) + " differs: got " +
                                observed[i].dump() + ", expected " + want.dump());
            }
            check(next_query == recorded.size(),
                  "consumed " + std::to_string(next_query) + " of " +
                      std::to_string(recorded.size()) + " native queries");
            if (result)
                ++spawned;
            ++cases;
            writes += observed.size();
            words += loot_seeds.size();
            queries += recorded.size();
        }
        check(cases == reference.at("cases").size(), "case count");
        std::cout << cases << " native BonusChestFeature cases match: " << writes
                  << " placements, " << words << " local-Random words, " << queries
                  << " dependency answers, " << spawned << " bonus chests flagged spawned\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
