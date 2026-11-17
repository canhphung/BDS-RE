#include "bds/world/lighting.hpp"
#include "bds/game/mce_math.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace bds {
const TickBlockProperties &tick_block_properties(BlockId block) {
  static constexpr std::array<TickBlockProperties, 5> table{{
      {0, 0, false, false, false, 1.0f, false}, // air
      {15, 0, true, false, false, 0.0f, false}, // stone
      {15, 0, true, false, true, 0.0f, false},  // grass
      {15, 0, true, false, false, 0.0f, false}, // dirt, mapped type 0
      {15, 0, true, false, false, 0.0f, false}, // bedrock
  }};
  if (block >= table.size())
    throw std::out_of_range("block has no translated lighting/tick properties");
  return table[block];
}
unsigned clear_sky_darken(int time) {
  constexpr float pi = std::bit_cast<float>(0x40490fdbu);
  float phase = static_cast<float>(time % 24000) / 24000.0f - 0.25f;
  if (phase < 0)
    phase += 1.0f;
  if (phase > 1)
    phase -= 1.0f;
  const float initial = phase;
  phase = 1.0f - (game::mce_cos(phase * pi) + 1.0f) / 2.0f;
  phase = initial + (phase - initial) / 3.0f;
  // getOldSkyDarken uses the mce table (not Dimension::getSkyDarken(float)'s
  // cosf).
  float amount = 1.0f - (game::mce_cos(phase * pi * 2.0f) * 2.0f + 0.5f);
  amount = std::clamp(amount, 0.0f, 1.0f);
  float brightness = 1.0f - amount;
  // Original clear-weather attenuation factors are both 1; no boss set.
  return static_cast<unsigned>((1.0f - brightness) * 11.0f);
}
void WorldLighting::rebuild(int chunk_x, int chunk_z) {
  std::array<std::pair<bool, std::uint64_t>, 9> dependencies{};
  std::array<const Chunk *, 9> chunks{};
  unsigned height = 16;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx) {
      const auto n = static_cast<unsigned>((dz + 1) * 3 + dx + 1);
      chunks[n] = world_.chunk(chunk_x + dx, chunk_z + dz);
      dependencies[n] = {chunks[n] != nullptr,
                         chunks[n] ? chunks[n]->light_revision() : 0};
      if (chunks[n])
        height = std::max(height, chunks[n]->allocated_height());
    }
  const auto key = std::pair{chunk_x, chunk_z};
  const auto found = cache_.find(key);
  if (found != cache_.end() && found->second.dependencies == dependencies)
    return;
  if (cache_.size() >= 256)
    cache_.clear();
  // INFERRED host job adapter: compute stable lighting synchronously. The
  // original uses async 48^3 subchunk jobs; here the 3x3-column footprint spans
  // all loaded Y layers. Transient job ordering is not reconstructed.
  // A 16-block margin exceeds the 15-level attenuated propagation range.
  constexpr unsigned width = 48, plane = width * width;
  std::vector<Cell> grid(height * plane);
  bool propagation_needed = false;
  for (unsigned z = 0; z < width; ++z)
    for (unsigned x = 0; x < width; ++x) {
      const auto *chunk = chunks[(z / 16) * 3 + x / 16];
      if (!chunk)
        continue;
      for (unsigned y = 0; y < height; ++y) {
        auto &cell = grid[y * plane + z * width + x];
        const auto &p = tick_block_properties(chunk->block(x % 16, y, z % 16));
        cell.opacity = static_cast<std::uint8_t>(p.opacity);
        // _recalcHeight seeds preserve the half-open direct-sky interval,
        // including top-layer removal; layers above the allocated span are sky.
        cell.sky = chunk->direct_sky(x % 16, y, z % 16) ? 15 : 0;
        cell.block = static_cast<std::uint8_t>(p.emission);
        if ((cell.opacity < 15 && cell.sky < 14) || cell.block)
          propagation_needed = true;
      }
    }
  // Flat/rolling solid ground with no cavities has no cell that any sky seed
  // can raise. Avoid enqueueing already-lit air in that common case.
  if (propagation_needed) {
    std::vector<unsigned> queue;
    for (unsigned i = 0; i < grid.size(); ++i)
      if (grid[i].sky || grid[i].block)
        queue.push_back(i);
    // runPropagation 0x1e20820, _propagateLight 0x1e208c0 and
    // _checkAndAddToQueue 0x1e20a10. Native order:
    // down/up/west/east/north/south.
    constexpr int delta[6] = {
        -static_cast<int>(plane), static_cast<int>(plane), -1, 1, -48, 48};
    for (std::size_t i = 0; i < queue.size(); ++i) {
      const auto index = queue[i], y = index / plane,
                 z = (index % plane) / width, x = index % width;
      const auto source = grid[index];
      if (source.sky <= 1 && source.block <= 1)
        continue;
      const bool inside[6] = {y > 0,         y + 1 < height, x > 0,
                              x + 1 < width, z > 0,          z + 1 < width};
      for (unsigned direction = 0; direction < 6; ++direction) {
        if (!inside[direction])
          continue;
        const auto target_index =
            static_cast<unsigned>(static_cast<int>(index) + delta[direction]);
        const auto tx = target_index % width,
                   tz = (target_index % plane) / width;
        if (!chunks[(tz / 16) * 3 + tx / 16])
          continue;
        auto &target = grid[target_index];
        const int attenuation = std::max(1, static_cast<int>(target.opacity));
        const int sky = std::max(0, static_cast<int>(source.sky) - attenuation);
        const int block =
            std::max(0, static_cast<int>(source.block) - attenuation);
        if (sky > target.sky || block > target.block) {
          target.sky = static_cast<std::uint8_t>(
              std::max(sky, static_cast<int>(target.sky)));
          target.block = static_cast<std::uint8_t>(
              std::max(block, static_cast<int>(target.block)));
          queue.push_back(target_index);
        }
      }
    }
  }
  Column column;
  const auto central_height = std::max(16u, chunks[4]->allocated_height());
  column.cells.resize(central_height * 256);
  for (unsigned y = 0; y < central_height; ++y)
    for (unsigned z = 0; z < 16; ++z)
      for (unsigned x = 0; x < 16; ++x)
        column.cells[(y * 16 + z) * 16 + x] =
            grid[y * plane + (z + 16) * width + x + 16];
  cache_.insert_or_assign(key, CachedColumn{dependencies, std::move(column)});
}
std::array<unsigned, 2> WorldLighting::pair(World::Position position) {
  const auto [x, y, z] = position;
  const auto key = std::pair{chunk_coordinate(x), chunk_coordinate(z)};
  if (const auto *chunk = world_.chunk(key.first, key.second)) {
    // A direct sky seed is already maximal and is preserved by the native
    // lighting invariant (0x1e20f6e..0x1e20fb9). The supported palette has no
    // emission, so this pair cannot change during propagation.
    if (y < 0 || y >= 256 ||
        chunk->direct_sky(local_coordinate(x), static_cast<unsigned>(y),
                          local_coordinate(z)))
      return {15, 0};
    rebuild(key.first, key.second);
    const auto &cells = cache_.at(key).column.cells;
    if (y >= 0 && static_cast<unsigned>(y) < cells.size() / 256) {
      const auto &c =
          cells[(static_cast<unsigned>(y) * 16 + local_coordinate(z)) * 16 +
                local_coordinate(x)];
      return {c.sky, c.block};
    }
  }
  // Original missing chunk/source pair and subchunk-above-span pair: 15,0.
  return {15, 0};
}
unsigned WorldLighting::raw(World::Position position, unsigned sky_darken,
                            bool propagate, bool darken) {
  const auto [x, y, z] = position;
  const bool loaded =
      world_.chunk(chunk_coordinate(x), chunk_coordinate(z)) != nullptr;
  const auto &block =
      tick_block_properties(loaded ? world_.block(position) : 0);
  if (block.opacity == 15)
    return 0; // 0x1c5a6ef; before bounds checks
  if (propagate && block.propagate) {
    unsigned value = raw({x, y + 1, z}, sky_darken, false, darken);
    for (auto at : {World::Position{x - 1, y, z},
                    {x + 1, y, z},
                    {x, y, z + 1},
                    {x, y, z - 1}})
      value = std::max(value, raw(at, sky_darken, false, darken));
    return value;
  }
  if (y < 0)
    return 0;
  if (y >= 256) { // The original applies darken here even when darken=false.
    const auto value = static_cast<std::uint8_t>(15 - sky_darken);
    return value > 15 ? 0 : value;
  }
  if (!world_.chunk(chunk_coordinate(x), chunk_coordinate(z)))
    return 15;
  const auto light = pair(position);
  return std::max(light[0] > (darken ? sky_darken : 0)
                      ? light[0] - (darken ? sky_darken : 0)
                      : 0,
                  light[1]);
}
} // namespace bds
