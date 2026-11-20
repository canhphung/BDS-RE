// SwampTreeFeature::place against the native corpus.
//
// SavannaTreeFeature::place 0x1b511c0 is the first subclass with a real body:
// height = nextInt(3)+nextInt(3)+5, a leaning trunk, a 3x3 plate plus four tips
// at the trunk top, a 7x7 slab with the corners cut one level below, and one
// side branch with its own canopy. Blocks are log2/leaves2.
//
// The interesting part is the material model: _placeLeafAt's _isFree accepts
// air AND leaves, so the branch canopy can re-place leaves the slab already
// wrote and the corpus records those duplicate writes. A harness that treats
// every filled cell as blocked loses nine writes in case 12.
#include "bds/world/tree.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>

namespace {
using Json = nlohmann::json;
void check(bool condition, const std::string &what) {
    if (!condition)
        throw std::runtime_error("check failed: " + what);
}
std::string decode_base64(const std::string &text) {
    static constexpr signed char table[] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,
        -1,-2,-1,-1,-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,-1,26,27,28,
        29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,
        49,50,51,-1,-1,-1,-1,-1};
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

using Position = bds::DecorationRegion::Position;
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "usage: swamp_tree_tests <fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open swamp fixture");
        const auto reference = Json::parse(input);
        const auto &names = reference.at("block_names");
        const auto name_of = [&](int i) { return names.at(i).get<std::string>(); };
        // The corpus's query answers are all zero; the world each case ran
        // against is scenes[case.scene], a zlib+base64 <hhhH> cell list. Decode
        // them once and simulate forward from there.
        std::vector<std::map<std::tuple<int, int, int>, std::string>> worlds;
        for (const auto &scene : reference.at("scenes")) {
            const std::string cells = inflate(decode_base64(scene.at("cells").get<std::string>()));
            std::map<std::tuple<int, int, int>, std::string> cells_by_pos;
            for (std::size_t at = 0; at < cells.size(); at += 8) {
                std::int16_t x, y, z;
                std::uint16_t index;
                std::memcpy(&x, cells.data() + at, 2);
                std::memcpy(&y, cells.data() + at + 2, 2);
                std::memcpy(&z, cells.data() + at + 4, 2);
                std::memcpy(&index, cells.data() + at + 6, 2);
                const std::string entry = name_of(index);
                cells_by_pos[{x, y, z}] = entry.substr(0, entry.rfind(':'));
            }
            worlds.push_back(std::move(cells_by_pos));
        }
        std::size_t cases = 0, writes = 0, words = 0, queries = 0;
        for (const auto &test : reference.at("cases")) {
            std::size_t next_query = 0;
            const auto recorded = test.at("queries");
            const auto answer = [&](const std::string &kind, Position p) {
                check(next_query < recorded.size(),
                      "more dependency calls than the original at " + kind + " #" +
                          std::to_string(next_query) + " for case " + test.at("at").dump() +
                          " words=" + std::to_string(test.at("words").size()));
                const auto &entry = recorded.at(next_query++);
                check(entry.at(0).get<std::string>() == kind,
                      "kind differs at #" + std::to_string(next_query - 1) + ": expected " +
                          entry.at(0).get<std::string>() + ", got " + kind);
                check(entry.at(1).get<int>() == std::get<0>(p) &&
                          entry.at(2).get<int>() == std::get<1>(p) &&
                          entry.at(3).get<int>() == std::get<2>(p),
                      "position differs for " + kind + " at #" + std::to_string(next_query - 1));
                return entry.at(4).get<int>() != 0;
            };
            std::set<std::string> interned;
            const auto intern = [&](const std::string &n) {
                return interned.insert(n).first->c_str();
            };
            std::vector<Json> observed;
            std::map<std::tuple<int, int, int>, std::string> world =
                worlds.at(test.at("scene").get<std::size_t>());
            const auto name_at = [&](Position p) {
                const auto found = world.find({std::get<0>(p), std::get<1>(p), std::get<2>(p)});
                return found == world.end() ? std::string("air") : found->second;
            };
            // MaterialType ids the swamp corpus distinguishes.
            const auto material_of = [](const std::string &name) {
                if (name == "air")
                    return 0;
                if (name == "sand")
                    return 4;
                if (name == "leaves")
                    return 7;
                if (name == "log")
                    return 9;
                if (name == "vine")
                    return 3;
                return 2; // grass / dirt / stone share the ground material
            };
            bds::TreeEnvironment environment;
            environment.max_height = [] { return 256; };
            environment.may_place_on = [&](Position p, bds::TreeBlock) {
                return answer("may_place_on", p);
            };
            environment.block = [&](Position p) {
                answer("get_block", p);
                return bds::TreeBlock{intern(name_at(p))};
            };
            environment.is_solid_blocking = [&](Position p) {
                const std::string name = name_at(p);
                return name != "air" && name != "leaves" && name != "vine";
            };
            environment.is_free = [](bds::TreeBlock) { return true; };
            environment.is_empty = [](Position) { return true; };
            environment.material_is = [&](Position p, int type) {
                return material_of(name_at(p)) == type;
            };
            environment.place_block = [&](Position p, bds::TreeBlock block) {
                observed.push_back(Json::array({std::get<0>(p), std::get<1>(p), std::get<2>(p),
                                                std::string(block.name), block.data}));
                world[{std::get<0>(p), std::get<1>(p), std::get<2>(p)}] = std::string(block.name);
                return true;
            };
            bds::Random random(0);
            std::vector<std::uint32_t> observed_words;
            random.observe_draws([&](std::uint32_t w) { observed_words.push_back(w); });
            random.set_seed(test.at("seed").get<std::uint32_t>());
            const auto at = test.at("at");
            const Position position{at.at(0).get<int>(), at.at(1).get<int>(), at.at(2).get<int>()};
            bds::DecorationRegion region;
            const bool result = bds::swamp_tree_place(
                region, position, random, environment, bds::TreeBlock{intern("log")},
                bds::TreeBlock{intern("leaves")}, bds::TreeBlock{intern("dirt")});
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
                check(observed[i].at(0) == want.at(0) && observed[i].at(1) == want.at(1) &&
                          observed[i].at(2) == want.at(2) &&
                          observed[i].at(3) == entry.substr(0, colon) &&
                          observed[i].at(4).get<int>() == std::stoi(entry.substr(colon + 1)),
                      "placement " + std::to_string(i) + " differs: got " + observed[i].dump() +
                          ", expected " + want.dump());
            }
            check(next_query == recorded.size(),
                  "consumed " + std::to_string(next_query) + " of " +
                      std::to_string(recorded.size()) + " queries");
            ++cases;
            writes += observed.size();
            words += observed_words.size();
            queries += recorded.size();
        }
        std::cout << cases << " native SwampTreeFeature::place cases match: " << writes
                  << " placements, " << words << " Random words, " << queries
                  << " dependency answers\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
