#pragma once
#include "bds/game/item_actor.hpp"
#include "bds/world/lighting.hpp"
#include <optional>
namespace bds::game {
// RandomPos::getPos/generateRandomPos 0x231b5e0/0x231b620, ordinary
// non-flying/non-swimming animals with navigation, no home restriction and
// directional bias only for RandomPos::getPosAvoid (0x231bbc0): `direction`
// is the mob position minus the position to avoid; a candidate offset with
// dx * direction.x + dz * direction.z < 0 is skipped (0x231b7ca..0x231b806). Uses Actor's own Random; never the level Random.
std::optional<Vec3> random_stroll_position(const World &world,
                                           WorldLighting &lighting,
                                           Vec3 position, Random &actor_random,
                                           int xz, int y_distance,
                                           unsigned sky_darken,
                                           const Vec3 *direction = nullptr);
} // namespace bds::game
