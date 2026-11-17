#pragma once
#include "bds/world/random.hpp"
#include "bds/world/world.hpp"

namespace bds {
enum class GrassTickBlockKind { Air, Dirt, Grass, GrassPath, Other };
struct GrassTickBlock {
    GrassTickBlockKind kind = GrassTickBlockKind::Other;
    bool solid = false;
};
struct GrassTickMaterial {
    float translucency = 0;
    bool liquid = false;
};
// The original GrassBlock queries BlockSource/light/material state. Keep those
// dependencies explicit: a host must supply real lighting, not a guessed
// "air means bright" rule. Reference tests replay their original live values.
class GrassTickSource {
  public:
    virtual ~GrassTickSource() = default;
    // BlockSource::getRawBrightness(position, true, true).
    virtual unsigned brightness(World::Position position) = 0;
    virtual GrassTickBlock block(World::Position position) = 0;
    virtual GrassTickMaterial material(World::Position position) = 0;
    virtual int dirt_variant(World::Position position) = 0;
    virtual void set_block(World::Position position, GrassTickBlockKind kind, int flags) = 0;
};
// GrassBlock::tick (Linux BDS 1.6.0.15, 0x1e716a0). Not yet connected to
// Server::tick: the chunk scheduler/light backend must be translated first.
void tick_grass(GrassTickSource &source, World::Position position, Random &random,
                bool client_side = false);
} // namespace bds
