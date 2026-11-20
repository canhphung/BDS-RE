// TreeFeature::_placeLeaf against the native function corpus.
#include "bds/world/tree.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
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
        check(argc == 2, "usage: tree_leaf_reference_tests <fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open tree leaf fixture");
        const auto reference = Json::parse(input);
        check(reference.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        check(reference.at("address") == "0x1b59d60", "unexpected function address");

        std::size_t cases = 0, placements = 0, type_answers = 0;
        for (const auto &test : reference.at("cases")) {
            const auto at = test.at("at");
            const Position position{at.at(0).get<int>(), at.at(1).get<int>(),
                                    at.at(2).get<int>()};
            const auto block = test.at("block").get<std::string>();
            // The corpus stores the three isType answers in native order; -1
            // means the original short-circuited before that call.
            const auto recorded = test.at("is_type").get<std::vector<int>>();
            std::size_t next_answer = 0;
            std::set<std::string> interned;
            const auto intern = [&](const std::string &name) {
                return interned.insert(name).first->c_str();
            };
            std::vector<Json> observed;
            bds::TreeEnvironment environment;
            environment.block = [&](Position p) {
                check(p == position, "the feature queried a cell other than the anchor");
                return bds::TreeBlock{intern(block.substr(0, block.rfind(':')))};
            };
            environment.material_is = [&](Position p, int type) {
                check(p == position, "material query on the wrong cell");
                check(next_answer < recorded.size(), "more isType calls than the original");
                const int expected = recorded[next_answer++];
                check(expected != -1, "the original had already short-circuited here");
                (void)type;
                ++type_answers;
                return expected != 0;
            };
            environment.place_block = [&](Position p, bds::TreeBlock leaf) {
                observed.push_back(Json::array({std::get<0>(p), std::get<1>(p), std::get<2>(p),
                                                std::string(leaf.name), leaf.data}));
                // _placeLeaf returns _placeBlock's bool. Placing leaves on an
                // existing leaves cell changes nothing, so the native write
                // reports false; the corpus records exactly that for leaves:0.
                return block != std::string("leaves") + ":0";
            };
            bds::DecorationRegion region;
            const bool result =
                bds::tree_place_leaf(region, position, environment, bds::TreeBlock{intern("leaves")});
            check(result == test.at("result").get<bool>(),
                  "return value differs for " + block + ": got " +
                      (result ? "true" : "false") + ", expected " +
                      (test.at("result").get<bool>() ? "true" : "false"));
            // Every recorded answer must have been consumed; a remaining -1 tail
            // is fine because the native short-circuited there too.
            for (std::size_t i = next_answer; i < recorded.size(); ++i)
                check(recorded[i] == -1, "the original asked a question this replay skipped");
            const auto expected = test.at("writes");
            check(observed.size() == expected.size(),
                  "placement count " + std::to_string(observed.size()) + " vs " +
                      std::to_string(expected.size()) + " for " + block);
            for (std::size_t i = 0; i < expected.size(); ++i) {
                const auto &want = expected.at(i);
                const std::string entry = want.at(3).get<std::string>();
                const auto colon = entry.rfind(':');
                const bool same = observed[i].at(0) == want.at(0) &&
                                  observed[i].at(1) == want.at(1) &&
                                  observed[i].at(2) == want.at(2) &&
                                  observed[i].at(3) == entry.substr(0, colon) &&
                                  observed[i].at(4).get<int>() == std::stoi(entry.substr(colon + 1));
                check(same, "placement " + std::to_string(i) + " differs for " + block);
            }
            ++cases;
            placements += observed.size();
        }
        std::cout << cases << " native _placeLeaf cases match: " << placements
                  << " placements, " << type_answers << " Material::isType answers\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
