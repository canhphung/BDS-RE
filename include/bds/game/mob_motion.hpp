#pragma once
#include "bds/game/item_actor.hpp"
namespace bds::game {
// Actor::moveRelative 0x21b0880; libm sin/cos, not the mce lookup table.
void move_relative(ActorMotion &actor, Vec3 input, float yaw, float speed);
// Mob::travel 0x22b5ad0, normal land/air branch. Inputs are post-aiStep
// damping. No liquid/ladder/gliding/effect/rider branches in this host yet.
void travel_mob(ActorMotion &actor, const World &world, Vec3 input, float yaw,
                float ground_speed, float air_speed = 0.02f,
                float friction_modifier = 1.0f, bool gravity = true);
// Actor::push(Actor&,bool) 0x21b1280, non-riding pushable actors.
void push_actors(ActorMotion &self, ActorMotion &other, bool one_way,
                 float pushthrough = 0.0f);
} // namespace bds::game
