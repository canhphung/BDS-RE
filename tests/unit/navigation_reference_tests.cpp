#include "bds/game/mob_motion.hpp"
#include "bds/game/navigation.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace bds;
using namespace bds::game;
using Json = nlohmann::json;
PathNode node(const Json &j) { return {j[0], j[1], j[2]}; }
Json json_node(PathNode n) { return Json::array({n.x, n.y, n.z}); }
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected native paths fixture");
        std::ifstream in(argv[1]);
        const auto ref = Json::parse(in);
        if (ref.at("binary_sha256") != "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad")
            throw std::runtime_error("unexpected original binary");
        int count = 0;
        for (const auto &s : ref.at("scenes")) {
            World w;
            w.configure_terrain({1, 123});
            for (int x = -4; x <= 4; ++x)
                for (int z = -4; z <= 4; ++z)
                    w.ensure_chunk(x, z);
            for (const auto &e : s.at("edits"))
                w.set_block({e[0].get<int>(), e[1].get<int>(), e[2].get<int>()}, e[3].get<BlockId>());
            for (const auto &c : s.at("cases")) {
                const auto &q = c.at("search");
                Json expansions = Json::array();
                auto path = PathFinder::find(
                    w, node(q.at("start").at("pos")), node(q.at("target").at("pos")), node(q.at("size")),
                    q.at("max_distance"), [&](const PathExpansion &e) {
                        Json neighbors = Json::array();
                        for (auto n : e.neighbors)
                            neighbors.push_back(json_node(n));
                        expansions.push_back({{"node", json_node(e.node)}, {"neighbors", neighbors}});
                    });
                Json expected = Json::array();
                for (const auto &e : c.at("expanded")) {
                    Json neighbors = Json::array();
                    for (const auto &n : e.at("neighbors"))
                        neighbors.push_back(n.at("pos"));
                    expected.push_back({{"node", e.at("node").at("pos")}, {"neighbors", neighbors}});
                }
                Json result = nullptr, expected_path = nullptr;
                if (path) {
                    result = Json::array();
                    for (auto n : path->nodes)
                        result.push_back(json_node(n));
                }
                if (!c.at("path").is_null()) {
                    expected_path = Json::array();
                    for (const auto &n : c.at("path"))
                        expected_path.push_back(n.at("pos"));
                }
                if (expansions != expected || result != expected_path) {
                    std::cerr << s.at("name") << " target " << c.at("target") << "\ngot " << result
                              << " expected " << expected_path << '\n';
                    for (std::size_t i = 0; i < std::min(expansions.size(), expected.size()); ++i)
                        if (expansions[i] != expected[i]) {
                            std::cerr << "expansion " << i << " got " << expansions[i] << " expected "
                                      << expected[i] << '\n';
                            break;
                        }
                    throw std::runtime_error("native path search differs");
                }
                ++count;
            }
        }
        if (ref.contains("controls"))
            for (const auto &c : ref.at("controls").at("move_cases")) {
                const auto &b = c.at("before"), &a = b.at("actor"), &ctl = b.at("control");
                ActorMotion actor;
                actor.pos = {a["position"][0], a["position"][1], a["position"][2]};
                const auto &box = a.at("box");
                actor.bb = {{box[0], box[1], box[2]}, {box[3], box[4], box[5]}};
                MobBrain brain;
                brain.yaw = a.at("yaw");
                MoveControl move;
                move.wanted = {ctl["wanted"][0], ctl["wanted"][1], ctl["wanted"][2]};
                move.modifier = ctl.at("modifier");
                move.max_turn = ctl.at("max_turn");
                move.has_wanted = ctl.at("has_wanted").get<int>() != 0;
                move.speed = ctl.at("speed");
                move.forward = ctl.at("forward");
                JumpControl jump;
                move.tick(actor, brain, 0.25f, jump);
                const auto &e = c.at("after");
                const auto &ec = e.at("control");
                if (brain.yaw != e.at("actor").at("yaw").get<float>() ||
                    move.speed != ec.at("speed").get<float>() ||
                    move.forward != ec.at("forward").get<float>() ||
                    move.has_wanted != (ec.at("has_wanted").get<int>() != 0) ||
                    jump.wanted != (e.at("jump_wanted").get<int>() != 0)) {
                    std::cerr << "control " << c << " got yaw=" << brain.yaw << " speed=" << move.speed
                              << " forward=" << move.forward << " jump=" << jump.wanted << '\n';
                    throw std::runtime_error("native control differs");
                }
                jump.tick();
                if (jump.jumping != (c.at("after_jump").at("jumping").get<int>() != 0) || jump.wanted)
                    throw std::runtime_error("native jump control differs");
                ++count;
            }
        if (ref.contains("controls"))
            for (const auto &sequence : ref.at("controls").at("navigation_cases")) {
                World w;
                w.configure_terrain({1, 123});
                for (int x = -4; x <= 4; ++x)
                    for (int z = -4; z <= 4; ++z)
                        w.ensure_chunk(x, z);
                const auto &initial = sequence.at("initial"), &state = initial.at("actor");
                ActorMotion actor;
                actor.height_offset = 0;
                actor.max_step = state.at("max_step");
                const auto vector = [](const Json &v) -> Vec3 { return {v[0], v[1], v[2]}; };
                actor.pos = vector(state.at("position"));
                actor.motion = vector(state.at("motion"));
                const auto &box = state.at("box");
                actor.bb = {{box[0], box[1], box[2]}, {box[3], box[4], box[5]}};
                actor.on_ground = true;
                MobBrain brain;
                brain.yaw = state.at("yaw");
                MoveControl move;
                JumpControl jump;
                PathNavigation nav;
                const float width = initial.at("width"), height = initial.at("height");
                if (!nav.move_to(w, actor, vector(sequence.at("target")), 0.8f, width, height))
                    throw std::runtime_error("create path failed");
                for (std::size_t i = 0; i < sequence.at("steps").size(); ++i) {
                    const auto &step = sequence.at("steps")[i];
                    nav.tick(w, actor, width, height, move);
                    move.tick(actor, brain, 0.25f, jump);
                    jump.tick();
                    const auto &ec = step.at("controls").at("control");
                    if ((nav.path ? int(nav.path->index) : -1) !=
                            (step.at("index").is_null() ? -1 : step.at("index").get<int>()) ||
                        move.wanted != vector(ec.at("wanted"))) {
                        std::cerr << "tick " << i << " got index " << (nav.path ? int(nav.path->index) : -1)
                                  << " expected " << step.at("index") << '\n';
                        throw std::runtime_error("native path following differs");
                    }
                    travel_mob(actor, w, {0, 0, move.forward * 0.98f}, brain.yaw, move.speed);
                    const auto &e = step.at("after").at("actor");
                    if (actor.pos != vector(e.at("position")) || actor.motion != vector(e.at("motion")) ||
                        brain.yaw != e.at("yaw").get<float>()) {
                        std::cerr << "trajectory tick " << i << " got "
                                  << Json::array({actor.pos.x, actor.pos.y, actor.pos.z}) << " motion "
                                  << Json::array({actor.motion.x, actor.motion.y, actor.motion.z})
                                  << " expected " << e << '\n';
                        throw std::runtime_error("native retained navigation motion differs");
                    }
                    ++count;
                }
            }
        std::cout << count << " native paths/controls/retained motion match\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
