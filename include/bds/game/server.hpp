#pragma once
#include "bds/game/protocol.hpp"
#include "bds/server/access.hpp"
#include "bds/server/properties.hpp"
#include <deque>
#include <map>
namespace bds::game {
class Server {
  public:
    Server(World &world, const Properties &properties, AccessLists &access)
        : world_(world), properties_(properties), access_(access) {}
    std::vector<GamePacket> prepare(const auth::Identity &identity, protocol::StartGame &game);
    std::vector<GamePacket> handle(std::uint64_t runtime, const GamePacket &packet,
                                   std::uint64_t now);
    std::vector<GamePacket> drain(std::uint64_t runtime);
    std::optional<std::pair<int, int>> center(std::uint64_t runtime) const;
    void leave(std::uint64_t runtime);

  private:
    struct Session {
        auth::Identity identity;
        protocol::StartGame game;
        std::string key;
        bool active = false;
        std::deque<GamePacket> queue;
        bool overflow = false;
        std::uint64_t break_time = 0;
        std::optional<World::Position> breaking;
    };
    World &world_;
    const Properties &properties_;
    AccessLists &access_;
    std::map<std::uint64_t, Session> sessions_;
    void enqueue(std::uint64_t runtime, GamePacket packet);
    void broadcast(GamePacket packet, std::uint64_t exclude = 0);
    bool collides(const PlayerData &p) const;
    bool reach(const PlayerData &p, World::Position block) const;
    std::vector<GamePacket> correct(Session &s);
    std::vector<GamePacket> reject_item_use(const Session &s, const Transaction &t) const;
    std::vector<GamePacket> use(Session &s, const Transaction &t, std::uint64_t now);
    std::vector<GamePacket> destroy(Session &s, World::Position block, std::uint64_t now);
};
} // namespace bds::game
