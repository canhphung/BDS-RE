// SavannaTreeFeature::place against the native corpus.
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
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;
using Position = bds::DecorationRegion::Position;
void check(bool condition, const std::string &what) {
    if (!condition)
        throw std::runtime_error("check failed: " + what);
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "usage: savanna_tree_tests <fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open savanna fixture");
        const auto reference = Json::parse(input);
        const auto &names = reference.at("block_names");
        const auto name_of = [&](int i) { return names.at(i).get<std::string>(); };
        std::size_t cases = 0, writes = 0, words = 0, queries = 0;
        for (const auto &test : reference.at("cases")) {
            std::size_t next_query = 0;
            const auto recorded = test.at("queries");
            const auto answer = [&](const std::string &kind, Position p) {
                check(next_query < recorded.size(),
                      "more dependency calls than the original at " + kind);
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
            bds::TreeEnvironment environment;
            environment.max_height = [] { return 256; };
            environment.may_place_on = [&](Position p, bds::TreeBlock) {
                const bool r = answer("may_place_on", p);
                if (next_query < recorded.size() &&
                    recorded.at(next_query).at(0).get<std::string>() == "get_block")
                    answer("get_block", p);
                return r;
            };
            environment.block = [&](Position p) {
                answer("get_block", p);
                return bds::TreeBlock{intern("air")};
            };
            environment.is_free = [](bds::TreeBlock) { return true; };
            environment.is_empty = [](Position) { return true; };
            // _isFree accepts Material::isType(0) (air) and isType(7) (leaves),
            // so a cell the feature already filled with a leaf still accepts one
            // more leaf; that is what the duplicate writes in the corpus are.
            std::map<std::tuple<int, int, int>, std::string> filled;
            environment.material_is = [&](Position p, int type) {
                const auto key = std::tuple{std::get<0>(p), std::get<1>(p), std::get<2>(p)};
                const auto found = filled.find(key);
                if (found == filled.end())
                    return type == 0;
                if (found->second == std::string("leaves2"))
                    return type == 7;
                return false;
            };
            environment.place_block = [&](Position p, bds::TreeBlock block) {
                observed.push_back(Json::array({std::get<0>(p), std::get<1>(p), std::get<2>(p),
                                                std::string(block.name), block.data}));
                filled[{std::get<0>(p), std::get<1>(p), std::get<2>(p)}] = std::string(block.name);
                return true;
            };
            bds::Random random(0);
            std::vector<std::uint32_t> observed_words;
            random.observe_draws([&](std::uint32_t w) { observed_words.push_back(w); });
            random.set_seed(test.at("seed").get<std::uint32_t>());
            const auto at = test.at("at");
            const Position position{at.at(0).get<int>(), at.at(1).get<int>(), at.at(2).get<int>()};
            bds::DecorationRegion region;
            const bool result = bds::savanna_tree_place(
                region, position, random, environment, bds::TreeBlock{intern("log2")},
                bds::TreeBlock{intern("leaves2")}, bds::TreeBlock{intern("dirt")});
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
        std::cout << cases << " native SavannaTreeFeature::place cases match: " << writes
                  << " placements, " << words << " Random words, " << queries
                  << " dependency answers\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
