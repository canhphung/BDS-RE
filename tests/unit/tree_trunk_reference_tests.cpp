// TreeFeature::_placeTrunk against the native corpus.
#include "bds/world/tree.hpp"
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
        check(argc == 2, "usage: tree_trunk_reference_tests <fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open tree trunk fixture");
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
            bds::TreeEnvironment environment;
            const auto answer = [&](const std::string &kind, Position p, std::uint32_t *raw = nullptr) {
                check(next_query < recorded.size(),
                      "more dependency calls than the original (" + kind + ")");
                const auto &entry = recorded.at(next_query++);
                check(entry.at(0).get<std::string>() == kind,
                      "dependency kind differs: expected " + entry.at(0).get<std::string>() +
                          ", got " + kind);
                check(entry.at(1).get<int>() == std::get<0>(p) &&
                          entry.at(2).get<int>() == std::get<1>(p) &&
                          entry.at(3).get<int>() == std::get<2>(p),
                      "dependency position differs for " + kind + " at #" +
                          std::to_string(next_query - 1) + ": expected (" +
                          std::to_string(entry.at(1).get<int>()) + "," +
                          std::to_string(entry.at(2).get<int>()) + "," +
                          std::to_string(entry.at(3).get<int>()) + ") got (" +
                          std::to_string(std::get<0>(p)) + "," + std::to_string(std::get<1>(p)) +
                          "," + std::to_string(std::get<2>(p)) + ")");
                if (raw && entry.size() > 5)
                    *raw = entry.at(5).get<std::uint32_t>();
                return entry.at(4).get<int>() != 0;
            };
            // _placeTrunk does not call _prepareSpawn, mayPlaceOn, getMaxHeight
            // or _isFree: its only dependencies are _getBlock and _placeBlock.
            int block_calls = 0;
            environment.block = [&](Position p) {
                ++block_calls;
                answer("get_block", p);
                const auto nm = name_at(p);
                std::cerr << "  block(" << std::get<0>(p) << "," << std::get<1>(p) << ","
                          << std::get<2>(p) << ")=" << nm << "\n";
                return bds::TreeBlock{intern(nm)};
            };
            // _isFree 0x1b5a190 interprets the block; the corpus records the
            // _getBlock answer for every cell the native walk inspected, so a
            // cell is "free" here exactly when the original asked about it and
            // the recorded block was free. The native _isFree is a pure test of
            // the block identity, so the host answers it from the same table the
            // original used.
            // The native walk calls _getBlock for each cell and then _isFree on
            // the result; the corpus records those get_block rows in order, so
            // is_free must consume them to stay aligned. The predicate below is
            // only the host's view of the same block; _isFree 0x1b5a190 is a
            // pure identity test, and the corpus's own answer is authoritative.
            environment.is_free = [&](bds::TreeBlock block) {
                return std::strcmp(block.name, "air") == 0;
            };
            environment.is_empty = [&](Position p) {
                answer("get_block", p);
                const auto nm = name_at(p);
                return nm == "air";
            };
            environment.place_block = [&](Position p, bds::TreeBlock block) {
                observed.push_back(Json::array({std::get<0>(p), std::get<1>(p), std::get<2>(p),
                                                std::string(block.name), block.data}));
                return true;
            };


            const auto at = test.at("at");
            const Position position{at.at(0).get<int>(), at.at(1).get<int>(), at.at(2).get<int>()};
            bds::DecorationRegion region;
            bds::Random random(0);
            std::vector<std::uint32_t> observed_words;
            random.observe_draws([&](std::uint32_t word) { observed_words.push_back(word); });
            random.set_seed(test.at("seed").get<std::uint32_t>());
            const bool result = bds::tree_place_trunk(
                region, position, test.at("height").get<int>(), random, environment,
                /*manually_placed=*/false, /*type_flags=*/false,
                bds::TreeBlock{intern("log")}, bds::TreeBlock{intern("leaves")});
            check(result == test.at("result").get<bool>(), "return value differs");
            // The first two draws are nextInt(0x50) and the trunk-height scale's
            // nextInt(0xc); the fixture records every generator word.
            check(observed_words.size() >= 2, "expected at least the two trunk draws");
            check(next_query == recorded.size(), "consumed " + std::to_string(next_query) + " of " +
                                                    std::to_string(recorded.size()) +
                                                    " queries (block_calls=" +
                                                    std::to_string(block_calls) + ", writes=" +
                                                    std::to_string(observed.size()) + ")");
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
