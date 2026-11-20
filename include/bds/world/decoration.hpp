#pragma once
#include "bds/world/overworld.hpp"
#include <map>
#include <tuple>
#include <functional>

namespace bds {
// postProcess uses a BlockSource over several 256-high LevelChunks. Surface
// generation's 128-high BlockVolume alone cannot hold cross-column features.
class DecorationRegion {
  public:
    using Position = std::tuple<int, int, int>;
    struct Mutation {
        Position position;
        TerrainBlock block;
        int flags;
    };
    virtual ~DecorationRegion() = default;
    void load_chunk(int x, int z, const TerrainVolume &);
    void load_empty_chunk(int x, int z);
    bool has_chunk(int x, int z) const { return chunks_.contains({x, z}); }
    // Virtual so the native post-process (chunk_post_process.cpp) can run the
    // same features over its GenRegion.
    virtual TerrainBlock block(Position) const;
    virtual bool set_block(Position, TerrainBlock, int flags = 3);
    std::vector<Mutation> take_mutations() { return std::exchange(mutations_, {}); }

  private:
    std::map<std::pair<int, int>, std::vector<TerrainBlock>> chunks_;
    std::vector<Mutation> mutations_;
};

// OreFeature 0x1b58e40/0x1b58eb0: also used for dirt, gravel and stone variants.
bool place_ore(DecorationRegion &, DecorationRegion::Position, TerrainBlock, int size, Random &);

// BlockSource dependencies of LakeFeature. Material solidity is distinct from
// collision/opacity; brightness is the sky component of getBrightnessPair.
// The caller supplies these from its block registry and lighting snapshot.
struct LakeMaterial {
    bool solid;
    bool liquid;
    bool lava;
};
struct LakeEnvironment {
    std::function<LakeMaterial(TerrainBlock)> material;
    std::function<std::uint8_t(DecorationRegion::Position)> sky_brightness;
    // Every Feature::_placeBlock call, including writes of an identical block
    // and out-of-height writes. Region mutations contain only changed cells.
    std::function<void(const DecorationRegion::Mutation &)> placement_observer;
};
// LakeFeature constructors 0x1b5dbb0/0x1b5dc10, place 0x1b5dc70.
// Native callers normally use air above the liquid; the second constructor
// accepts a different upper block. Does not run pending liquid/lighting jobs.
bool place_lake(DecorationRegion &, DecorationRegion::Position, TerrainBlock liquid, Random &,
                const LakeEnvironment &, TerrainBlock upper = terrain_block::air);
struct OrePlacement {
    DecorationRegion::Position position;
    TerrainBlock target;
    int size;
};
using OrePlacementObserver = std::function<void(const OrePlacement &)>;
// Default OverworldDecorator::decorateOres and Mesa's extra-gold override.
void decorate_ores(DecorationRegion &, DecorationRegion::Position, Random &, bool mesa = false,
                   const OrePlacementObserver &observer = {});
} // namespace bds
