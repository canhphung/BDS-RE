#include "bds/game/mob_motion.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace bds::game {
void move_relative(ActorMotion &a, Vec3 input, float yaw, float speed) {
  float length = input.x * input.x + input.y * input.y + input.z * input.z;
  if (length < 1.0e-4f)
    return; // 0x28275a0
  length = static_cast<float>(std::sqrt(static_cast<double>(length)));
  if (length < 1.0f)
    length = 1.0f;
  const float scale = speed / length;
  input.x = scale * input.x;
  input.y = scale * input.y;
  input.z = scale * input.z;
  const float angle = yaw * std::bit_cast<float>(0x3c8efa35u);
  const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
  const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
  a.motion.x += input.x * c - input.z * s;
  a.motion.y += input.y;
  a.motion.z += input.z * c + input.x * s;
}
void travel_mob(ActorMotion &a, const World &world, Vec3 input, float yaw,
                float ground_speed, float air_speed, float friction_modifier,
                bool gravity) {
  // Mob::calcMoveRelativeSpeed 0x22b5886..0x22b5a4f. Ground speed is
  // Mob::getSpeed(), already evaluated by the AI's MoveControl.
  float speed = air_speed;
  if (a.on_ground) {
    const float friction = 0.6f * 0.91f; // all supported ground materials
    const float ratio = (0.6f * 0.91f) / friction;
    speed = ground_speed * (ratio * ratio * ratio);
  }
  move_relative(a, input, yaw, speed);
  // Friction is sampled before Actor::move, while grounded.
  float friction = 0.91f;
  if (a.on_ground) {
    const auto ground =
        world.block({static_cast<int>(std::floor(a.pos.x)),
                     static_cast<int>(std::floor(a.bb.min.y - 0.1f)),
                     static_cast<int>(std::floor(a.pos.z))});
    friction = ground ? 0.6f * 0.91f : std::bit_cast<float>(0x3f0bc6a9u);
  }
  move(a, world, a.motion);
  if (gravity)
    a.motion.y += -0.08f; // ActorFlags::affected_by_gravity, 0x22b7395
  if (gravity)
    a.motion.y *= 0.98f; // 0x22b73df
  friction *= friction_modifier;
  a.motion.x = friction * a.motion.x;
  a.motion.z = friction * a.motion.z;
}
void push_actors(ActorMotion &a, ActorMotion &b, bool one_way,
                 float pushthrough) {
  float dx = b.pos.x - a.pos.x, dz = b.pos.z - a.pos.z;
  float distance = std::max(std::abs(dx), std::abs(dz)); // mce::Math::absMax
  if (distance < 0.01f)
    return; // 0x28275a4
  distance = static_cast<float>(std::sqrt(static_cast<double>(distance)));
  dx /= distance;
  dz /= distance;
  const float inverse = std::min(1.0f / distance, 1.0f);
  dx = inverse * dx;
  dz = inverse * dz;
  dx = 0.05f * dx;
  dz = 0.05f * dz;
  dx = (1.0f - pushthrough) * dx;
  dz = (1.0f - pushthrough) * dz;
  a.motion.x += -dx;
  a.motion.z += -dz;
  if (!one_way) {
    b.motion.x += dx;
    b.motion.z += dz;
  }
}
} // namespace bds::game
