#include "bds/game/server.hpp"
#include "bds/game/world_tick.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
using namespace bds;
using namespace bds::game;
using Json = nlohmann::json;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
TickChunkPosition chunk(const Json &p) { return {p[0].get<int>(), p[1].get<int>()}; }
GamePacket drop_one_dirt() {
    ByteWriter out;
    out.varint(0); // normal transaction, container -> world interaction
    out.varint(2);
    out.varint(0);
    out.signed_varint(0);
    out.varint(0);
    item(out, {3, 1});
    item(out, {});
    out.varint(2);
    out.varint(0);
    out.varint(0);
    item(out, {});
    item(out, {3, 1});
    return packet(PacketId::InventoryTransaction, out.take());
}
void runtime_boundary_case(const Json &reference) {
    World world;
    world.configure_terrain({1, 123});
    for (int x = -2; x <= 4; ++x)
        for (int z = -2; z <= 3; ++z)
            world.ensure_chunk(x, z);
    Properties properties;
    AccessLists access(".");
    Server server(world, properties, access);
    // The recorded left item crosses z=16 on its first normalTick and stays
    // in the old owner through its second tick. The original's own Random
    // only adds <=0.02 throw spread, so this crossing is independent of it.
    for (const auto &p : reference.at("cases")[0].at("players")) {
        const auto id = server.allocate_entity_id();
        auth::Identity identity;
        identity.identity = "00000000-0000-4000-8000-00000000000" + std::to_string(id);
        identity.display_name = identity.identity;
        protocol::StartGame game;
        game.runtime_id = id;
        game.unique_id = static_cast<std::int64_t>(id);
        game.x = p.at("position")[0];
        game.y = p.at("position")[1];
        game.z = p.at("position")[2];
        server.prepare(identity, game);
        world.players().at("uuid:" + identity.identity).inventory[0] = {3, 1};
        ByteWriter initialized;
        initialized.varint64(id);
        server.handle(id, packet(PacketId::SetLocalPlayerAsInitialized, initialized.take()), 0);
    }
    for (std::uint64_t id = 1; id <= 2; ++id) {
        check(server.handle(id, drop_one_dirt(), 0).empty(), "drop transaction rejected");
        server.drain(id);
    }
    const auto left = std::find_if(server.items().begin(), server.items().end(),
                                    [](const ItemActor &a) { return a.pos.x < 16; });
    check(left != server.items().end(), "left boundary item absent");
    const auto id = left->runtime_id;
    for (int tick = 0; tick < 3; ++tick) {
        server.tick();
        const auto item = std::find_if(server.items().begin(), server.items().end(),
                                       [&](const ItemActor &a) { return a.runtime_id == id; });
        check(item != server.items().end(), "boundary item disappeared");
        const auto &frame = reference.at("cases")[static_cast<std::size_t>(tick)];
        const auto original = std::find_if(frame.at("items").begin(), frame.at("items").end(),
                                           [](const Json &a) { return a.at("position")[0].get<float>() < 16; });
        check(original != frame.at("items").end(), "original boundary item missing");
        check(item->owner_chunk == chunk(original->at("owner_chunk")),
              "deferred chunk ownership differs from original");
        check(item->age == original->at("age").get<int>() + 1,
              "ordinary item ticked more than once per level tick");
        check(item->pos.z > 16, "boundary scene did not cross into the next chunk");
        server.drain(1);
        server.drain(2);
    }
    server.leave(1);
    server.leave(2);
    const auto frozen = server.items();
    server.tick();
    check(server.items().size() == frozen.size(), "items vanished without players");
    for (std::size_t i = 0; i < frozen.size(); ++i)
        check(server.items()[i].age == frozen[i].age && server.items()[i].pos == frozen[i].pos,
              "ordinary item ticked without a player world");
}
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "expected chunk-order reference path");
        std::ifstream input(argv[1]);
        const auto reference = Json::parse(input);
        check(reference.at("binary_sha256") ==
                  "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        check(reference.at("cases").size() == 5, "missing original order frames");
        for (const auto &test : reference.at("cases")) {
            std::vector<std::uint64_t> players;
            std::map<std::uint64_t, TickChunkPosition> centers;
            std::map<TickChunkPosition, std::vector<std::uint64_t>> chunks;
            std::vector<TickChunkPosition> offsets;
            for (const auto &p : test.at("players")) {
                const auto id = p.at("actor").get<std::uint64_t>();
                players.push_back(id);
                centers[id] = {chunk_coordinate(static_cast<int>(std::floor(p.at("position")[0].get<float>()))),
                               chunk_coordinate(static_cast<int>(std::floor(p.at("position")[2].get<float>())))};
            }
            for (const auto &c : test.at("chunks"))
                chunks[chunk(c.at("position"))] = c.at("items").get<std::vector<std::uint64_t>>();
            for (const auto &p : test.at("offsets"))
                offsets.push_back(chunk(p));
            Json observed = Json::array();
            tick_player_worlds(players, offsets, {
                [&](std::uint64_t id) -> std::optional<TickChunkPosition> {
                    observed.push_back({{"event", "player_world"}, {"actor", id}});
                    return centers.at(id);
                },
                [&](TickChunkPosition p) { return chunks.contains(p); },
                [&](TickChunkPosition p) {
                    observed.push_back({{"event", "chunk_tick"}, {"chunk", Json::array({p.first, p.second})}});
                    for (const auto actor : chunks.at(p))
                        observed.push_back({{"event", "item_tick"}, {"actor", actor}});
                },
                [&](std::uint64_t id) {
                    observed.push_back({{"event", "player_tick"}, {"actor", id}});
                    observed.push_back({{"event", "nearby"}, {"actor", id}});
                }
            });
            check(observed == test.at("expected"), "callback order differs from original");
        }
        runtime_boundary_case(reference);
        std::cout << "Original per-player chunk/item order and deferred item ownership match\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
