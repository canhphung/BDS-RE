#pragma once
#include "bds/game/item_actor.hpp"
#include "bds/world/lighting.hpp"
#include <optional>
namespace bds::game {
// RandomPos::getPos/generateRandomPos 0x231b5e0/0x231b620, ordinary
// non-flying/non-swimming animals with navigation, no home restriction and
// no directional bias. Uses Actor's own Random; never the level Random.
std::optional<Vec3> random_stroll_position(const World &world,
                                           WorldLighting &lighting,
                                           Vec3 position, Random &actor_random,
                                           int xz, int y_distance,
                                           unsigned sky_darken);
} // namespace bds::game
