#pragma once
// Biomes and the overworld biome layers translated from BDS 1.6.0.15 (Linux).
// The biome registry is data read from the original (src/world/biome_data.cpp);
// the layer logic is translated from the Layer subclasses (addresses in
// biome_layers.cpp). OverworldDimension::createDefaultLayers 0x1d4f260 builds
// the stack.
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace bds {
enum class BiomeClass {
    Biome, Beach, Ocean, River, Swamp, Taiga, Desert, Forest, Jungle, Nether, Plains, TheEnd, Mutated,
    Savanna, OceanCold, OceanWarm, Overworld, StoneBeach, OceanFrozen, ExtremeHills, OceanLukewarm,
    MushroomIsland, Ice, Mesa
};
struct BiomeData {
    int id;
    const char *name;
    float temperature, downfall, depth, scale; // Biome +0x34, +0x38, +0x4c, +0x50
    bool snow;                                  // +0x3c > 0 (isSnowCovered)
    int type;                                   // BiomeType object (+0x80), compared by identity
    BiomeClass kind;                            // from the vtable
    int parent;                                 // MutatedBiome +0xc0 (id), -1 otherwise
    // OverworldBiome surface: top +0xe0, filler +0xe8, underwater floor +0xf0
    // ("name:data", nullptr where the class has none), floor depth +0xf8.
    const char *top, *filler, *floor;
    int floor_depth;
    int subtype; // +0x1b0: TaigaBiome / ExtremeHillsBiome / SavannaBiome kind (other data elsewhere)
};
const std::vector<BiomeData> &biome_registry();
// Biome::getBiome(int) 0x1dc0690: nullptr outside the registry vector or for
// an empty slot.
const BiomeData *biome(int id);
// Biome::isSame (vtable +0xb8): MutatedBiome::isSame 0x1dbab10 asks its parent;
// Biome::isSame 0x1dc1a40 compares the BiomeType objects.
bool biome_same(const BiomeData &a, const BiomeData &b);
// getTemperatureCategory (vtable +0xb0): 0 for the ocean classes, the parent's
// for MutatedBiome 0x1dbab60, else Biome 0x1dc19e0 (< 0.2 -> 1, < 1.0 -> 2, 3).
int temperature_category(const BiomeData &b);

// Vanilla biome ids (VanillaBiomes::m* globals mapped through the registry dump).
namespace biome_id {
constexpr int Ocean = 0, Plains = 1, Desert = 2, ExtremeHills = 3, Forest = 4, Taiga = 5, Swampland = 6, River = 7,
              FrozenRiver = 11, IceFlats = 12, IceMountains = 13, MushroomIsland = 14, MushroomIslandShore = 15,
              Beaches = 16, DesertHills = 17, ForestHills = 18, TaigaHills = 19, SmallerExtremeHills = 20,
              Jungle = 21, JungleHills = 22, JungleEdge = 23, OceanDeep = 24, StoneBeach = 25, ColdBeach = 26,
              BirchForest = 27, BirchForestHills = 28, RoofedForest = 29, TaigaCold = 30, TaigaColdHills = 31,
              RedwoodTaiga = 32, RedwoodTaigaHills = 33, ExtremeHillsWithTrees = 34, Savanna = 35,
              SavannaRock = 36, Mesa = 37, MesaRock = 38, MesaClearRock = 39, OceanWarm = 40, OceanWarmDeep = 41,
              OceanLukewarm = 42, OceanLukewarmDeep = 43, OceanCold = 44, OceanColdDeep = 45, OceanFrozen = 46,
              OceanFrozenDeep = 47;
}

// Layer (0x1d453c0..0x1d45a50): the 64-bit seed LCG shared by all layers.
// LayerData (+0 result, +8 scratch, swapped after each fillArea).
struct LayerData {
    std::vector<int> a, b;
    int *in = nullptr, *out = nullptr;
    LayerData();
    void swap() { std::swap(in, out); }
};

class Layer {
  public:
    explicit Layer(std::uint32_t salt); // 0x1d453c0
    virtual ~Layer() = default;
    virtual void init(std::int64_t world_seed); // 0x1d454d0
    virtual void fill(LayerData &data, int x, int z, int w, int h) = 0;
    virtual int mode_or_random(int a, int b, int c, int d); // Layer::_modeOrRandom 0x1d45a50
    // Introspection for tests: the class name and the parents in member order
    // (+0x30, +0x40, +0x50), as tools/re/gdb_layer_oracle.py walks them.
    virtual const char *name() const = 0;
    virtual std::vector<Layer *> parents() const { return parent ? std::vector<Layer *>{parent.get()} : std::vector<Layer *>{}; }

  protected:
    void init_random(std::int64_t x, std::int64_t z); // 0x1d455e0
    int next_random(int n);                          // 0x1d456d0
    int random_of(const int *values, int n) { return values[next_random(n)]; } // _random 0x1d45a20
    std::int64_t world_seed_ = 0, chunk_seed_ = 0, base_seed_ = 0; // +0x18, +0x20, +0x28
  public:
    std::shared_ptr<Layer> parent; // +0x30
};

// The two layers BiomeSource uses: the raw (1:4) layer for fillRawBiomeData
// and the VoronoiZoom layer for fillBiomeData.
struct BiomeLayers {
    std::shared_ptr<Layer> raw, zoomed;
};
// OverworldDimension::createDefaultLayers 0x1d4f260 (generator type 1,
// infinite) with the level seed zero-extended to 64 bits.
BiomeLayers overworld_layers(std::uint32_t seed, int generator_type = 1);
// BiomeSource::fillRawBiomeData 0x1dbba10: 10 x 10 ids at (x, z).
std::array<int, 100> raw_biomes(const BiomeLayers &layers, int x, int z);
// BiomeSource::fillBiomeData 0x1dbb8c0: the 16 x 16 ids of a chunk, index x + z * 16.
std::array<int, 256> chunk_biomes(const BiomeLayers &layers, int chunk_x, int chunk_z);
// BiomeSource::getBiome(BlockPos) (vtable +0x38): the zoomed layer filled at
// (x, z) with a 1 x 1 area (the original caches the result).
int biome_at(const BiomeLayers &layers, int x, int z);
} // namespace bds
