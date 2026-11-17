#include "bds/game/random_pos.hpp"
#include "bds/game/spawner.hpp"
#include <cmath>
#include <limits>
namespace bds::game {
namespace {
bool solid(const World &world, int x, int y, int z) {
  return world.chunk(chunk_coordinate(x), chunk_coordinate(z)) &&
         world.block({x, y, z}) != 0;
}
// RandomPos::snapToGround 0x231bc40. Its initial solid/air transition
// succeeds without changing Y; the subsequent scans store current Y+1.
bool snap(const World &world, World::Position &position, float mob_y,
          int distance) {
  auto &[x, y, z] = position;
  int scan = y;
  const bool start = solid(world, x, scan, z);
  bool above = solid(world, x, scan + 1, z);
  if (start && !above)
    return true;
  const int lower = static_cast<int>(mob_y) - distance; // truncation, not floor
  for (; scan >= lower; --scan) {
    const bool current = solid(world, x, scan, z);
    if (current && !above) {
      y = scan + 1;
      return true;
    }
    above = current;
  }
  bool previous = start;
  const int upper = static_cast<int>(mob_y) + distance;
  scan = y;
  while (scan < upper) {
    ++scan;
    const bool current = solid(world, x, scan, z);
    if (!current && previous) {
      y = scan + 1;
      return true;
    }
    previous = current;
  }
  return false;
}
} // namespace
std::optional<Vec3> random_stroll_position(const World &world,
                                           WorldLighting &lighting, Vec3 p,
                                           Random &random, int xz,
                                           int y_distance,
                                           unsigned sky_darken) {
  // The original goal supplies positive bounded xz/y ranges (10,7).
  if (xz <= 0 || xz > 1024 || y_distance <= 0 || y_distance > 256)
    throw std::invalid_argument("unsupported RandomPos search range");
  float best = -99999.0f; // 0x2855c50
  std::optional<Vec3> wanted;
  for (int i = 0; i < 10; ++i) {
    // Native draw order Z, Y, X; upper endpoints are exclusive.
    const int dz = random.next_int(2 * xz) - xz;
    const int dy = random.next_int(2 * y_distance) - y_distance;
    const int dx = random.next_int(2 * xz) - xz;
    World::Position at{static_cast<int>(std::floor(p.x)) + dx,
                       static_cast<int>(std::floor(p.y)) + dy,
                       static_cast<int>(std::floor(p.z)) + dz};
    if (!snap(world, at, p.y, y_distance))
      continue;
    const auto [x, y, z] = at;
    // Animal::_getWalkTargetValue 0x22d2960: grass below scores 10,
    // otherwise actual brightness ramp minus 0.5. No magma in this palette.
    const bool loaded = world.chunk(chunk_coordinate(x), chunk_coordinate(z));
    const float score =
        loaded && world.block({x, y - 1, z}) == 2
            ? 10.0f
            : brightness_ramp(lighting.raw(at, sky_darken)) - 0.5f;
    if (score > best) {
      best = score;
      wanted = Vec3{static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(z)};
    }
  }
  return wanted;
}
} // namespace bds::game
