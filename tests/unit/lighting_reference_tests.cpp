#include "bds/game/server.hpp"
#include "bds/world/chunk_ticks.hpp"
#include "bds/world/lighting.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
using namespace bds;
using Json = nlohmann::json;
void runtime_block_tick_case() {
  World world;
  world.configure_terrain({1, 123});
  for (int x = -1; x <= 1; ++x)
    for (int z = -1; z <= 1; ++z)
      world.ensure_chunk(x, z);
  for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z)
      world.set_block({x, 4, z}, 1);
  Properties properties;
  AccessLists access(".");
  game::Server server(world, properties, access);
  auth::Identity identity;
  identity.identity = "00000000-0000-4000-8000-000000000001";
  identity.display_name = "A";
  protocol::StartGame game;
  game.runtime_id = 1;
  game.unique_id = 1;
  game.x = 0.5f;
  game.y = 6.62f;
  game.z = 0.5f;
  server.prepare(identity, game);
  server.handle(
      1, game::packet(game::PacketId::SetLocalPlayerAsInitialized, {1}), 0);
  server.drain(1);
  unsigned updates = 0;
  for (int tick = 0; tick < 512; ++tick) {
    server.tick();
    std::vector<protocol::GamePacket> packets;
    for (;;) {
      auto batch = server.drain(1);
      if (batch.empty())
        break;
      packets.insert(packets.end(), batch.begin(), batch.end());
    }
    for (const auto &packet : packets)
      if (packet.header.id ==
          static_cast<unsigned>(game::PacketId::UpdateBlock)) {
        // A covered source may only decay into dirt here, with original
        // flags 3.
        protocol::ByteReader reader(packet.body);
        const int x = reader.signed_varint(),
                  y = static_cast<int>(reader.varint()),
                  z = reader.signed_varint();
        const auto runtime = reader.varint();
        // Wandering sheep can graze elsewhere in the loaded scene. Only
        // compare mutations of the explicitly covered 16x16 fixture here.
        if (x < 0 || x >= 16 || z < 0 || z >= 16 || y != 3)
          continue;
        ++updates;
        if (world.block({x, y, z}) != 3 || y != 3 || reader.varint() != 3 ||
            reader.varint() != 0 ||
            runtime != protocol::runtime_id("minecraft:dirt", 0).value())
          throw std::runtime_error("runtime random tick update did not match "
                                   "authoritative dirt mutation");
      }
  }
  unsigned dirt = 0;
  for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z)
      if (world.block({x, 3, z}) == 3)
        ++dirt;
  if (!dirt || updates != dirt)
    throw std::runtime_error(
        "Server did not wire chunk grass ticks to world and updates");
  std::cout << "Runtime chunk scheduler: " << dirt
            << " covered grass mutations and updates\n";
}
int main(int argc, char **argv) {
  try {
    if (argc < 2)
      throw std::runtime_error("expected original lighting fixtures");
    for (int arg = 1; arg < argc; ++arg) {
      std::ifstream file(argv[arg]);
      const auto ref = Json::parse(file);
      if (ref.at("binary_sha256") !=
          "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad")
        throw std::runtime_error("unexpected original binary");
      World world;
      world.configure_terrain({1, 123});
      for (int x = -3; x <= 3; ++x)
        for (int z = -3; z <= 3; ++z)
          world.ensure_chunk(x, z);
      WorldLighting lighting(world);
      for (const auto &line : ref.at("scene_setup_commands")) {
        std::istringstream in(line.get<std::string>());
        std::string command, block;
        in >> command;
        if (command != "fill" && command != "setblock")
          continue;
        int x1, y1, z1, x2, y2, z2;
        in >> x1 >> y1 >> z1;
        if (command == "fill")
          in >> x2 >> y2 >> z2;
        else {
          x2 = x1;
          y2 = y1;
          z2 = z1;
        }
        in >> block;
        if (command == "setblock")
          (void)lighting.pair(
              {x1, y1, z1}); // warm cache before geometry mutation
        const BlockId id = block == "air"     ? 0
                           : block == "stone" ? 1
                           : block == "grass" ? 2
                           : block == "dirt"  ? 3
                                              : 4;
        for (int x = x1; x <= x2; ++x)
          for (int y = y1; y <= y2; ++y)
            for (int z = z1; z <= z2; ++z)
              world.set_block({x, y, z}, id);
      }
      for (const auto &test : ref.at("queries")) {
        const auto &p = test.at("position");
        World::Position at{p[0].get<int>(), p[1].get<int>(), p[2].get<int>()};
        const auto pair = lighting.pair(at);
        const auto darken = ref.at("sky_darken").get<unsigned>();
        if (Json(pair) != test.at("pair") ||
            lighting.raw(at, darken, true, false) != test.at("raw")[0] ||
            lighting.raw(at, darken) != test.at("raw")[1]) {
          std::cerr << "At " << p << " got pair " << Json(pair) << " raw "
                    << lighting.raw(at, darken, true, false) << ","
                    << lighting.raw(at, darken) << " expected " << test << '\n';
          throw std::runtime_error("lighting differs from original");
        }
      }
      for (const auto &test : ref.at("clear_sky_time_cases"))
        if (clear_sky_darken(test[0].get<int>()) != test[1].get<unsigned>()) {
          std::cerr << "Time " << test << " got "
                    << clear_sky_darken(test[0].get<int>()) << '\n';
          throw std::runtime_error("sky darkening differs from original");
        }
      for (const auto &row : ref.at("registry")) {
        const auto &p = tick_block_properties(row.at("id").get<BlockId>());
        if (p.opacity != row.at("opacity") ||
            p.emission != row.at("emission") || p.solid != row.at("solid") ||
            p.propagate != row.at("propagate") ||
            p.tickable != row.at("tickable") ||
            p.translucency != row.at("translucency") ||
            p.liquid != row.at("liquid"))
          throw std::runtime_error(
              "block properties differ from original registry");
      }
      for (const auto &p : ref.at("initial_ground"))
        world.set_block({p[0].get<int>(), p[1].get<int>(), p[2].get<int>()},
                        p[3].get<BlockId>());
      BlockTickRandom coordinates(
          ref.at("block_ticks")[0].at("lcg_before").get<std::uint32_t>());
      for (const auto &test : ref.at("block_ticks")) {
        Random random(test.at("seed").get<std::uint32_t>());
        std::vector<std::uint32_t> words;
        random.observe_draws(
            [&](std::uint32_t word) { words.push_back(word); });
        Json mutations = Json::array();
        if (coordinates.state() != test.at("lcg_before"))
          throw std::runtime_error("LCG before differs");
        tick_clear_chunk(world, lighting, 0, 0, random, coordinates,
                         ref.at("sky_darken").get<unsigned>(),
                         [&](World::Position p, BlockId id, int flags) {
                           const auto [x, y, z] = p;
                           mutations.push_back(
                               {{"position", Json::array({x, y, z})},
                                {"id", id},
                                {"flags", flags}});
                         });
        if (words != test.at("words").get<std::vector<std::uint32_t>>() ||
            coordinates.state() != test.at("lcg_after") ||
            mutations != test.at("mutations")) {
          std::cerr << "Seed " << test.at("seed") << " got words "
                    << Json(words) << " mutations " << mutations << " expected "
                    << test << '\n';
          throw std::runtime_error(
              "integrated tickBlocks differs from original");
        }
      }
      std::cout << argv[arg] << ": " << ref.at("queries").size()
                << " original light points match\n";
    }
    runtime_block_tick_case();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
