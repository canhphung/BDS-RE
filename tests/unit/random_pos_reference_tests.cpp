#include "bds/game/random_pos.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using namespace bds::game;
using Json = nlohmann::json;
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("expected original RandomPos fixture");
    std::ifstream file(argv[1]);
    const auto ref = Json::parse(file);
    if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b0965"
                                   "9590fdadc0900af41c4ad" ||
        ref.at("cases").size() != 96)
      throw std::runtime_error("unexpected RandomPos corpus");
    World world;
    world.configure_terrain({1, 123});
    for (int x = -4; x <= 4; ++x)
      for (int z = -4; z <= 4; ++z)
        world.ensure_chunk(x, z);
    for (const auto &p : ref.at("extra_blocks"))
      world.set_block({p[0].get<int>(), p[1].get<int>(), p[2].get<int>()},
                      p[3].get<BlockId>());
    WorldLighting lighting(world);
    for (const auto &test : ref.at("cases")) {
      Random random(test.at("seed").get<std::uint32_t>());
      std::vector<std::uint32_t> words;
      random.observe_draws([&](std::uint32_t w) { words.push_back(w); });
      const auto &p = test.at("position");
      const auto target = random_stroll_position(
          world, lighting, {p[0], p[1], p[2]}, random, test.at("xz"),
          test.at("y"), ref.value("sky_darken", 0u));
      if (target.has_value() != test.at("result") ||
          words != test.at("words").get<std::vector<std::uint32_t>>() ||
          (target && Json::array({target->x, target->y, target->z}) !=
                         test.at("wanted"))) {
        std::cerr << "At " << p << " seed " << test.at("seed") << " got "
                  << (target ? Json::array({target->x, target->y, target->z})
                             : Json(nullptr))
                  << " expected " << test << '\n';
        throw std::runtime_error("RandomPos differs from native function");
      }
    }
    std::cout
        << "96 original RandomPos destinations and own-Random streams match\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
