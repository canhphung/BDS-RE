// Small surface features against the native vegetation corpus.
//
// Each case replays the original's own dependency answers (recorded by
// tools/re/gdb_vegetation_cases.py) into the translated feature, then compares
// every Random word, every Feature::_placeBlock attempt and the final block
// state. Nothing is computed from an expected result.
#include "bds/world/vegetation.hpp"
#include <algorithm>
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
void check(bool condition, const std::string &what) {
    if (!condition)
        throw std::runtime_error("check failed: " + what);
}

// base64 + zlib for the scene cells (same pair the terrain fixture uses).
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
    std::string out(1 << 21, '\0');
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
        check(argc == 2, "usage: vegetation_reference_tests <fixture>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open vegetation fixture");
        const auto reference = Json::parse(input);
        check(reference.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        const auto &names = reference.at("block_names");
        const auto name_of = [&](int index) { return names.at(index).get<std::string>(); };
        std::map<std::string, int> index_of;
        for (std::size_t i = 0; i < names.size(); ++i)
            index_of[names.at(i).get<std::string>()] = static_cast<int>(i);

        // Rebuild the deduplicated scenes.
        std::vector<std::map<std::tuple<int, int, int>, int>> scenes;
        for (const auto &scene : reference.at("scenes")) {
            const std::string cells = inflate(decode_base64(scene.at("cells").get<std::string>()));
            check(cells.size() % 8 == 0, "scene cell framing");
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

        std::size_t cases = 0, words = 0, writes = 0, dependencies = 0;
        std::size_t case_index = 0;
        for (const auto &test : reference.at("cases")) {
            ++case_index;
            const auto feature = test.at("feature").get<std::string>();
            // `changed` is the RESULT (cells the feature modified), so it must not
            // be applied before the run: the feature has to see the initial scene.
            const auto initial = scenes.at(test.at("scene").get<std::size_t>());
            auto blocks = initial;

            // Recorded dependency answers, consumed in native order.
            struct Dependency {
                std::string kind;
                Position position;
                bool result;
                std::uint32_t raw;
                int argument;
            };
            std::vector<Dependency> recorded;
            for (const auto &dep : test.at("dependencies"))
                recorded.push_back({dep.at(0).get<std::string>(),
                                    {dep.at(1).get<int>(), dep.at(2).get<int>(), dep.at(3).get<int>()},
                                    dep.at(4).get<int>() != 0,
                                    dep.size() > 4 ? dep.at(5).get<std::uint32_t>() : 0U,
                                    dep.size() > 6 ? dep.at(6).get<int>() : 0});
            std::size_t next_dependency = 0;
            const auto answer = [&](const std::string &kind, Position position) -> const Dependency & {
                check(next_dependency < recorded.size(),
                      feature + ": feature asked more predicates than the original");
                const auto &dep = recorded[next_dependency++];
                if (dep.kind != kind) {
                    const auto &a = dep.position; const auto &b = position;
                    throw std::runtime_error(feature + " case " + std::to_string(case_index) +
                        ": predicate #" + std::to_string(next_dependency - 1) + " expected " + dep.kind +
                        " at (" + std::to_string(std::get<0>(a)) + "," + std::to_string(std::get<1>(a)) +
                        "," + std::to_string(std::get<2>(a)) + ") got " + kind + " at (" +
                        std::to_string(std::get<0>(b)) + "," + std::to_string(std::get<1>(b)) + "," +
                        std::to_string(std::get<2>(b)) + ")");
                }
                check(dep.kind == kind,
                      feature + ": predicate order differs, expected " + dep.kind + ", got " + kind);
                if (dep.position != position) {
                    const auto &a = dep.position;
                    const auto &b = position;
                    throw std::runtime_error(
                        feature + " case " + std::to_string(case_index) + ": predicate #" + std::to_string(next_dependency - 1) +
                        " (" + dep.kind + ") expected (" + std::to_string(std::get<0>(a)) + "," +
                        std::to_string(std::get<1>(a)) + "," + std::to_string(std::get<2>(a)) +
                        ") got (" + std::to_string(std::get<0>(b)) + "," +
                        std::to_string(std::get<1>(b)) + "," + std::to_string(std::get<2>(b)) + ")");
                }
                return dep;
            };

            // Intern names so FeatureBlock's `const char*` outlives the call.
            std::set<std::string> interned;
            const auto intern = [&](const std::string &name) {
                return interned.insert(name).first->c_str();
            };
            // block_names entries are "name:data"; FeatureBlock carries the
            // two separately, the way the original's Block does.
            const auto block_at = [&](Position p) -> bds::FeatureBlock {
                const auto found = blocks.find(p);
                const std::string entry = found == blocks.end() ? std::string("air:0")
                                                                : name_of(found->second);
                const auto colon = entry.rfind(':');
                return bds::FeatureBlock{intern(entry.substr(0, colon)),
                                         static_cast<std::uint16_t>(std::stoi(entry.substr(colon + 1)))};
            };
            // Material pointers recorded by the oracle: the host answers the
            // MaterialType question by comparing the recorded pointer with the
            // material of a reference cell of that type.
            const auto material_is = [&](Position p, int) {
                return answer("material", p).result;
            };
            std::vector<Json> observed_writes;
            bds::VegetationEnvironment environment;
            environment.is_empty = [&](Position p) { return answer("is_empty", p).result; };
            environment.can_survive = [&](Position p, bds::FeatureBlock) {
                const bool result = answer("can_survive", p).result;
                // Block::canSurvive for cactus queries four neighbour materials
                // inside its own body; the oracle recorded them right after, so
                // consume them here to keep the stream aligned.
                while (next_dependency < recorded.size() &&
                       recorded[next_dependency].kind == "material")
                    ++next_dependency;
                return result;
            };
            environment.may_place = [&](Position p, bds::FeatureBlock) {
                return answer("may_place", p).result;
            };
            environment.may_place_face = [&](Position p, bds::FeatureBlock, int face) {
                const auto &dep = answer("may_place_face", p);
                check(dep.argument == face, feature + ": mayPlace face differs");
                return dep.result;
            };
            environment.heightmap = [&](Position p) {
                return static_cast<int>(answer("heightmap", p).raw);
            };
            environment.may_place_on = [&](Position p, bds::FeatureBlock) {
                return answer("may_place_on", p).result;
            };
            environment.block = block_at;
            environment.material_is = material_is;
            environment.has_property = [&](Position p, int property) {
                const auto &dep = answer("has_property", p);
                (void)property;
                return dep.result;
            };
            environment.place_block = [&](Position p, bds::FeatureBlock block) {
                observed_writes.push_back(Json::array(
                    {std::get<0>(p), std::get<1>(p), std::get<2>(p),
                     std::string(block.name), block.data}));
                // The placed block is visible to every later candidate in the
                // same feature call, exactly as the native BlockSource is.
                const std::string entry =
                    std::string(block.name) + ":" + std::to_string(block.data);
                const auto found = index_of.find(entry);
                if (found != index_of.end())
                    blocks[{std::get<0>(p), std::get<1>(p), std::get<2>(p)}] = found->second;
            };

            bds::Random random(0);
            std::vector<std::uint32_t> observed_words;
            random.observe_draws([&](std::uint32_t word) { observed_words.push_back(word); });
            random.set_seed(test.at("seed").get<std::uint32_t>());

            const auto at = test.at("at");
            const Position position{at.at(0).get<int>(), at.at(1).get<int>(), at.at(2).get<int>()};
            const auto block_for = [&](const char *name) { return bds::FeatureBlock{intern(name)}; };
            bds::DecorationRegion region;
            // The vine walk needs BlockSource::getMaxHeight; the scene covers
            // Y 2..21, so the recorded run stops at the world height it saw.
            const int max_height = 256;
            if (feature == "tall_grass")
                bds::place_tall_grass(region, position, block_for("tall_grass"), random, environment);
            else if (feature == "flower")
                bds::place_flower(region, position, block_for("flower"), random, environment);
            else if (feature == "pumpkin")
                bds::place_pumpkin(region, position, block_for("pumpkin"), random, environment,
                                   block_for("grass"));
            else if (feature == "cactus")
                bds::place_cactus(region, position, block_for("cactus"), random, environment);
            else if (feature == "waterlily")
                bds::place_waterlily(region, position, block_for("waterlily"), random, environment);
            else if (feature == "reed")
                bds::place_reed(region, position, block_for("reed"), random, environment);
            else if (feature == "vine")
                bds::place_vine(region, position, block_for("vine"), random, environment, max_height);
            else
                throw std::runtime_error("unknown feature " + feature);

            check(next_dependency == recorded.size(),
                  feature + " consumed " + std::to_string(next_dependency) + " of " +
                      std::to_string(recorded.size()) + " native predicates");
            const auto expected_words = test.at("words").get<std::vector<std::uint32_t>>();
            check(observed_words == expected_words, feature + ": Random words differ");
            const auto expected_writes = test.at("writes");
            check(observed_writes.size() == expected_writes.size(),
                  feature + ": placements " + std::to_string(observed_writes.size()) + " vs " +
                      std::to_string(expected_writes.size()));
            for (std::size_t i = 0; i < expected_writes.size(); ++i) {
                const auto &expected = expected_writes.at(i);
                const auto &actual = observed_writes[i];
                // expected row is [x, y, z, block_name_index, flags]; the
                // observed block name carries no ":data" suffix.
                const std::string expected_entry = name_of(expected.at(3).get<int>());
                const auto expected_colon = expected_entry.rfind(':');
                const std::string expected_name = expected_entry.substr(0, expected_colon);
                const int expected_data = std::stoi(expected_entry.substr(expected_colon + 1));
                const bool same = actual.at(0) == expected.at(0) && actual.at(1) == expected.at(1) &&
                                  actual.at(2) == expected.at(2) &&
                                  actual.at(3) == expected_name &&
                                  actual.at(4).get<int>() == expected_data;
                check(same, feature + ": placement " + std::to_string(i) + " differs: got " +
                                actual.dump() + ", expected " + expected.dump());
            }
            ++cases;
            words += observed_words.size();
            writes += observed_writes.size();
            dependencies += recorded.size();
        }
        std::cout << cases << " native vegetation cases match: " << writes << " placements, "
                  << words << " Random words, " << dependencies << " dependency answers\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
