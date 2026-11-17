#include "bds/game/mob_motion.hpp"
#include "bds/game/navigation.hpp"
#include "bds/game/random_pos.hpp"
#include "bds/game/spawner.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using namespace bds::game;
using Json = nlohmann::json;
Vec3 vec(const Json &j) { return {j[0], j[1], j[2]}; }
class Senses final : public MobSenses {
  public:
    World world;
    WorldLighting light{world};
    Random level{456}, own{123};
    ActorMotion actor;
    PathNavigation nav;
    float width, height;
    Senses(float w, float h) : width(w), height(h) {
        world.configure_terrain({1, 123});
        for (int x = -4; x <= 4; ++x)
            for (int z = -4; z <= 4; ++z)
                world.ensure_chunk(x, z);
    }
    Random &level_random() override { return level; }
    std::vector<NearbyPlayer> players_in(float) override { return {}; }
    std::optional<NearbyPlayer> player(std::uint64_t) override { return std::nullopt; }
    BlockId block(World::Position p) override { return world.block(p); }
    void eat_start() override { throw std::runtime_error("cow cannot eat grass"); }
    void eat_grass(World::Position) override { throw std::runtime_error("cow cannot eat grass"); }
    std::optional<Vec3> stroll_position(int xz, int y) override {
        return random_stroll_position(world, light, actor.pos, own, xz, y, 0);
    }
    bool navigation_done() const override { return nav.done(); }
    bool navigation_close(float d) const override { return nav.close_to_done(actor.pos, d); }
    std::optional<World::Position> navigation_end() const override {
        if (!nav.path || nav.path->nodes.empty())
            return std::nullopt;
        auto p = nav.path->position(width, nav.path->nodes.size() - 1);
        return World::Position{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
    }
    void navigate(Vec3 p, float speed) override { nav.move_to(world, actor, p, speed, width, height); }
    void stop_navigation() override { nav.stop(); }
};
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected retained native AI fixture");
        std::ifstream in(argv[1]);
        const auto ref = Json::parse(in);
        if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad")
            throw std::runtime_error("unexpected original binary");
        Senses senses(ref.at("width"), ref.at("height"));
        const auto &initial = ref.at("initial");
        const auto &a = initial.at("actor");
        auto &actor = senses.actor;
        actor.pos = vec(a.at("position"));
        actor.motion = vec(a.at("motion"));
        actor.height_offset = 0;
        actor.max_step = a.at("max_step");
        const auto &box = a.at("box");
        actor.bb = {{box[0], box[1], box[2]}, {box[3], box[4], box[5]}};
        actor.on_ground = true;
        auto brain = make_brain(actor_type::Cow, false, senses.width, senses.height);
        if (brain.attach_head != vec(ref.at("attach3")) || brain.attach_eyes != vec(ref.at("attach7")))
            throw std::runtime_error("native initial attach offsets differ");
        MoveControl move;
        JumpControl jump;
        std::vector<std::uint32_t> level_words, own_words;
        senses.level.observe_draws([&](std::uint32_t w) { level_words.push_back(w); });
        senses.own.observe_draws([&](std::uint32_t w) { own_words.push_back(w); });
        unsigned tick = 0, total = 0;
        for (const auto &step : ref.at("steps")) {
            level_words.clear();
            own_words.clear();
            tick_goals(brain, actor.pos, senses);
            senses.nav.tick(senses.world, actor, senses.width, senses.height, move);
            move.tick(actor, brain, 0.25f, jump);
            tick_look(brain, actor.pos);
            jump.tick();
            const auto &c = step.at("controls");
            if (level_words != step.at("level_words").get<std::vector<std::uint32_t>>() ||
                own_words != step.at("own_words").get<std::vector<std::uint32_t>>() ||
                move.speed != c.at("speed").get<float>() || brain.pitch != c.at("pitch").get<float>() ||
                brain.head_yaw != c.at("head").get<float>() ||
                brain.yaw != c.at("actor").at("yaw").get<float>()) {
                std::cerr << "AI tick " << tick << " got words " << Json(level_words) << " own "
                          << Json(own_words) << " pitch " << brain.pitch << " headbits "
                          << std::bit_cast<std::uint32_t>(brain.head_yaw) << " expectedbits "
                          << std::bit_cast<std::uint32_t>(c.at("head").get<float>()) << " yaw/head "
                          << brain.yaw << '/' << brain.head_yaw << " expected " << c << '\n';
                throw std::runtime_error("native walking AI decisions differ");
            }
            travel_mob(actor, senses.world, {0, 0, move.forward * 0.98f}, brain.yaw, move.speed);
            const auto &e = step.at("after").at("actor");
            if (actor.pos != vec(e.at("position")) || actor.motion != vec(e.at("motion"))) {
                std::cerr << "tick " << tick << " position "
                          << Json::array({actor.pos.x, actor.pos.y, actor.pos.z}) << " expected " << e
                          << '\n';
                throw std::runtime_error("native retained walking AI trajectory differs");
            }
            total += unsigned(level_words.size() + own_words.size());
            ++tick;
        }
        std::cout << tick << " native walking AI ticks and " << total << " RNG words match exactly\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
