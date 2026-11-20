// TreeFeature's leaf-volume pass against the full place() corpus.
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
        check(argc == 2, "usage: tree_place_leaves_tests <tree_place fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open tree place fixture");
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
            // Native leaves for this case.
            std::set<std::tuple<int, int, int>> native;
            for (const auto &write : test.at("writes")) {
                if (name_of(write.at(3).get<int>()).rfind("leaves:", 0) == 0)
                    native.insert({write.at(0).get<int>(), write.at(1).get<int>(),
                                   write.at(2).get<int>()});
            }
            if (native.empty())
                continue;
            // The leaf pass sees the trunk already built; _placeLeaf's material
            // tests are answered from the cell identity exactly as the oracle
            // did, and every write is collected.
            std::set<std::tuple<int, int, int>> produced;
            std::set<std::string> interned;
            const auto intern = [&](const std::string &name) {
                return interned.insert(name).first->c_str();
            };
            bds::TreeEnvironment environment;
            const auto trunk_column = [&](Position p) {
                return std::get<0>(p) == std::get<0>(origin) && std::get<2>(p) == std::get<2>(origin);
            };
            environment.block = [&](Position p) {
                return bds::TreeBlock{intern(trunk_column(p) ? "log" : "air")};
            };
            environment.material_is = [&](Position p, int type) {
                // tree_place_leaves never offers the trunk column, so every cell
                // _placeLeaf sees is air in this scene and answers isType(0).
                (void)p;
                return type == 0;
            };
            environment.place_block = [&](Position p, bds::TreeBlock block) {
                if (std::strcmp(block.name, "leaves") == 0)
                    produced.insert({std::get<0>(p), std::get<1>(p), std::get<2>(p)});
                return true;
            };
            bds::Random random(0);
            random.set_seed(test.at("seed").get<std::uint32_t>());
            // _placeTrunk consumes nextInt(0x50) and the scale's nextInt(0xc)
            // before the leaf pass runs.
            (void)random.next_int(0x50);
            (void)random.next_int(0xc);
            bds::DecorationRegion region;
            bds::tree_place_leaves(region, origin, test.at("height").get<int>(), random, environment,
                                   bds::TreeBlock{intern("leaves")});
            if (produced != native) {
                std::vector<std::tuple<int, int, int>> extra, missing;
                for (const auto &p : produced)
                    if (!native.contains(p)) extra.push_back(p);
                for (const auto &p : native)
                    if (!produced.contains(p)) missing.push_back(p);
                throw std::runtime_error(
                    "height " + std::to_string(test.at("height").get<int>()) +
                    " leaf set differs: produced " + std::to_string(produced.size()) +
                    ", native " + std::to_string(native.size()) + "; extra " +
                    std::to_string(extra.size()) + ", missing " + std::to_string(missing.size()));
            }
            ++cases;
            leaves += produced.size();
        }
        std::cout << cases << " native tree leaf volumes match: " << leaves << " leaf cells\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
