#include "bds/world/grass_ticks.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>
using Json = nlohmann::json;
using namespace bds;
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
GrassTickBlockKind kind(const std::string &name) {
    if (name == "air") return GrassTickBlockKind::Air;
    if (name == "dirt") return GrassTickBlockKind::Dirt;
    if (name == "grass") return GrassTickBlockKind::Grass;
    if (name == "grass_path") return GrassTickBlockKind::GrassPath;
    check(name == "other", "unrecognized original block kind");
    return GrassTickBlockKind::Other;
}
class ReplaySource final : public GrassTickSource {
  public:
    explicit ReplaySource(const Json &calls) : calls_(calls) {}
    unsigned brightness(World::Position p) override {
        return consume("brightness", p).at("value").get<unsigned>();
    }
    GrassTickBlock block(World::Position p) override {
        const auto &value = consume("block", p).at("value");
        return {kind(value.at("kind").get<std::string>()), value.at("solid").get<bool>()};
    }
    GrassTickMaterial material(World::Position p) override {
        const auto &value = consume("material", p).at("value");
        return {value.at("translucency").get<float>(), value.at("liquid").get<bool>()};
    }
    int dirt_variant(World::Position p) override {
        return consume("variant", p).at("value").get<int>();
    }
    void set_block(World::Position p, GrassTickBlockKind block_kind, int flags) override {
        const auto &call = consume("set_block", p);
        check(kind(call.at("value").get<std::string>()) == block_kind,
              "GrassBlock mutation differs from original");
        check(call.at("flags") == flags, "GrassBlock mutation flags differ from original");
    }
    bool complete() const { return index_ == calls_.size(); }

  private:
    const Json &consume(const char *operation, World::Position p) {
        check(index_ < calls_.size(), "unexpected GrassBlock dependency call");
        const auto &call = calls_[index_++];
        check(call.at("kind") == operation, "GrassBlock dependency order differs from original");
        auto [x, y, z] = p;
        check(call.at("position") == Json::array({x, y, z}),
              "GrassBlock queried different coordinates from original");
        return call;
    }
    const Json &calls_;
    std::size_t index_ = 0;
};
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "expected grass oracle path");
        std::ifstream input(argv[1]);
        const auto fixture = Json::parse(input);
        check(fixture.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        check(fixture.at("min_brightness") == 4 && fixture.at("max_brightness") == 15,
              "original grass brightness constants changed");
        check(fixture.at("cases").size() == 160, "missing original GrassBlock cases");
        for (const auto &test : fixture.at("cases")) {
            ReplaySource source(test.at("calls"));
            Random random(test.at("seed").get<std::uint32_t>());
            std::vector<std::uint32_t> words;
            random.observe_draws([&](std::uint32_t word) { words.push_back(word); });
            const auto &p = test.at("position");
            tick_grass(source, {p[0].get<int>(), p[1].get<int>(), p[2].get<int>()}, random);
            check(source.complete(), "GrassBlock omitted original dependency calls");
            check(words == test.at("words").get<std::vector<std::uint32_t>>(),
                  "GrassBlock consumed different RNG words from original");
        }
        std::cout << "160 original day/night GrassBlock dependency/mutation/RNG cases match\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
