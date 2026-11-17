#include "bds/game/mob_motion.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using namespace bds::game;
using Json = nlohmann::json;
Vec3 vec(const Json &p) {
  return {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
}
ActorMotion actor(const Json &p) {
  ActorMotion a;
  a.pos = vec(p.at("position"));
  a.motion = vec(p.at("motion"));
  const auto &b = p.at("box");
  a.bb = {{b[0], b[1], b[2]}, {b[3], b[4], b[5]}};
  a.on_ground = (p.at("ground").get<int>() & 1) != 0;
  a.horizontal_collision = (p.at("hcol").get<int>() & 1) != 0;
  a.vertical_collision = (p.at("vcol").get<int>() & 1) != 0;
  a.collision = (p.at("collision").get<int>() & 1) != 0;
  a.stuck_in_collider = (p.at("stuck").get<int>() & 1) != 0;
  a.no_exit = (p.at("no_exit").get<int>() & 1) != 0;
  a.last_penetration = p.at("penetration");
  a.height_offset = p.at("height_offset");
  a.max_step = p.value("max_step", 0.0f);
  a.slide_offset = p.value("slide", 0.0f);
  return a;
}
void compare(const ActorMotion &a, const Json &p, const Json &context) {
  const auto expected = actor(p);
  if (a.pos != expected.pos || a.motion != expected.motion ||
      a.bb.min != expected.bb.min || a.bb.max != expected.bb.max ||
      a.on_ground != expected.on_ground ||
      a.horizontal_collision != expected.horizontal_collision ||
      a.vertical_collision != expected.vertical_collision ||
      a.collision != expected.collision ||
      a.stuck_in_collider != expected.stuck_in_collider ||
      a.last_penetration != expected.last_penetration ||
      a.slide_offset != expected.slide_offset) {
    std::cerr << "Context " << context << "\ngot pos "
              << Json::array({a.pos.x, a.pos.y, a.pos.z}) << " motion "
              << Json::array({a.motion.x, a.motion.y, a.motion.z})
              << " expected " << p << '\n';
    throw std::runtime_error("native mob physics differs");
  }
}
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("expected original mob-motion fixture");
    std::ifstream in(argv[1]);
    const auto ref = Json::parse(in);
    if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b0965"
                                   "9590fdadc0900af41c4ad" ||
        ref.at("scenes").size() != 2)
      throw std::runtime_error("unexpected native motion corpus");
    unsigned count = 0;
    for (const auto &scene : ref.at("scenes")) {
      World world;
      world.configure_terrain({1, 123});
      for (int x = -4; x <= 4; ++x)
        for (int z = -4; z <= 4; ++z)
          world.ensure_chunk(x, z);
      for (const auto &p : scene.at("initial_blocks"))
        world.set_block({p[0].get<int>(), p[1].get<int>(), p[2].get<int>()},
                        p[3].get<BlockId>());
      for (const auto &test : scene.at("cases")) {
        if (test.value("kind", std::string{}) == "move") {
          auto a = actor(test.at("before"));
          move(a, world, vec(test.at("delta")));
          compare(a, test.at("after"),
                  {{"stage", "direct move"},
                   {"before", test.at("before")},
                   {"delta", test.at("delta")}});
          ++count;
          continue;
        }
        if (test.value("kind", std::string{}) == "push") {
          auto a = actor(test.at("before")), b = actor(test.at("other_before"));
          push_actors(a, b, test.at("one_way").get<int>() != 0,
                      test.at("before").value("pushthrough", 0.0f));
          compare(a, test.at("after"),
                  {{"stage", "push"}, {"tick", test.at("tick")}});
          compare(b, test.at("other_after"),
                  {{"stage", "pushed"}, {"tick", test.at("tick")}});
          ++count;
          continue;
        }
        auto a = actor(test.at("before"));
        const auto &r = test.at("relative");
        travel_mob(a, world, vec(test.at("input")), test.at("before").at("yaw"),
                   r[3].get<float>(), test.at("before").at("air_speed"),
                   test.at("before").at("friction_modifier"));
        compare(a, test.at("after"),
                {{"stage", "travel"},
                 {"actor", test.at("actor")},
                 {"tick", test.at("tick")}});
        for (const auto &m : test.at("moves")) {
          auto b = actor(m.at("before"));
          move(b, world, vec(m.at("delta")));
          compare(b, m.at("after"),
                  {{"stage", "move"},
                   {"actor", test.at("actor")},
                   {"tick", test.at("tick")}});
        }
        ++count;
      }
    }
    std::cout << count << " native mob travel/move cases match exactly\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
