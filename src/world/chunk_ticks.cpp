#include "bds/world/chunk_ticks.hpp"
namespace bds {
namespace {
class Source final : public GrassTickSource {
public:
  Source(World &world, WorldLighting &lighting, unsigned darken,
         const BlockTickMutation &mutation)
      : world_(world), lighting_(lighting), darken_(darken),
        mutation_(mutation) {}
  unsigned brightness(World::Position p) override {
    return lighting_.raw(p, darken_);
  }
  GrassTickBlock block(World::Position p) override {
    const auto id = loaded_block(p);
    const auto kind = id == 0   ? GrassTickBlockKind::Air
                      : id == 2 ? GrassTickBlockKind::Grass
                      : id == 3 ? GrassTickBlockKind::Dirt
                                : GrassTickBlockKind::Other;
    return {kind, tick_block_properties(id).solid};
  }
  GrassTickMaterial material(World::Position p) override {
    const auto &properties = tick_block_properties(loaded_block(p));
    return {properties.translucency, properties.liquid};
  }
  int dirt_variant(World::Position) override {
    return 0;
  } // host palette has only ordinary dirt
  void set_block(World::Position p, GrassTickBlockKind kind,
                 int flags) override {
    const BlockId id = kind == GrassTickBlockKind::Dirt ? 3 : 2;
    world_.set_block(p, id); // synchronous authoritative block mutation
    if (mutation_)
      mutation_(p, id, flags);
  }

private:
  BlockId loaded_block(World::Position p) const {
    const auto [x, y, z] = p;
    return world_.chunk(chunk_coordinate(x), chunk_coordinate(z))
               ? world_.block(p)
               : 0;
  }
  World &world_;
  WorldLighting &lighting_;
  unsigned darken_;
  const BlockTickMutation &mutation_;
};
} // namespace
void tick_clear_chunk(World &world, WorldLighting &lighting, int x, int z,
                      Random &random, BlockTickRandom &coordinates,
                      unsigned sky_darken, const BlockTickMutation &mutation) {
  const auto *chunk = world.chunk(x, z);
  if (!chunk)
    return;
  // 0x1de50b9..0x1de5114: lightningLevel=0 gives nextInt(100000),
  // then isLightning=false; the rain+lightning mob branch is skipped.
  random.next_int(100000);
  // 0x1de5654..0x1de574d: advance weather-column LCG even in clear
  // weather, then nextInt(16). This host has no water to freeze and rain=0
  // skips the snow/handleRain branch at 0x1de583e.
  coordinates.next_column();
  random.next_int(16);
  const auto height = chunk->allocated_height();
  const auto samples = BlockTickRandom::sample_count(height / 16);
  Source source(world, lighting, sky_darken, mutation);
  for (unsigned i = 0; i < samples; ++i) {
    const auto p = coordinates.next_position(static_cast<int>(height));
    const World::Position at{x * 16 + p[0], p[1], z * 16 + p[2]};
    const auto block = world.block(at);
    if (tick_block_properties(block).tickable)
      tick_grass(source, at, random); // Block::tick -> GrassBlock::tick
    // Original extra-layer tick follows primary-layer tick at 0x1de5baf.
    // The current host has an empty extra layer for all supported blocks.
  }
}
} // namespace bds
