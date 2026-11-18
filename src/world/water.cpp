#include "bds/world/world.hpp"
#include <algorithm>
#include <climits>
#include <cmath>

namespace bds {
namespace {
// _getSpread/_getSlopeDistance: W E N S. Neighbor notification: W E S N U D.
constexpr std::array<std::array<int, 3>, 6> directions{
    {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, -1, 0}}};
World::Position offset(World::Position p, unsigned direction) {
  const auto [x, y, z] = p;
  const auto d = directions[direction];
  // World coordinates at int limits have no representable neighbor.
  const auto add = [](int a, int b) {
    return int(std::clamp(std::int64_t(a) + b, std::int64_t(INT_MIN),
                          std::int64_t(INT_MAX)));
  };
  return {add(x, d[0]), y + d[1], add(z, d[2])};
}
} // namespace
World::Block World::water_cell(Position p) const {
  const auto [x, y, z] = p;
  return chunk(chunk_coordinate(x), chunk_coordinate(z)) ? block(p) : 0;
}
bool World::water_blocking(Position p) const {
  // Translation for this runtime's full-cube palette. Partial shapes,
  // waterlogged/extra layers and other replaceable materials need their
  // native liquidCanFlowIntoFromDirection implementations.
  return block_has_collision(water_cell(p));
}
bool World::water_can_spread(Position p) const {
  const auto [x, y, z] = p;
  if (y < 0 || y >= 256 || !chunk(chunk_coordinate(x), chunk_coordinate(z)))
    return false;
  const auto id = block(p);
  // LiquidBlockDynamic::_canSpreadTo 0x1e9dbe0: same material, lava and
  // blocking geometry reject the destination.
  return !is_water(id) && id != kStillLava && !water_blocking(p);
}
void World::push_water(std::vector<WaterTick> &heap, WaterTick value) {
  std::size_t hole = heap.size();
  heap.push_back(value);
  while (hole > 0) {
    const auto parent = (hole - 1) / 2;
    if (heap[parent].due <= value.due)
      break;
    heap[hole] = heap[parent];
    hole = parent;
  }
  heap[hole] = value;
}
World::WaterTick World::pop_water(std::vector<WaterTick> &heap) {
  const auto result = heap.front(), value = heap.back();
  heap.pop_back();
  if (heap.empty())
    return result;
  // libstdc++ __adjust_heap: equal children choose the right child. Equal
  // deadlines have no insertion-order tie-break in TickNextTickData.
  std::size_t hole = 0, right = 2;
  while (right < heap.size()) {
    if (heap[right].due > heap[right - 1].due)
      --right;
    heap[hole] = heap[right];
    hole = right;
    right = 2 * (hole + 1);
  }
  if (right == heap.size()) {
    heap[hole] = heap[right - 1];
    hole = right - 1;
  }
  while (hole > 0) {
    const auto parent = (hole - 1) / 2;
    if (heap[parent].due <= value.due)
      break;
    heap[hole] = heap[parent];
    hole = parent;
  }
  heap[hole] = value;
  return result;
}
void World::schedule_water(Position p, unsigned delay) {
  const auto [x, y, z] = p;
  if (y < 0 || y >= 256)
    return;
  auto &queue =
      water_ticks_
          .try_emplace(std::pair{chunk_coordinate(x), chunk_coordinate(z)},
                       WaterQueue{age_, {}})
          .first->second;
  if (queue.clock > std::numeric_limits<std::uint64_t>::max() - delay ||
      water_sequence_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("water tick queue exhausted");
  push_water(queue.heap, WaterTick{p, queue.clock + delay, water_sequence_++});
}
void World::wake_water_neighbors(Position p) {
  // BlockSource::updateNeighborsAt 0x1c5b400; static::_setDynamic 0x1e3d330
  // preserves all 4 data bits and writes with flag 4 (no recursive wake).
  for (const unsigned dir : {0u, 1u, 3u, 2u, 4u, 5u}) {
    const auto at = offset(p, dir);
    const auto [x, y, z] = at;
    if (y < 0 || y >= 256 || !chunk(chunk_coordinate(x), chunk_coordinate(z)))
      continue;
    const auto id = block(at);
    if (is_water(id) && !is_flowing_water(id)) {
      set_block(at, water_block(water_depth(id)), 4);
      schedule_water(at); // _setDynamic schedules after dynamic::onPlace
    }
  }
}
void World::water_changed(Position p, Block before, Block after, int flags) {
  if ((flags & 2) && (is_water(before) || is_water(after)))
    water_updates_.push_back({p, after, flags});
  if (is_flowing_water(after) && !is_flowing_water(before))
    schedule_water(p);
  if (flags & 1)
    wake_water_neighbors(p);
}
int World::water_slope_distance(Position p, int distance, int previous) const {
  // LiquidBlockDynamic::_getSlopeDistance 0x1e9f180, depth limit 4 for water.
  int result = 1000;
  for (int dir = 0; dir < 4; ++dir) {
    if (dir == (previous ^ 1))
      continue;
    const auto to = offset(p, dir);
    if (water_blocking(to) || water_depth(water_cell(to)) == 0)
      continue;
    if (!water_blocking(offset(to, 5)))
      return distance;
    if (distance < 4)
      result = std::min(result, water_slope_distance(to, distance + 1, dir));
  }
  return result;
}
std::array<bool, 4> World::water_spread(Position p) const {
  // LiquidBlockDynamic::_getSpread 0x1e9ee70; all ties, including 1000,
  // are returned. _trySpreadTo subsequently rejects blocked destinations.
  std::array<int, 4> costs{1000, 1000, 1000, 1000};
  for (unsigned dir = 0; dir < 4; ++dir) {
    const auto to = offset(p, dir);
    if (water_blocking(to) || water_depth(water_cell(to)) == 0)
      continue;
    costs[dir] =
        !water_blocking(offset(to, 5)) ? 0 : water_slope_distance(to, 1, dir);
  }
  const auto best = *std::min_element(costs.begin(), costs.end());
  return {costs[0] == best, costs[1] == best, costs[2] == best,
          costs[3] == best};
}
std::array<float, 3> World::water_flow(Position p) const {
  // LiquidBlock::_getFlow 0x1e8d8d0. Unlike later/other editions, this
  // implementation returns a horizontal normal, even for falling water.
  const auto rendered = [](BlockId id) {
    const int depth = water_depth(id);
    return depth >= 8 ? 0 : depth;
  };
  const int depth = rendered(water_cell(p));
  if (depth < 0)
    return {};
  std::array<float, 3> flow{};
  // Facing::Plane::HORIZONTAL is N E S W in the original registry.
  for (const unsigned dir : {2u, 1u, 3u, 0u}) {
    const auto to = offset(p, dir);
    int other = rendered(water_cell(to));
    int gradient = 0;
    if (other >= 0)
      gradient = other - depth;
    else if (!water_blocking(to)) {
      other = rendered(water_cell(offset(to, 5)));
      if (other >= 0)
        gradient = other - (depth - 8);
    }
    flow[0] += static_cast<float>(directions[dir][0] * gradient);
    flow[2] += static_cast<float>(directions[dir][2] * gradient);
  }
  const float length = static_cast<float>(std::sqrt(static_cast<double>(
      flow[0] * flow[0] + flow[1] * flow[1] + flow[2] * flow[2])));
  if (length < 1.0e-4f)
    return {};
  return {flow[0] / length, flow[1] / length, flow[2] / length};
}
void World::tick_water(Position p) {
  // LiquidBlockDynamic::_tick 0x1e9dd10, ordinary (non-instatick) water
  // branch. Water consumes no random draws here.
  const auto original = water_cell(p);
  if (!is_flowing_water(original))
    return;
  int depth = water_depth(original);
  if (depth > 0) {
    int highest = -100, sources = 0;
    for (const unsigned dir : {0u, 1u, 3u, 2u}) {
      const auto to = offset(p, dir);
      if (water_blocking(to))
        continue;
      int d = water_depth(water_cell(to));
      if (d < 0)
        continue;
      if (!d)
        ++sources;
      if (d >= 8)
        d = 0;
      if (highest < 0 || d < highest)
        highest = d;
    }
    int next = highest + 1;
    if (next >= 8 || highest < 0)
      next = -1;
    const int above = water_depth(water_cell(offset(p, 4)));
    if (above >= 0)
      next = above >= 8 ? above : above + 8;
    if (sources == 1 && above == 0)
      ++sources; // 0x1e9e2d5
    const auto below = offset(p, 5);
    if (sources >= 2 && (block_has_collision(water_cell(below)) ||
                         water_depth(water_cell(below)) == 0))
      next = 0;
    if (next != depth) {
      depth = next;
      set_block(p, depth < 0 ? 0 : water_block(depth));
      if (depth >= 0) {
        wake_water_neighbors(
            p); // explicit updateNeighborsAt after setLiquidBlock
        schedule_water(p);
      }
    } else
      set_block(p, water_block(depth, false), 2);
  } else
    set_block(p, water_block(depth, false), 2);
  const auto below = offset(p, 5);
  if (water_can_spread(below)) {
    // Even a just-removed cell can emit the final downward depth (7).
    set_block(below, water_block(depth >= 8 ? depth : depth + 8));
    schedule_water(below); // _spread, in addition to dynamic::onPlace
  } else if (depth >= 0 && (!depth || water_blocking(below))) {
    const auto spread = water_spread(p);
    const int next = depth >= 8 ? 1 : depth + 1;
    if (next >= 8)
      return;
    for (unsigned dir = 0; dir < 4; ++dir) {
      const auto to = offset(p, dir);
      if (spread[dir] && water_can_spread(to)) {
        set_block(to, water_block(next));
        schedule_water(to);
      }
    }
  }
}
void World::tick_water_chunk(int x, int z) {
  const auto key = std::pair{x, z};
  const auto queue = water_ticks_.find(key);
  if (queue == water_ticks_.end() || !chunk(x, z))
    return;
  auto &pending = queue->second;
  pending.clock = age_;
  std::vector<WaterTick> due;
  // BlockTickingQueue::tickPendingTicks 0x1c517b0: extract at most 100
  // existing entries into a second heap before executing any of them.
  const auto budget = std::min<std::size_t>(100, pending.heap.size());
  for (std::size_t i = 0;
       i < budget && !pending.heap.empty() && pending.heap.front().due <= age_;
       ++i)
    push_water(due, pop_water(pending.heap));
  while (!due.empty()) {
    const auto tick = pop_water(due);
    const auto [px, py, pz] = tick.position;
    const auto bound = [](int n, int d) {
      return int(std::clamp(std::int64_t(n) + d, std::int64_t(INT_MIN),
                            std::int64_t(INT_MAX)));
    };
    bool loaded = true;
    for (int cx = chunk_coordinate(bound(px, -8));
         cx <= chunk_coordinate(bound(px, 8)); ++cx)
      for (int cz = chunk_coordinate(bound(pz, -8));
           cz <= chunk_coordinate(bound(pz, 8)); ++cz)
        loaded = loaded && chunk(cx, cz);
    if (loaded)
      tick_water(tick.position);
    else
      schedule_water(tick.position, 0);
  }
}
} // namespace bds
