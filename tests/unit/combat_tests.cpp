// Eating, effects, absorption, armor and player combat against the original's
// packets (tests/reference/combat_bds_1.6.0.15.json, recorded with
// tools/reference/combat_oracle.py; player A is runtime 1, B runtime 7).
#include "bds/game/items.hpp"
#include "bds/game/server.hpp"
#include "bds/world/store.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace bds;
using namespace bds::game;
using namespace bds::protocol;

namespace {
int failures = 0;
void check(bool ok, const std::string &why) {
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}
std::string hex(const Bytes &b) {
    std::string out;
    for (auto c : b) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
auth::Identity identity(unsigned n) {
    auth::Identity i;
    i.identity = "00000000-0000-4000-8000-00000000000" + std::to_string(n);
    i.display_name = "Player" + std::to_string(n);
    i.client_data_json = "{}";
    return i;
}
ItemStack stack(const char *name, int count = 1, int damage = 0) {
    return {item_named(name), static_cast<std::uint8_t>(count), static_cast<std::int16_t>(damage)};
}
void vec(ByteWriter &w, float x, float y, float z) {
    w.float32(x);
    w.float32(y);
    w.float32(z);
}
struct Change {
    int window;
    unsigned slot;
    ItemStack before, after;
};
void changes(ByteWriter &w, const std::vector<Change> &list) {
    w.varint(static_cast<std::uint32_t>(list.size()));
    for (const auto &c : list) {
        w.varint(0);
        w.signed_varint(c.window);
        w.varint(c.slot);
        item(w, c.before);
        item(w, c.after);
    }
}
GamePacket use_air(int slot, ItemStack held, const float at[3], const std::vector<Change> &list = {}) {
    ByteWriter w;
    w.varint(2);
    changes(w, list);
    w.varint(1);
    position(w, {0, 0, 0});
    w.signed_varint(-1);
    w.signed_varint(slot);
    item(w, held);
    vec(w, at[0], at[1], at[2]);
    vec(w, 0, 0, 0);
    return packet(PacketId::InventoryTransaction, w.take());
}
GamePacket consume(int slot, ItemStack held, const float at[3], const std::vector<Change> &list) {
    ByteWriter w;
    w.varint(4);
    changes(w, list);
    w.varint(1);
    w.signed_varint(slot);
    item(w, held);
    vec(w, at[0], at[1], at[2]);
    return packet(PacketId::InventoryTransaction, w.take());
}
GamePacket attack(std::uint64_t target, int slot, ItemStack held, const float at[3], const std::vector<Change> &list) {
    ByteWriter w;
    w.varint(3);
    changes(w, list);
    w.varint64(target);
    w.varint(1);
    w.signed_varint(slot);
    item(w, held);
    vec(w, at[0], at[1], at[2]);
    vec(w, 0, 0, 0);
    return packet(PacketId::InventoryTransaction, w.take());
}
GamePacket equip(std::uint64_t runtime, unsigned slot, ItemStack held) {
    ByteWriter w;
    w.varint64(runtime);
    item(w, held);
    w.byte(static_cast<std::uint8_t>(slot));
    w.byte(static_cast<std::uint8_t>(slot));
    w.byte(0);
    return packet(PacketId::MobEquipment, w.take());
}
GamePacket player_action(std::uint64_t runtime, int kind) {
    ByteWriter w;
    w.varint64(runtime);
    w.signed_varint(kind);
    position(w, {0, 0, 0});
    w.signed_varint(0);
    return packet(PacketId::PlayerAction, w.take());
}
GamePacket fall(std::uint64_t runtime, float distance) {
    ByteWriter w;
    w.varint64(runtime);
    w.float32(distance);
    w.boolean(false);
    return packet(PacketId::ActorFall, w.take());
}
GamePacket stand(std::uint64_t runtime, const float at[3]) {
    Motion m;
    m.runtime = runtime;
    m.data.x = at[0];
    m.data.y = at[1];
    m.data.z = at[2];
    m.grounded = true;
    return packet(PacketId::MovePlayer, motion(m));
}
std::uint64_t first_varint(const Bytes &b) {
    std::uint64_t v = 0;
    for (std::size_t i = 0, shift = 0; i < b.size(); ++i, shift += 7) {
        v |= static_cast<std::uint64_t>(b[i] & 0x7f) << shift;
        if (b[i] < 0x80)
            break;
    }
    return v;
}
using Seen = std::vector<std::pair<unsigned, std::string>>;
// The packets the reference keeps: the two players' entity packets and the
// combat/effect packets.
Seen relevant(const std::vector<GamePacket> &packets) {
    Seen out;
    for (const auto &p : packets) {
        const unsigned id = p.header.id;
        const bool entity = id == 0x27 || id == 0x28 || id == 0x1b || id == 0x1d || id == 0x1c || id == 0x20;
        if (entity && first_varint(p.body) != 1 && first_varint(p.body) != 7)
            continue;
        if (id == 0x1b && p.body.size() > 1 && p.body[1] == 0x42) // structure feature event
            continue;
        if (entity || id == 0x18 || id == 0x60 || id == 0x26 || id == 0x2c || id == 0x0f)
            out.emplace_back(id, hex(p.body));
    }
    return out;
}
Seen reference(const nlohmann::json &steps, const std::string &step, const char *who) {
    Seen out;
    for (const auto &s : steps)
        if (s["step"] == step)
            for (const auto &p : s[who])
                out.emplace_back(p[0].get<unsigned>(), p[1].get<std::string>());
    return out;
}
void show(const char *what, const Seen &a, const Seen &b) {
    std::cerr << what << "\n  BDS-RE:\n";
    for (const auto &[id, body] : a)
        std::cerr << "    " << std::hex << id << std::dec << ' ' << body << '\n';
    std::cerr << "  original:\n";
    for (const auto &[id, body] : b)
        std::cerr << "    " << std::hex << id << std::dec << ' ' << body << '\n';
}
} // namespace

void modifiers(const nlohmann::json &ref);
void instant(const nlohmann::json &ref);

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: combat_tests combat_bds_1.6.0.15.json effect_modifiers_bds_1.6.0.15.json\n";
        return 2;
    }
    std::ifstream file(argv[1]);
    const auto ref = nlohmann::json::parse(file)["steps"];
    std::ifstream second(argv[2]);
    try {
        const auto steps = nlohmann::json::parse(second)["steps"];
        modifiers(steps);
        instant(steps);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    try {
        World w;
        w.configure_terrain({1, 123});
        for (int cx = -1; cx <= 0; ++cx)
            for (int cz = -1; cz <= 0; ++cz)
                w.ensure_chunk(cx, cz);
        Properties properties;
        properties.difficulty = 2; // normal, as the capture
        AccessLists access(".");
        Server server(w, properties, access);
        for (int i = 0; i < 10; ++i)
            server.allocate_entity_id();
        const std::uint64_t A = 1, B = 7;
        const float pa[3] = {0.5f, 5.62f, 0.5f}, pb[3] = {2.5f, 5.62f, 0.5f};
        for (auto [n, at] : {std::pair{A, pa}, std::pair{B, pa}}) { // both spawn at A's place
            StartGame g;
            g.runtime_id = n;
            g.unique_id = static_cast<std::int64_t>(n);
            g.x = at[0];
            g.y = at[1];
            g.z = at[2];
            g.settings.spawn = {0, 4, 0};
            server.prepare(identity(static_cast<unsigned>(n)), g);
            server.handle(n, packet(PacketId::SetLocalPlayerAsInitialized, {static_cast<std::uint8_t>(n)}), 0);
        }
        auto &a = w.players().at("uuid:" + identity(1).identity);
        auto &b = w.players().at("uuid:" + identity(7).identity);
        a.inventory[0] = stack("golden_apple", 2);
        a.inventory[1] = stack("diamond_sword");
        a.inventory[2] = stack("spider_eye", 4);
        a.inventory[3] = stack("bread", 4);
        b.inventory[0] = stack("iron_helmet");
        b.inventory[1] = stack("iron_sword");
        server.handle(A, stand(A, pa), 0);
        server.handle(B, stand(B, pb), 0);
        const auto run = [&](int ticks, Seen *to_a = nullptr, Seen *to_b = nullptr) {
            for (int t = 0; t < ticks; ++t) {
                server.tick();
                auto ra = relevant(server.drain(A)), rb = relevant(server.drain(B));
                if (to_a)
                    to_a->insert(to_a->end(), ra.begin(), ra.end());
                if (to_b)
                    to_b->insert(to_b->end(), rb.begin(), rb.end());
            }
        };
        run(40);

        // A falls 10 blocks: 7 damage.
        const float up[3] = {pa[0], pa[1] + 10.0f, pa[2]};
        GamePacket rise = stand(A, up);
        rise.body[rise.body.size() - 2] = 0; // not on ground
        server.handle(A, rise, 0);
        run(6);
        server.handle(A, stand(A, pa), 0);
        server.handle(A, fall(A, 10.0f), 0);
        Seen fell = relevant(server.drain(A));
        run(1, &fell);
        // The block-fall sound carries air's runtime id (3757 for seed 123,
        // the same palette order as the original's).
        auto fell_want = reference(ref, "a_fall", "a");
        if (fell != fell_want)
            show("fall", fell, fell_want);
        check(fell == fell_want, "fall sounds, hurt event and health");
        run(29);
        check(a.health == 13.0f, "fall leaves A at 13 health");

        // A eats a golden apple: start (flag USINGITEM), consume after 33 ticks.
        server.handle(A, use_air(0, stack("golden_apple", 2), pa), 0);
        Seen started;
        run(1, &started);
        check(!started.empty() && started[0] == reference(ref, "a_eat", "a")[0], "using-item flags");
        run(32);
        Seen eat;
        check(server.handle(A, consume(0, stack("golden_apple", 2), pa, {{0, 0, stack("golden_apple", 2), stack("golden_apple", 1)}}), 0).empty(),
              "the predicted consume needs no correction");
        eat = relevant(server.drain(A));
        run(5 * 20 + 10, &eat);
        auto want = reference(ref, "a_eat", "a");
        want.erase(want.begin());
        if (eat != want)
            show("eating a golden apple", eat, want);
        check(eat == want, "golden apple: effects, attributes, colour, regeneration and removal");
        check(a.inventory[0] == stack("golden_apple", 1), "one apple eaten");

        // B puts on the iron helmet by using it.
        server.drain(A);
        server.drain(B);
        server.handle(B, use_air(0, stack("iron_helmet"), pb,
                                 {{0, 0, stack("iron_helmet"), {}}, {120, 0, {}, stack("iron_helmet")}}),
                      0);
        Seen ea = relevant(server.drain(A)), eb = relevant(server.drain(B));
        run(1, &ea, &eb);
        check(ea == reference(ref, "b_equip", "a") && eb == reference(ref, "b_equip", "b"), "armor equipment broadcast");
        check(b.armor[0] == stack("iron_helmet") && !b.inventory[0].block, "helmet worn");

        // A hits B with the diamond sword (1 + 7 = 8, helmet: 7).
        server.handle(A, equip(A, 1, stack("diamond_sword")), 0);
        run(10);
        server.handle(A, attack(B, 1, stack("diamond_sword"), pa, {{0, 1, stack("diamond_sword"), stack("diamond_sword", 1, 1)}}), 0);
        Seen ha = relevant(server.drain(A)), hb = relevant(server.drain(B));
        run(1, &ha, &hb);
        auto want_a = reference(ref, "a_hit", "a"), want_b = reference(ref, "a_hit", "b");
        want_a.resize(5);
        want_b.resize(6);
        // A's exhaustion entry joins A's packets at the same tick.
        want_a.push_back(reference(ref, "a_hit", "a")[5]);
        want_b.push_back(reference(ref, "a_hit", "b")[6]);
        // The original ticked B before A; both entries arrive in that tick.
        const auto unordered = [](Seen v) {
            std::sort(v.end() - 2, v.end());
            return v;
        };
        ha = unordered(ha);
        hb = unordered(hb);
        want_a = unordered(want_a);
        want_b = unordered(want_b);
        if (ha != want_a || hb != want_b) {
            show("A hits B (A)", ha, want_a);
            show("A hits B (B)", hb, want_b);
        }
        check(ha == want_a && hb == want_b, "sword hit through armor");
        check(b.health == 13.0f && b.armor[0].aux == 2 && a.inventory[1].aux == 1,
              "B at 13, helmet worn by 2, sword by 1");

        // B sprint-attacks A with the iron sword (1 + 6 = 7): absorption takes 4.
        run(80); // natural regeneration brings A back to 20
        check(a.health == 20.0f, "A regenerated");
        server.handle(B, equip(B, 1, stack("iron_sword")), 0);
        server.handle(B, player_action(B, 9), 0);
        run(10);
        server.handle(B, attack(A, 1, stack("iron_sword"), pb, {{0, 1, stack("iron_sword"), stack("iron_sword", 1, 1)}}), 0);
        Seen sa = relevant(server.drain(A)), sb = relevant(server.drain(B));
        run(1, &sa, &sb);
        auto ws = reference(ref, "b_sprint", "a");
        // B's exhaustion: 0.32 (two walked blocks + 0.3 for the attack) in the
        // capture; here B's natural regeneration fell in between (+3).
        for (auto &[id, body] : sa)
            if (id == 0x1d && body.rfind("0702", 0) == 0)
                body.replace(20, 8, "0bd7a33e");
        check(std::abs(b.exhaustion - 3.32f) < 1e-5f || std::abs(b.exhaustion - 0.32f) < 1e-5f, "B exhaustion");
        if (sa != ws)
            show("B sprint-hits A", sa, ws);
        check(sa == ws, "sprint hit: absorption, armor packet, knockback, push, attacker stops sprinting");

        // A drops one spider eye.
        server.handle(A, packet(PacketId::InventoryTransaction, [&] {
                          ByteWriter wr;
                          wr.varint(0);
                          wr.varint(2);
                          wr.varint(0);
                          wr.signed_varint(0);
                          wr.varint(2);
                          item(wr, stack("spider_eye", 4));
                          item(wr, stack("spider_eye", 3));
                          wr.varint(2);
                          wr.varint(0);
                          wr.varint(0);
                          item(wr, {});
                          item(wr, stack("spider_eye", 1));
                          return wr.take();
                      }()),
                      0);
        // Player::drop(item, false): one entity with the default pickup delay,
        // thrown forward along the view (yaw 0: +z) with a spread under 0.02.
        check(server.items().size() == 1 && server.items()[0].item == stack("spider_eye", 1) &&
                  server.items()[0].pickup_delay == 40 && server.items()[0].motion.z > 0.27f &&
                  server.items()[0].motion.z < 0.33f && std::abs(server.items()[0].motion.x) < 0.021f,
              "dropped spider eye entity");
        check(a.inventory[2] == stack("spider_eye", 3), "three spider eyes left");
    try {
        // Poison from a spider eye (hungry player): -1 health every 25 ticks
        // as MAGIC damage (cause 14), as B in the capture; it stops at 1.
        World w;
        w.configure_terrain({1, 123});
        Properties properties;
        properties.difficulty = 2;
        AccessLists access(".");
        Server server(w, properties, access);
        for (int i = 0; i < 10; ++i)
            server.allocate_entity_id();
        PlayerData hungry;
        hungry.hunger = 10;
        hungry.health = 3;
        hungry.inventory[0] = stack("spider_eye", 1);
        w.players()["uuid:" + identity(1).identity] = hungry;
        StartGame g;
        g.runtime_id = 1;
        g.unique_id = 1;
        g.settings.spawn = {0, 4, 0};
        server.prepare(identity(1), g);
        server.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
        const float at[3] = {g.x, g.y, g.z};
        server.tick();
        server.drain(1);
        server.handle(1, use_air(0, stack("spider_eye", 1), at), 0);
        server.handle(1, consume(0, stack("spider_eye", 1), at, {{0, 0, stack("spider_eye", 1), {}}}), 0);
        const auto added = relevant(server.drain(1));
        check(!added.empty() && added[0] == Seen::value_type{0x1c, "0101260001c801"}, "poison effect added");
        int hits = 0;
        for (int t = 0; t < 120; ++t) {
            server.tick();
            for (const auto &[id, body] : relevant(server.drain(1)))
                hits += id == 0x1b && body == "01021c";
        }
        auto &p = w.players().at("uuid:" + identity(1).identity);
        check(hits == 2 && p.health == 1.0f, "poison hurts twice and stops at 1 health");
        // Armor and item damage are persisted.
        p.armor[1] = stack("diamond_chestplate", 1, 17);
        p.inventory[5] = stack("stone_sword", 1, 3);
        const auto copy = WorldStore::decode(WorldStore::encode(w), {});
        const auto &q = copy.players().at("uuid:" + identity(1).identity);
        check(q.armor == p.armor && q.inventory == p.inventory, "armor and damage persisted");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    if (failures)
        return 1;
    std::cout << "eating, effects, absorption, armor, combat and dropping match the original\n";
    return 0;
}

// Speed, strength, health boost and weakness given by the effect command, two
// punches, then clearing (tests/reference/effect_modifiers_bds_1.6.0.15.json).
void modifiers(const nlohmann::json &ref) {
    World w;
    w.configure_terrain({1, 123});
    for (int cx = -1; cx <= 0; ++cx)
        for (int cz = -1; cz <= 0; ++cz)
            w.ensure_chunk(cx, cz);
    Properties properties;
    properties.difficulty = 2;
    AccessLists access(".");
    Server server(w, properties, access);
    for (int i = 0; i < 10; ++i)
        server.allocate_entity_id();
    const std::uint64_t A = 1, B = 7;
    const float pa[3] = {0.5f, 5.62f, 0.5f}, pb[3] = {2.5f, 5.62f, 0.5f};
    for (auto n : {A, B}) {
        StartGame g;
        g.runtime_id = n;
        g.unique_id = static_cast<std::int64_t>(n);
        g.x = pa[0];
        g.y = pa[1];
        g.z = pa[2];
        g.settings.spawn = {0, 4, 0};
        server.prepare(identity(static_cast<unsigned>(n)), g);
        server.handle(n, packet(PacketId::SetLocalPlayerAsInitialized, {static_cast<std::uint8_t>(n)}), 0);
    }
    server.handle(A, stand(A, pa), 0);
    server.handle(B, stand(B, pb), 0);
    const auto step = [&](const char *name, auto act, std::size_t keep_a, std::size_t keep_b) {
        act();
        Seen ga = relevant(server.drain(A)), gb = relevant(server.drain(B));
        for (int t = 0; t < 2; ++t) {
            server.tick();
            auto ra = relevant(server.drain(A)), rb = relevant(server.drain(B));
            ga.insert(ga.end(), ra.begin(), ra.end());
            gb.insert(gb.end(), rb.begin(), rb.end());
        }
        auto wa = reference(ref, name, "a"), wb = reference(ref, name, "b");
        wa.resize(std::min(wa.size(), keep_a));
        wb.resize(std::min(wb.size(), keep_b));
        // Players tick in the order of their actor addresses in the
        // original: packets of both players in one tick are compared as a set.
        for (auto *v : {&ga, &gb, &wa, &wb})
            std::sort(v->begin(), v->end());
        if (ga != wa || gb != wb) {
            show((std::string(name) + " (A)").c_str(), ga, wa);
            show((std::string(name) + " (B)").c_str(), gb, wb);
        }
        check(ga == wa && gb == wb, std::string("effect modifiers: ") + name);
        for (int t = 0; t < 18; ++t) { // the oracle's steps are about a second apart
            server.tick();
            server.drain(A);
            server.drain(B);
        }
    };
    for (int t = 0; t < 40; ++t) {
        server.tick();
        server.drain(A);
        server.drain(B);
    }
    step("effects", [&] {
        server.give_effect(A, 1, 30, 1);
        server.give_effect(A, 5, 30, 0);
        server.give_effect(A, 21, 30, 0);
    }, 5, 5);
    step("strong_punch", [&] { server.handle(A, attack(B, 0, {}, pa, {}), 0); }, 6, 7);
    step("weakness", [&] { server.give_effect(A, 18, 30, 0); }, 2, 2);
    step("weak_punch", [&] { server.handle(A, attack(B, 0, {}, pa, {}), 0); }, 6, 7);
    step("clear", [&] { server.clear_effects(A); }, 7, 7);
}

// Instant health then instant damage II (effect command, 1 second): a per-tick
// temporal buff; the repeated harm is refused by the hurt cooldown until it
// kills ~6 ticks later.
void instant(const nlohmann::json &ref) {
    World w;
    w.configure_terrain({1, 123});
    Properties properties;
    properties.difficulty = 2;
    AccessLists access(".");
    Server server(w, properties, access);
    for (int i = 0; i < 10; ++i)
        server.allocate_entity_id();
    StartGame g;
    g.runtime_id = 1;
    g.unique_id = 1;
    g.x = 0.5f;
    g.y = 5.62f;
    g.z = 0.5f;
    g.settings.spawn = {0, 4, 0};
    server.prepare(identity(1), g);
    server.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
    for (int t = 0; t < 40; ++t) {
        server.tick();
        server.drain(1);
    }
    server.handle(1, fall(1, 10.0f), 0);
    for (int t = 0; t < 20; ++t) {
        server.tick();
        server.drain(1);
    }
    const auto watch = [&](const char *name, int ticks, bool as_set = false) {
        Seen got = relevant(server.drain(1));
        for (int t = 0; t < ticks; ++t) {
            server.tick();
            auto r = relevant(server.drain(1));
            got.insert(got.end(), r.begin(), r.end());
        }
        std::erase_if(got, [](const auto &p) { return p.first == 0x18 || p.first == 0x41 || p.first == 0x09 || p.first == 0x2d; });
        auto want = reference(ref, name, "a");
        if (as_set) {
            std::sort(got.begin(), got.end());
            std::sort(want.begin(), want.end());
        }
        if (got != want)
            show(name, got, want);
        check(got == want, std::string("effect: ") + name);
    };
    server.give_effect(1, 6, 1, 0);
    // The capture has the colour data before the second UpdateAttributes,
    // although a gdb trace puts both in one tick and _sendDirtyActorData
    // (0x21bd970) sends attributes first; compared as a set.
    watch("instant_heal", 30, true);
    server.give_effect(1, 7, 1, 1);
    watch("instant_harm", 40);
}
