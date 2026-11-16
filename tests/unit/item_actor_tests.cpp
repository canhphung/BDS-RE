// ItemActor physics against the original: per-tick state of a dropped item
// recorded under gdb (tests/reference/item_actor_bds_1.6.0.15.json).
#include "bds/game/item_actor.hpp"
#include "bds/game/protocol.hpp"
#include <nlohmann/json.hpp>
#include <bit>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace bds;
using namespace bds::game;
namespace {
float f(const nlohmann::json &v) { return std::bit_cast<float>(v.get<std::uint32_t>()); }
Vec3 vec(const nlohmann::json &v) { return {f(v[0]), f(v[1]), f(v[2])}; }
std::string show(Vec3 v) {
    char text[96];
    std::snprintf(text, sizeof text, "%.9g,%.9g,%.9g", v.x, v.y, v.z);
    return text;
}
void same(Vec3 a, Vec3 b, const std::string &what) {
    if (std::bit_cast<std::uint32_t>(a.x) != std::bit_cast<std::uint32_t>(b.x) ||
        std::bit_cast<std::uint32_t>(a.y) != std::bit_cast<std::uint32_t>(b.y) ||
        std::bit_cast<std::uint32_t>(a.z) != std::bit_cast<std::uint32_t>(b.z))
        throw std::runtime_error(what + ": got " + show(a) + " expected " + show(b));
}
ItemActor load(const nlohmann::json &t) {
    ItemActor a;
    a.item = {3, 1};
    a.pos = a.pos_prev = vec(t.at("pos"));
    a.motion = vec(t.at("motion"));
    const auto &box = t.at("aabb");
    a.bb = {{f(box[0]), f(box[1]), f(box[2])}, {f(box[3]), f(box[4]), f(box[5])}};
    a.on_ground = t.at("on_ground").get<bool>();
    a.pickup_delay = t.at("pickup_delay").get<int>();
    a.age = t.at("age").get<int>();
    return a;
}

// Two drops merge at their first tick, then the hole is refilled over the
// survivor: clip-collide overlap push, stuck-in-collider flag, pushOutOfBlocks.
std::size_t merge_scenario(const nlohmann::json &ref) {
    World world;
    const auto &stone = ref.at("world").at("stone");
    for (int x = stone[0][0]; x <= stone[1][0].get<int>(); ++x)
        for (int y = stone[0][1]; y <= stone[1][1].get<int>(); ++y)
            for (int z = stone[0][2]; z <= stone[1][2].get<int>(); ++z)
                world.set_block({x, y, z}, 1);
    for (const auto &p : ref.at("world").at("air"))
        world.set_block({p[0].get<int>(), p[1].get<int>(), p[2].get<int>()}, 0);
    const auto &ticks = ref.at("ticks");
    std::vector<ItemActor> items{load(ref.at("absorbed")), load(ticks[0])};
    const auto refill_at = ref.at("refill_before_age").get<int>();
    for (std::size_t i = 1; i < ticks.size(); ++i) {
        if (items[1].age == refill_at)
            for (const auto &p : ref.at("refill"))
                world.set_block({p[0].get<int>(), p[1].get<int>(), p[2].get<int>()}, 1);
        for (auto &a : items)
            if (!a.removed)
                normal_tick(a, world, [&](ItemActor &self) {
                    const auto area = merge_area(self);
                    for (auto &other : items)
                        if (!other.removed && other.bb.intersects(area))
                            merge(self, other);
                });
        const auto &t = ticks[i];
        const auto &a = items[1];
        const auto where = "merge scenario tick " + std::to_string(i);
        if (!items[0].removed || a.item.count != 2)
            throw std::runtime_error(where + ": merge");
        if (a.age != t.at("age").get<int>())
            throw std::runtime_error(where + ": age");
        same(a.pos, vec(t.at("pos")), where + " position");
        same(a.motion, vec(t.at("motion")), where + " motion");
        if (a.on_ground != t.at("on_ground").get<bool>() || a.stuck_in_collider != t.at("stuck").get<bool>() ||
            std::bit_cast<std::uint32_t>(a.last_penetration) != t.at("penetration").get<std::uint32_t>())
            throw std::runtime_error(where + ": on_ground/stuck/penetration");
    }
    return ticks.size() - 1;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("usage: item_actor_tests <item_actor.json> <item_actor_merge.json>");
        std::ifstream input(argv[1]);
        const auto ref = nlohmann::json::parse(input);
        World world;
        const auto &stone = ref.at("world").at("stone");
        for (int x = stone[0][0]; x <= stone[1][0].get<int>(); ++x)
            for (int y = stone[0][1]; y <= stone[1][1].get<int>(); ++y)
                for (int z = stone[0][2]; z <= stone[1][2].get<int>(); ++z)
                    world.set_block({x, y, z}, 1);
        for (const auto &p : ref.at("world").at("air"))
            world.set_block({p[0].get<int>(), p[1].get<int>(), p[2].get<int>()}, 0);
        const auto &ticks = ref.at("ticks");
        ItemActor a;
        a.item = {3, 1};
        a.pos = vec(ticks[0].at("pos"));
        a.motion = vec(ticks[0].at("motion"));
        const auto &box = ticks[0].at("aabb");
        a.bb = {{f(box[0]), f(box[1]), f(box[2])}, {f(box[3]), f(box[4]), f(box[5])}};
        a.on_ground = ticks[0].at("on_ground").get<bool>();
        a.pickup_delay = ticks[0].at("pickup_delay").get<int>();
        // Actor::setPos reproduces the original box from the position.
        ItemActor placed;
        placed.set_pos(a.pos);
        same(placed.bb.min, a.bb.min, "setPos box min");
        same(placed.bb.max, a.bb.max, "setPos box max");
        std::vector<std::string> deltas;
        int motions = 0;
        for (std::size_t i = 1; i < ticks.size(); ++i) {
            normal_tick(a, world, [](ItemActor &) {});
            if (i == 1) {
                a.runtime_id = 8;
                a.sent_pos = a.pos; // AddItemActor after the first tick
                a.sent_on_ground = a.on_ground;
            } else
                for (const auto &q : replicate(a))
                    if (q.header.id == 0x28)
                        ++motions;
                    else if (q.header.id == 0x6f) {
                        std::string h;
                        for (auto b : q.body) {
                            h += "0123456789abcdef"[b >> 4];
                            h += "0123456789abcdef"[b & 15];
                        }
                        deltas.push_back(h);
                    }
            const auto &t = ticks[i];
            const auto where = "tick " + std::to_string(i);
            if (a.age != t.at("age").get<int>() || a.pickup_delay != t.at("pickup_delay").get<int>())
                throw std::runtime_error(where + ": age/pickup delay");
            same(a.pos, vec(t.at("pos")), where + " position");
            same(a.motion, vec(t.at("motion")), where + " motion");
            if (a.on_ground != t.at("on_ground").get<bool>())
                throw std::runtime_error(where + ": on_ground");
        }
        // Player::take into an empty inventory: the original InventoryTransaction bytes.
        const auto body = pickup_transaction({{0, {}, {3, 1}}}, {3, 1}, {});
        std::string hex;
        for (auto b : body) {
            static const char *digits = "0123456789abcdef";
            hex += digits[b >> 4];
            hex += digits[b & 15];
        }
        if (hex != ref.at("pickup").at("inventory_transaction").get<std::string>())
            throw std::runtime_error("pickup InventoryTransaction bytes " + hex);
        // AddItemActor and SetActorData for the first tick, and the MoveActorDelta
        // sequence of the replayed ticks, byte for byte with the original
        // (runtime id 8, unique id -4294967288 in that session).
        const auto to_hex = [](const std::vector<std::uint8_t> &bytes) {
            std::string out;
            for (auto b : bytes) {
                out += "0123456789abcdef"[b >> 4];
                out += "0123456789abcdef"[b & 15];
            }
            return out;
        };
        const auto &packets = ref.at("packets");
        ItemActor first;
        first.runtime_id = 8;
        first.unique_id = -4294967288LL;
        first.item = {3, 1};
        first.pos = first.sent_pos = vec(ticks[1].at("pos"));
        first.motion = vec(ticks[1].at("motion"));
        if (to_hex(add_item_actor(first)) != packets.at("add_item_actor").get<std::string>())
            throw std::runtime_error("AddItemActor bytes " + to_hex(add_item_actor(first)));
        if (to_hex(item_actor_data(8)) != packets.at("set_actor_data").get<std::string>())
            throw std::runtime_error("SetActorData bytes");
        if (deltas != packets.at("move_actor_delta").get<std::vector<std::string>>())
            throw std::runtime_error("MoveActorDelta sequence differs");
        if (motions != 0) // the original sent no SetActorMotion for this item
            throw std::runtime_error("unexpected SetActorMotion");
        std::ifstream merge_input(argv[2]);
        const auto merged = merge_scenario(nlohmann::json::parse(merge_input));
        std::cout << merged << " merge/push-out ticks and " << ticks.size() - 1 << " ItemActor ticks, AddItemActor/SetActorData bytes, the MoveActorDelta "
                     "sequence and the pickup InventoryTransaction match the original\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
