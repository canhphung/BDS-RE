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
    void load_chunk(int x, int z, const TerrainVolume &);
    void load_empty_chunk(int x, int z);
    bool has_chunk(int x, int z) const { return chunks_.contains({x, z}); }
    TerrainBlock block(Position) const;
    bool set_block(Position, TerrainBlock, int flags = 3);
    std::vector<Mutation> take_mutations() { return std::exchange(mutations_, {}); }

  private:
    std::map<std::pair<int, int>, std::vector<TerrainBlock>> chunks_;
    std::vector<Mutation> mutations_;
};

// OreFeature 0x1b58e40/0x1b58eb0: also used for dirt, gravel and stone variants.
bool place_ore(DecorationRegion &, DecorationRegion::Position, TerrainBlock, int size, Random &);
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
