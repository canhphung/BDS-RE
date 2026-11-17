#pragma once
#include "bds/world/world.hpp"
#include <array>
#include <map>
#include <vector>

namespace bds {
struct TickBlockProperties {
  unsigned opacity, emission;
  bool solid, propagate, tickable;
  float translucency;
  bool liquid;
};
// Live registry dump: Block::getLight 0x1e68a90, getLightEmission 0x1e68b80,
// shouldTick 0x1e68b30, BlockLegacy::canPropagateBrightness 0x1c639e0.
const TickBlockProperties &tick_block_properties(BlockId block);
// Dimension::getTimeOfDay(int,float) 0x1d688e0 and getOldSkyDarken 0x1d64f30.
// Clear Overworld only, no boss/weather attenuation; interpolation argument 0.
unsigned clear_sky_darken(int time);

class WorldLighting {
public:
  explicit WorldLighting(const World &world) : world_(world) {}
  // LevelChunk::getBrightness 0x1de2f80: sky, block (not a combined value).
  std::array<unsigned, 2> pair(World::Position position);
  // BlockSource::getRawBrightness 0x1c5a670; server source has sky darkening.
  unsigned raw(World::Position position, unsigned sky_darken,
               bool propagate = true, bool darken = true);

private:
  struct Cell {
    std::uint8_t sky = 0, block = 0, opacity = 0;
  };
  struct Column {
    std::vector<Cell> cells;
  };
  const World &world_;
  struct CachedColumn {
    std::array<std::pair<bool, std::uint64_t>, 9> dependencies;
    Column column;
  };
  std::map<std::pair<int, int>, CachedColumn> cache_;
  void rebuild(int chunk_x, int chunk_z);
};
} // namespace bds
