#pragma once
#include "bds/game/mob_ai.hpp"
#include <functional>
namespace bds::game {
struct PathNode {
    int x = 0, y = 0, z = 0;
    bool operator==(const PathNode &) const = default;
};
struct Path {
    std::vector<PathNode> nodes;
    std::size_t index = 0;
    bool done() const { return index >= nodes.size(); }
    Vec3 position(float width, std::size_t i) const;
};
struct PathExpansion {
    PathNode node;
    std::vector<PathNode> neighbors;
};
// BDS 1.6.0.15 PathFinder (0x1c72780), walker node evaluator. The host's
// cube palette has no doors/fences/damage blocks; their evaluators are separate.
class PathFinder {
  public:
    using Observer = std::function<void(const PathExpansion &)>;
    static std::optional<Path> find(const World &, PathNode start, PathNode target, PathNode size,
                                    float max_distance, Observer observe = {});
    static std::optional<Path> create(const World &, const ActorMotion &, Vec3 target, float width,
                                      float height, float max_distance = 16);
};
struct JumpControl {
    bool wanted = false, jumping = false;
    void jump() { wanted = true; }
    void tick() {
        jumping = wanted;
        wanted = false;
    } // 0x23364c0
};
struct MoveControl {
    Vec3 wanted;
    float modifier = 0, max_turn = 30, speed = 0, forward = 0;
    bool has_wanted = false;
    void set_wanted(Vec3 p, float s) {
        wanted = p;
        modifier = s;
        has_wanted = true;
    }
    void tick(ActorMotion &, MobBrain &, float movement, JumpControl &);
};
class PathNavigation {
  public:
    std::optional<Path> path;
    float speed = 0;
    int ticks = 0, last_check = 0;
    Vec3 last_pos;
    bool done() const { return !path || path->done(); }
    void stop() { path.reset(); }
    bool move_to(const World &, const ActorMotion &, Vec3, float, float, float);
    void tick(const World &, const ActorMotion &, float width, float height, MoveControl &);
    bool close_to_done(Vec3 position, float distance) const;
};
} // namespace bds::game
