#pragma once
#include "bds/game/mob_ai.hpp"
#include "bds/game/mob_motion.hpp"
namespace bds::game {
// RideableComponent::positionRider 0x248f470, Vec3::yRot 0x1d768b0.
Vec3 rider_position(Vec3 mount, float yaw, Vec3 seat, float scale, float rider_height);
struct RiderControl {
    float yaw = 0, pitch = 0, strafe = 0, vertical = 0, forward = 0;
};
// Mob::travel's WASD branch (0x22b5d5d..0x22b62ef), before medium travel.
Vec3 ridden_input(ActorMotion &, MobBrain &, const RiderControl &, float movement, float &speed,
                  float &air_speed, float &jump_power, float jump_strength, int jump_boost = -1);
Vec3 view_vector(float pitch, float yaw); // Actor::getViewVector 0x21ab490
Vec3 dismount_position(const ActorMotion &rider, const World &, Vec3 mount, Vec3 view,
                       float distance = 1.6f); // Actor::_exitRide 0x21af210
float player_jump_power(int charge);           // Mob::onPlayerJump 0x22c0c10
} // namespace bds::game
