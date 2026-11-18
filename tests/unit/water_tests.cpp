#include "bds/game/mob_motion.hpp"
#include "bds/game/server.hpp"
#include "bds/network/chunk.hpp"
#include "bds/network/level_packets.hpp"
#include "bds/world/chunk_ticks.hpp"
#include "bds/world/lighting.hpp"
#include "bds/world/store.hpp"
#include "bds/world/world.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace bds;
using Json = nlohmann::json;
static void require(bool b, const std::string &message) {
  if (!b)
    throw std::runtime_error(message);
}
static World::Position position(const Json &j) { return {j[0], j[1], j[2]}; }
static void runtime_case() {
  World w;
  w.configure_terrain({1, 123});
  for (int x = -2; x <= 2; ++x)
    for (int z = -2; z <= 2; ++z)
      w.ensure_chunk(x, z);
  Properties props;
  AccessLists access(".");
  game::Server server(w, props, access);
  auth::Identity identity;
  identity.identity = "00000000-0000-4000-8000-000000000001";
  identity.display_name = "WaterTest";
  protocol::StartGame start;
  start.runtime_id = 1;
  start.unique_id = 1;
  start.x = 15.5f;
  start.y = 6.62f;
  start.z = 8.5f;
  server.prepare(identity, start);
  server.handle(
      1, game::packet(game::PacketId::SetLocalPlayerAsInitialized, {1}), 0);
  while (!server.drain(1).empty()) {
  }
  w.set_block({15, 4, 8}, water_block(0));
  unsigned updates = 0;
  std::map<World::Position, std::uint32_t> last;
  for (int tick = 0; tick < 30; ++tick) {
    w.tick();
    server.tick();
    for (;;) {
      auto packets = server.drain(1);
      if (packets.empty())
        break;
      for (const auto &packet : packets)
        if (packet.header.id == unsigned(game::PacketId::UpdateBlock)) {
          protocol::ByteReader r(packet.body);
          const int x = r.signed_varint(), y = int(r.varint()),
                    z = r.signed_varint();
          const auto runtime = r.varint(), flags = r.varint();
          require((flags == 2 || flags == 3) && r.varint() == 0,
                  "water packet flags/layer");
          r.finish();
          last[{x, y, z}] = runtime;
          ++updates;
        }
    }
  }
  require(updates > 20 && is_water(w.block({16, 4, 8})),
          "runtime did not stream flowing water across columns");
  for (const auto &[p, runtime] : last) {
    const auto id = w.block(p);
    if (!is_water(id))
      continue; // another actor may mutate a grass block
    require(runtime == protocol::runtime_id("minecraft:water", water_depth(id))
                           .value() ||
                runtime == protocol::runtime_id("minecraft:flowing_water",
                                                water_depth(id))
                               .value(),
            "client received stale water depth");
  }
  const auto chunk = protocol::decode_full_chunk(
      protocol::full_chunk_body(protocol::network_chunk(w, 1, 0)));
  const auto id = w.block({16, 4, 8});
  const auto &palette = protocol::internal_block_palette().at(id);
  require(chunk.sections.at(0).at(0).block(0, 4, 8) ==
              protocol::runtime_id(palette.name, palette.data).value(),
          "rejoining client chunk lost water state");
  WorldLighting lighting(w);
  require(lighting.pair({15, 4, 8})[0] < 15,
          "water failed to attenuate skylight");
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "expected native water fixture");
    std::ifstream input(argv[1]);
    const auto ref = Json::parse(input);
    require(
        ref.at("binary_sha256") ==
            "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
        "unexpected native binary");
    unsigned cases = 0;
    for (const auto &c : ref.at("cases")) {
      World w;
      std::map<World::Position, BlockId> expected;
      for (const auto &r : c.at("initial")) {
        w.set_block(position(r), r[3].get<BlockId>(), 0);
        expected[position(r)] = r[3].get<BlockId>();
      }
      const auto at = position(c.at("at"));
      const auto spread = w.water_spread(at);
      for (unsigned i = 0; i < 4; ++i)
        require(spread[i] == c.at("spread")[i].get<bool>(),
                c.at("name").get<std::string>() + " spread");
      if (c.contains("flow_half")) {
        const auto flow = w.water_flow(at);
        for (int i = 0; i < 3; ++i)
          require(flow[i] * .5f == c.at("flow_half")[i].get<float>(),
                  c.at("name").get<std::string>() + " flow vector");
        game::ActorMotion actor;
        const auto &b = c.at("box"), &m = c.at("motion_before");
        actor.motion = {m[0], m[1], m[2]};
        const bool found = game::apply_water_current(
            actor, w, {{b[0], b[1], b[2]}, {b[3], b[4], b[5]}});
        const auto &after = c.at("motion_after");
        require(found == c.at("in_water").get<bool>() &&
                    actor.motion == game::Vec3{after[0], after[1], after[2]},
                c.at("name").get<std::string>() + " native current motion");
      }
      w.tick_water(at);
      for (const auto &r : c.at("after"))
        expected[position(r)] = r[3].get<BlockId>();
      for (const auto &[p, id] : expected)
        require(w.block(p) == id, c.at("name").get<std::string>() +
                                      " mismatch at " +
                                      std::to_string(std::get<0>(p)) + "," +
                                      std::to_string(std::get<1>(p)) + "," +
                                      std::to_string(std::get<2>(p)) +
                                      ": got " + std::to_string(w.block(p)) +
                                      " expected " + std::to_string(id));
      w.for_each_block([&](auto p, auto id) {
        require(expected.contains(p) && expected.at(p) == id,
                c.at("name").get<std::string>() + " extra mutation");
      });
      ++cases;
    }
    if (ref.contains("queue")) {
      World native;
      native.configure_terrain({1, 123});
      for (int x = 0; x < 5; ++x)
        for (int z = -2; z < 3; ++z)
          native.ensure_chunk(x, z);
      auto restored = native;
      for (auto *world : {&native, &restored})
        for (const auto &r : ref.at("queue").at("initial"))
          world->set_block(position(r), r[3].get<BlockId>(), 0);
      std::map<World::Position, BlockId> expected;
      for (const auto &r : ref.at("queue").at("initial"))
        expected[position(r)] = r[3].get<BlockId>();
      for (const auto &step : ref.at("queue").at("steps")) {
        if (step.at("tick") == 37) {
          restored = WorldStore::decode(WorldStore::encode(native),
                                        WorldStore::Limits{});
          for (int x = 0; x < 5; ++x)
            for (int z = -2; z < 3; ++z)
              restored.ensure_chunk(x, z);
        }
        for (auto *world : {&native, &restored}) {
          for (const auto &r : step.at("edits"))
            world->set_block(position(r), r[3].get<BlockId>());
          world->tick();
          world->tick_water_chunk(2, 0);
        }
        for (const auto &r : step.at("after"))
          expected[position(r)] = r[3].get<BlockId>();
        for (const auto &[p, id] : expected) {
          const std::string where = "native queue tick " +
                                    std::to_string(step.at("tick").get<int>()) +
                                    " at " + std::to_string(std::get<0>(p)) +
                                    "," + std::to_string(std::get<1>(p)) + "," +
                                    std::to_string(std::get<2>(p));
          require(native.block(p) == id, where + ": got " +
                                             std::to_string(native.block(p)) +
                                             " expected " + std::to_string(id));
          require(restored.block(p) == id, where + " after reload");
        }
      }
    }
    // Delay, source withdrawal, cross-column spread and save/reload resume.
    World w;
    w.configure_terrain({1, 123});
    for (int x = -1; x < 3; ++x)
      for (int z = -1; z < 2; ++z)
        w.ensure_chunk(x, z);
    w.set_block({15, 4, 8}, water_block(0));
    for (int t = 0; t < 4; ++t) {
      w.tick();
      w.tick_water_chunk(0, 0);
    }
    require(w.block({16, 4, 8}) == 0, "water ticked before its deadline");
    w.tick();
    w.tick_water_chunk(0, 0);
    require(water_depth(w.block({16, 4, 8})) == 1,
            "water did not cross a chunk boundary");
    auto restored =
        WorldStore::decode(WorldStore::encode(w), WorldStore::Limits{});
    for (int x = -1; x < 3; ++x)
      for (int z = -1; z < 2; ++z)
        restored.ensure_chunk(x, z);
    w.set_block({15, 4, 8}, 0);
    restored.set_block({15, 4, 8}, 0);
    for (int t = 0; t < 120; ++t) {
      for (auto *world : {&w, &restored}) {
        world->tick();
        world->tick_water_chunk(0, 0);
        world->tick_water_chunk(1, 0);
      }
    }
    for (int x = 0; x < 32; ++x)
      for (int z = 0; z < 16; ++z) {
        require(!is_water(w.block({x, 4, z})), "orphan water did not drain");
        require(restored.block({x, 4, z}) == w.block({x, 4, z}),
                "save/reload water mismatch");
      }
    for (unsigned depth = 0; depth < 16; ++depth) {
      for (bool dynamic : {false, true}) {
        const auto id = water_block(depth, dynamic);
        const auto &palette = protocol::internal_block_palette().at(id);
        require(palette.name == (dynamic ? "minecraft:flowing_water"
                                         : "minecraft:water") &&
                    palette.data == depth,
                "wrong water wire state");
        require(!block_has_collision(id) &&
                    tick_block_properties(id).opacity == 2,
                "water properties");
        require(tick_block_properties(id).tickable == dynamic,
                "water random tick registry");
      }
    }
    // Geometry/light changes invalidate caches; a depth-only change does
    // not alter opacity or the direct-sky height map.
    World light;
    light.configure_terrain({1, 123});
    light.ensure_chunk(0, 0);
    WorldLighting lighting(light);
    light.set_block({8, 4, 8}, water_block(0));
    const auto revision = light.chunk(0, 0)->light_revision();
    require(lighting.pair({8, 4, 8})[0] == 13,
            "native water opacity not applied to skylight");
    light.set_block({8, 4, 8}, water_block(6));
    require(light.chunk(0, 0)->light_revision() == revision &&
                lighting.pair({8, 4, 8})[0] == 13,
            "depth changed light geometry");
    light.set_block({8, 4, 8}, 0);
    require(lighting.pair({8, 4, 8})[0] == 15,
            "drained water left stale light");
    // A random block tick must run dynamic water independently of its
    // scheduled deadline. Use a known sampled cell in an otherwise dry scene.
    for (unsigned seed = 1; seed < 1000; ++seed) {
      BlockTickRandom predict(seed);
      predict.next_column();
      const auto at = predict.next_position(16);
      if (at[1] <= 4 || at[1] >= 15)
        continue;
      World random_world;
      random_world.configure_terrain({1, 123});
      random_world.ensure_chunk(0, 0);
      random_world.set_block({at[0], at[1], at[2]}, water_block(0));
      WorldLighting random_light(random_world);
      Random random(123);
      BlockTickRandom coordinates(seed);
      tick_clear_chunk(random_world, random_light, 0, 0, random, coordinates,
                       0);
      require(is_water(random_world.block({at[0], at[1] - 1, at[2]})),
              "random tick omitted dynamic water");
      break;
    }
    runtime_case();
    std::cout << cases
              << " native flow/current cases, 120 native queue ticks, reload "
                 "and runtime stream passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
