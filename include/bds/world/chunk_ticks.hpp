#pragma once
#include "bds/world/block_ticks.hpp"
#include "bds/world/grass_ticks.hpp"
#include "bds/world/lighting.hpp"
#include <functional>
namespace bds {
using BlockTickMutation = std::function<void(World::Position, BlockId, int)>;
// LevelChunk::tickBlocks 0x1de5040, bounded to clear weather / non-freezing
// Overworld and the host's air/stone/grass/dirt/bedrock primary layer.
// Weather's lightning/freeze/snow effects require further translated providers.
// LCG and Level Random stay independent. Unsupported blocks fail explicitly.
void tick_clear_chunk(World &world, WorldLighting &lighting, int x, int z,
                      Random &random, BlockTickRandom &coordinates,
                      unsigned sky_darken,
                      const BlockTickMutation &mutation = {});
} // namespace bds
