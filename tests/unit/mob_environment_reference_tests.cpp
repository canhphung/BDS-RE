#include "bds/game/mob_motion.hpp"
#include "bds/world/lighting.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using namespace bds::game;
using Json = nlohmann::json;
Vec3 vec(const Json &p) { return {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()}; }
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
    if (a.pos != expected.pos || a.motion != expected.motion || a.bb.min != expected.bb.min ||
        a.bb.max != expected.bb.max || a.on_ground != expected.on_ground ||
        a.horizontal_collision != expected.horizontal_collision ||
        a.vertical_collision != expected.vertical_collision || a.collision != expected.collision ||
        a.stuck_in_collider != expected.stuck_in_collider ||
        a.last_penetration != expected.last_penetration || a.slide_offset != expected.slide_offset) {
        std::cerr << "Context " << context << "\ngot pos " << Json::array({a.pos.x, a.pos.y, a.pos.z})
                  << " motion " << Json::array({a.motion.x, a.motion.y, a.motion.z}) << " expected " << p
                  << '\n';
        throw std::runtime_error("native mob physics differs");
    }
}
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected native environment fixture");
        std::ifstream in(argv[1]);
        const auto ref = Json::parse(in);
        if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad")
            throw std::runtime_error("unexpected original binary");
        unsigned count = 0;
        for (const auto &c : ref.at("cases")) {
            World w;
            w.configure_terrain({1, 123});
            for (int x = -4; x <= 4; ++x)
                for (int z = -4; z <= 4; ++z)
                    w.ensure_chunk(x, z);
            for (const auto &e : c.at("edits"))
                w.set_block({e[0].get<int>(), e[1].get<int>(), e[2].get<int>()}, e[3].get<BlockId>());
            auto a = actor(c.at("before"));
            TravelOptions options;
            options.medium = c.at("travel_type").get<int>() == 0 ? TravelMedium::Water : TravelMedium::Land;
            if (c.at("effect")[0] == 24)
                options.levitation = c.at("effect")[1];
            options.slow_falling = c.at("effect")[0] == 27;
            if (travel_medium(a, w) != options.medium)
                throw std::runtime_error("native liquid classification differs");
            travel_mob(a, w, vec(c.at("input")), c.at("before").at("yaw"), 0.0f, 0.02f, 1.0f, true, options);
            compare(a, c.at("after"),
                    {{"kind", c.at("kind")}, {"effect", c.at("effect")}, {"input", c.at("input")}});
            ++count;
        }
        for (const auto &p : ref.at("registry")) {
            const auto &v = tick_block_properties(p.at("id").get<BlockId>());
            if (v.opacity != p.at("opacity").get<unsigned>() ||
                v.emission != p.at("emission").get<unsigned>() || v.solid != p.at("solid").get<bool>() ||
                v.propagate != p.at("propagate").get<bool>() || v.tickable != p.at("tickable").get<bool>() ||
                v.translucency != p.at("translucency").get<float>() || v.liquid != p.at("liquid").get<bool>())
                throw std::runtime_error("native liquid registry differs");
        }
        std::cout << count << " native water/effect cases match exactly\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
