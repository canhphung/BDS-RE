#include "bds/game/riding.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using namespace bds::game;
using Json = nlohmann::json;
Vec3 vec(const Json &j) { return {j[0], j[1], j[2]}; }
ActorMotion actor(const Json &j) {
    ActorMotion a;
    a.pos = vec(j.at("position"));
    a.motion = vec(j.at("motion"));
    const auto &b = j.at("box");
    a.bb = {{b[0], b[1], b[2]}, {b[3], b[4], b[5]}};
    a.on_ground = (j.at("ground").get<int>() & 1) != 0;
    a.height_offset = j.at("height_offset");
    a.max_step = j.at("max_step");
    return a;
}
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected native riding fixture");
        std::ifstream in(argv[1]);
        const auto ref = Json::parse(in);
        if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad")
            throw std::runtime_error("unexpected original binary");
        unsigned count = 0;
        if (!ref.at("wasd").get<bool>())
            throw std::runtime_error("native WASD must be active");
        for (const auto &c : ref.at("cases")) {
            const auto expected = actor(c.at("rider"));
            auto p = rider_position(vec(c.at("mount").at("position")), c.at("yaw"), {0, 1.1f, -0.2f},
                                    c.at("scale"), expected.height_offset - 0.4f);
            if (p != expected.pos) {
                std::cerr << "seat got " << Json::array({p.x, p.y, p.z}) << " expected " << c << '\n';
                throw std::runtime_error("native rider seat differs");
            }
            ++count;
        }
        for (const auto &c : ref.at("travel_cases")) {
            World w;
            w.configure_terrain({1, 123});
            for (int x = -1; x <= 1; ++x)
                for (int z = -1; z <= 1; ++z)
                    w.ensure_chunk(x, z);
            auto a = actor(c.at("before"));
            MobBrain brain;
            brain.yaw = c.at("before").at("yaw");
            RiderControl r{c.at("rider_yaw"), c.at("rider_pitch"), c.at("strafe"), 0, c.at("forward")};
            float speed = c.at("speed_before");
            float air = c.at("before").at("air_speed"), power = c.at("power_before");
            if (c.at("charge").get<int>() > 0 && power != player_jump_power(c.at("charge")))
                throw std::runtime_error("native charge scaling differs");
            const auto input =
                ridden_input(a, brain, r, c.at("movement"), speed, air, power, c.at("jump_strength"));
            travel_mob(a, w, input, brain.yaw, speed, air);
            const auto expected = actor(c.at("after"));
            if (a.pos != expected.pos || a.motion != expected.motion ||
                brain.yaw != c.at("after").at("yaw").get<float>() ||
                brain.pitch != c.at("pitch").get<float>() || a.max_step != expected.max_step ||
                air != c.at("after").at("air_speed").get<float>()) {
                std::cerr << "got pos " << Json::array({a.pos.x, a.pos.y, a.pos.z}) << " yaw " << brain.yaw
                          << " expected " << c << '\n';
                throw std::runtime_error("native ridden travel differs");
            }
            ++count;
        }
        if (ref.contains("exit_case")) {
            const auto &test = ref.at("exit_case");
            World world;
            world.configure_terrain({1, 123});
            world.ensure_chunk(0, 0);
            const auto result =
                dismount_position(actor(test.at("before")), world, vec(test.at("mount").at("position")),
                                  vec(test.at("view")), test.at("distance"));
            if (result != vec(test.at("after").at("position")))
                throw std::runtime_error("native dismount placement differs");
            ++count;
        }
        std::cout << count << " native rider cases match exactly\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
