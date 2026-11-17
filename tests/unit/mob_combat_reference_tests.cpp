// A player punches a summoned chicken to death, compared with the original's
// packets (tests/reference/mob_combat_bds_1.6.0.15.json, recorded with
// tools/re/scenarios.py mob_combat): the hit sound, the HURT event and the
// health attribute, then the DEATH event, the MobKilled EventPacket, the
// zero health, the loot items and the RemoveActor after the death time; then
// the experience orb: its actor data and AddActor bytes, the pickup
// LevelEvent and the player's experience attributes.
#include "bds/game/items.hpp"
#include "bds/game/server.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
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
GamePacket attack(std::uint64_t target, const float at[3]) {
    ByteWriter w;
    w.varint(3); // ItemUseOnActor
    w.varint(0); // no inventory actions
    w.varint64(target);
    w.varint(1); // attack
    w.signed_varint(0);
    item(w, ItemStack{});
    for (float v : {at[0], at[1], at[2], 0.0f, 0.0f, 0.0f})
        w.float32(v);
    return packet(PacketId::InventoryTransaction, w.take());
}
std::int64_t zigzag64(ByteReader &in) {
    const auto v = in.varint64();
    return static_cast<std::int64_t>(v >> 1) ^ -static_cast<std::int64_t>(v & 1);
}
float float32(ByteReader &in) {
    const auto b = in.take(4);
    float v;
    std::memcpy(&v, b.data(), 4);
    return v;
}
const GamePacket *find(const std::vector<GamePacket> &packets, PacketId id, const std::string &prefix = "") {
    for (const auto &p : packets)
        if (p.header.id == static_cast<unsigned>(id) && hex(p.body).rfind(prefix, 0) == 0)
            return &p;
    return nullptr;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "expected mob combat reference path\n";
        return 1;
    }
    // ExperienceOrb::truncateExperienceAmount and _computeLevelXp.
    check(truncate_experience_amount(1) == 1 && truncate_experience_amount(2) == 1 &&
              truncate_experience_amount(3) == 3 && truncate_experience_amount(16) == 7 &&
              truncate_experience_amount(5000) == 2477,
          "truncateExperienceAmount");
    check(level_xp(0) == 7 && level_xp(14) == 35 && level_xp(15) == 37 && level_xp(30) == 112, "_computeLevelXp");
    std::ifstream input(argv[1]);
    const auto reference = nlohmann::json::parse(input);
    World w;
    w.configure_terrain({1, 123});
    for (int cx = -2; cx <= 2; ++cx)
        for (int cz = -2; cz <= 2; ++cz)
            w.ensure_chunk(cx, cz);
    Properties properties;
    properties.difficulty = 2;
    AccessLists access(".");
    Server server(w, properties, access);
    // The player is runtime 1 (unique -4294967295) as in the capture.
    server.allocate_entity_id();
    StartGame g;
    g.runtime_id = 1;
    g.unique_id = reference.at("player").at("unique").get<std::int64_t>();
    const float eye[3] = {0.5f, 5.62f, 0.5f};
    g.x = eye[0];
    g.y = eye[1];
    g.z = eye[2];
    auth::Identity id;
    id.identity = "00000000-0000-4000-8000-000000000001";
    id.display_name = "Alpha";
    id.client_data_json = "{}";
    server.prepare(id, g);
    server.handle(1, packet(PacketId::SetLocalPlayerAsInitialized, {1}), 0);
    // The chicken is runtime 8: the capture's summons followed seven ids.
    while (server.allocate_entity_id() < 7) {
    }
    const auto chicken = server.summon(actor_type::Chicken, {2.5f, 4.0f, 0.5f}, true);
    check(chicken && *chicken == 8, "summoned chicken has runtime id 8");
    for (int i = 0; i < 3; ++i)
        server.tick();
    auto seen = server.drain(1);
    check(find(seen, PacketId::AddActor) != nullptr, "the player is sent the chicken (AddActor)");

    // First punch: ATTACK_STRONG at the chicken's body centre with the
    // player's actor type, then ActorEvent HURT (cause ENTITY_ATTACK).
    server.handle(1, attack(8, eye), 0);
    seen = server.drain(1);
    const auto &first = reference.at("first_hit");
    const auto *sound = find(seen, PacketId::LevelSoundEvent);
    check(sound && hex(sound->body) == first.at("sound").get<std::string>(), "first hit sound bytes");
    const auto *hurt = find(seen, PacketId::ActorEvent, "08");
    check(hurt && hex(hurt->body) == first.at("hurt_event").get<std::string>(), "HURT ActorEvent bytes");
    server.tick();
    seen = server.drain(1);
    const auto *health = find(seen, PacketId::UpdateAttributes, "08");
    check(health && hex(health->body) == first.at("health").get<std::string>(), "health 2 of 4 UpdateAttributes bytes");
    check(find(seen, PacketId::SetActorMotion, "08") != nullptr, "the knockback reaches the client (SetActorMotion)");

    // A punch within the 10 hurt ticks does no damage.
    server.handle(1, attack(8, eye), 0);
    seen = server.drain(1);
    check(find(seen, PacketId::ActorEvent, "08") == nullptr, "no HURT event while invulnerable");
    for (int i = 0; i < 12; ++i)
        server.tick();
    server.drain(1);

    // The killing punch: DEATH, the MobKilled EventPacket to the killer, then
    // the zero health and the loot (raw chicken, 0..2 feathers).
    const auto &kill = reference.at("killing_hit");
    const auto pos = std::find_if(server.mobs().begin(), server.mobs().end(),
                                  [](const Server::MobActor &m) { return m.runtime_id == 8; })->pos;
    const float near[3] = {pos.x - 1.5f, pos.y + 1.62f, pos.z};
    server.handle(1, attack(8, near), 0);
    seen = server.drain(1);
    const auto *death = find(seen, PacketId::ActorEvent, "08");
    check(death && hex(death->body) == kill.at("death_event").get<std::string>(), "DEATH ActorEvent bytes");
    const auto *event = find(seen, PacketId::Event);
    check(event && hex(event->body) == kill.at("mob_killed").get<std::string>(), "MobKilled EventPacket bytes");
    server.tick();
    seen = server.drain(1);
    const auto *zero = find(seen, PacketId::UpdateAttributes, "08");
    check(zero && hex(zero->body) == kill.at("health").get<std::string>(), "health 0 UpdateAttributes bytes");
    int chickens = 0, feathers = 0;
    for (const auto &item : server.items()) {
        chickens += item.item.block == item_named("chicken") ? item.item.count : 0;
        feathers += item.item.block == item_named("feather") ? item.item.count : 0;
    }
    check(chickens == 1, "one raw chicken dropped");
    check(feathers <= 2, "at most two feathers dropped");

    // Mob::tickDeath removes the body after more than 20 death ticks and
    // spawns the experience orbs (Chicken reward 1..2, split into value-1 orbs).
    const auto &xp = reference.at("experience_orb");
    int removed_at = -1, data_at = -1, add_at = -1;
    std::uint64_t orb = 0;
    for (int t = 1; t <= 25 && removed_at < 0; ++t) {
        server.tick();
        seen = server.drain(1);
        if (find(seen, PacketId::RemoveActor, reference.at("remove").get<std::string>()))
            removed_at = t;
        if (!server.orbs().empty() && data_at < 0) {
            orb = server.orbs().front().runtime_id;
            const auto *data = find(seen, PacketId::SetActorData, hex(Bytes{static_cast<std::uint8_t>(orb)}));
            if (data) {
                data_at = t;
                check(hex(data->body).substr(2) == xp.at("actor_data_after_runtime").get<std::string>(),
                      "orb SetActorData (packAll) bytes");
            }
        }
    }
    check(removed_at >= 19 && removed_at <= 21, "RemoveActor after the death time (" + std::to_string(removed_at) + ")");
    check(std::none_of(server.mobs().begin(), server.mobs().end(),
                       [](const Server::MobActor &m) { return m.runtime_id == 8; }),
          "the dead chicken left the level");
    const auto orbs = server.orbs().size();
    check(orbs == 1 || orbs == 2, "one or two experience orbs (" + std::to_string(orbs) + ")");
    for (const auto &o : server.orbs()) {
        check(o.value == xp.at("value").get<int>(), "orb value 1 (truncateExperienceAmount)");
        check(static_cast<std::uint64_t>(o.unique_id) == (0xffffffff00000000ULL | o.runtime_id), "orb unique id");
    }
    check(data_at > 0, "the orb's actor data is sent when it spawns");

    // The orb follows the player (within 8 blocks), the player picks it up
    // (1 in 6 per touch): LevelEvent 1051, the experience attributes, and the
    // RemoveActor in the next tick.
    int event_at = -1, attributes_at = -1, orb_removed_at = -1;
    std::int64_t orb_unique = 0;
    for (const auto &o : server.orbs())
        if (o.runtime_id == orb)
            orb_unique = o.unique_id;
    for (int t = data_at + 1; t <= 400 && (orb_removed_at < 0 || attributes_at < 0); ++t) {
        server.tick();
        seen = server.drain(1);
        for (const auto &p : seen) {
            const auto body = hex(p.body);
            if (p.header.id == static_cast<unsigned>(PacketId::AddActor) && add_at < 0) {
                ByteReader in(p.body);
                if (zigzag64(in) == orb_unique && in.varint64() == orb && in.varint() == 69u) {
                    add_at = t;
                    for (int i = 0; i < 6; ++i)
                        float32(in);
                    check(float32(in) == 0.0f, "AddActor pitch 0");
                    const float yaw = float32(in);
                    check(std::fmod(yaw / 360.0f * 256.0f, 1.0f) == 0.0f, "AddActor yaw is a whole byte angle");
                    check(float32(in) == 0.0f, "AddActor head yaw 0");
                    check(body.substr(body.size() - xp.at("add_actor_after_rotation").get<std::string>().size()) ==
                              xp.at("add_actor_after_rotation").get<std::string>(),
                          "orb AddActor attributes, metadata and links bytes");
                }
            }
            if (p.header.id == static_cast<unsigned>(PacketId::LevelEvent) && event_at < 0) {
                ByteReader in(p.body);
                if (in.signed_varint() == xp.at("pickup_event").get<int>())
                    event_at = t;
            }
            if (p.header.id == static_cast<unsigned>(PacketId::UpdateAttributes) && body.rfind("0102", 0) == 0 &&
                body.find("706c617965722e657870657269656e6365") != std::string::npos && attributes_at < 0) {
                attributes_at = t;
                check(body == xp.at("experience").get<std::string>(),
                      "player.experience 1/7 UpdateAttributes bytes (reset, then the buff)");
            }
            if (p.header.id == static_cast<unsigned>(PacketId::RemoveActor) && orb_removed_at < 0) {
                ByteReader in(p.body);
                if (zigzag64(in) == orb_unique) {
                    orb_removed_at = t;
                    check(attributes_at > 0, "the orb's RemoveActor follows the experience update");
                }
            }
        }
    }
    check(add_at == data_at + 1, "orb AddActor one tick after its data (" + std::to_string(add_at - data_at) + ")");
    check(event_at > 0, "pickup LevelEvent 1051");
    // Touched in the orb's own tick (chunk phase) the attributes go out in the
    // same tick's player baseTick; touched by Player::aiStep (the capture's
    // case) in the next one, before the RemoveActor.
    check(attributes_at == event_at || attributes_at == event_at + 1, "experience attributes after the pickup");
    check(orb_removed_at == event_at + 1, "orb RemoveActor in the tick after the pickup");
    if (failures)
        return 1;
    std::cout << "Chicken hit, death, loot and removal match the original's packets\n";
    return 0;
}
