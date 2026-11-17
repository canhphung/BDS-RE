#pragma once
#include "bds/game/item_actor.hpp"
namespace bds::game {
// Actor::moveRelative 0x21b0880; libm sin/cos, not the mce lookup table.
void move_relative(ActorMotion &actor, Vec3 input, float yaw, float speed);
enum class TravelMedium { Land, Water, Lava };
struct TravelOptions {
    TravelMedium medium = TravelMedium::Land;
    float underwater_speed = 0.02f, water_slowdown = 0.8f;
    int depth_strider = 0, levitation = -1;
    bool slow_falling = false, sprinting = false;
};
TravelMedium travel_medium(const ActorMotion &, const World &);
// Level::checkAndHandleMaterial 0x1c84fe0, primary-layer water branch.
bool apply_water_current(ActorMotion &, const World &, AABB water_box);
void update_water_current(ActorMotion &, const World &);
// Mob::travel 0x22b5ad0. Inputs are post-aiStep damping.
void travel_mob(ActorMotion &actor, const World &world, Vec3 input, float yaw, float ground_speed,
                float air_speed = 0.02f, float friction_modifier = 1.0f, bool gravity = true,
                const TravelOptions &options = {});
// Actor::push(Actor&,bool) 0x21b1280, non-riding pushable actors.
void push_actors(ActorMotion &self, ActorMotion &other, bool one_way,
                 float pushthrough = 0.0f);
} // namespace bds::game
