#pragma once
#include "bds/world/decoration.hpp"
#include "bds/world/overworld.hpp"
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>

namespace bds {
// Small surface features translated from BDS 1.6.0.15 (Linux). Every function
// cites its original address in src/world/vegetation.cpp and is verified
// against tests/reference/vegetation_bds_1.6.0.15.json, a native corpus that
// records each feature's Random words, placement attempts and the original's
// own answers to every predicate it performs.

// A block identity as the original's `Block const&` carries it: legacy name
// plus data variant.
struct FeatureBlock {
    const char *name = "air";
    std::uint16_t data = 0;
    bool operator==(const FeatureBlock &other) const {
        return data == other.data && std::strcmp(name, other.name) == 0;
    }
};

// Random::nextGaussianInt 0x1b52610: nextInt(n) - nextInt(n). Two draws; the
// name is misleading and this is not a Gaussian.
int next_gaussian_int(Random &, int n);

// The native BlockSource/Block queries the features perform. The host supplies
// the original's answers (the corpus records them), so no survival or material
// rule is invented here.
struct VegetationEnvironment {
    std::function<bool(DecorationRegion::Position)> is_empty;                  // 0x1c60cb0
    std::function<bool(DecorationRegion::Position, FeatureBlock)> can_survive; // 0x1e69f10
    std::function<bool(DecorationRegion::Position, FeatureBlock)> may_place;   // 0x1e6a5a0
    // Block::mayPlace(BlockSource&, BlockPos const&, signed char) 0x1e6a600.
    std::function<bool(DecorationRegion::Position, FeatureBlock, int face)> may_place_face;
    // BlockSource::getHeightmap 0x1c5ad20.
    std::function<int(DecorationRegion::Position)> heightmap;
    // Block::mayPlaceOn 0x1e6a540.
    std::function<bool(DecorationRegion::Position, FeatureBlock)> may_place_on;
    // BlockSource::getBlock 0x1c58ad0.
    std::function<FeatureBlock(DecorationRegion::Position)> block;
    // BlockSource::getMaterial 0x1c5acf0, compared with Material::isType
    // 0x1c56be0. The host answers "is this position's material the given
    // MaterialType" because that is the only question the features ask.
    std::function<bool(DecorationRegion::Position, int material_type)> material_is;
    // Block::hasProperty(BlockProperty) 0x1e6b6e0.
    std::function<bool(DecorationRegion::Position, int property)> has_property;
    // Feature::_placeBlock 0x1b6ebd0: write with flags 3.
    std::function<void(DecorationRegion::Position, FeatureBlock)> place_block;
};

// TallGrassFeature::place 0x1b66570 -> place(...,int,int) 0x1b663c0.
// The 0x1b66570 overload passes r8 = 0x5a (loop count) and r9 = 0x8 (spread);
// the descent limit is the constant 0x5a.
bool place_tall_grass(DecorationRegion &, DecorationRegion::Position, FeatureBlock grass,
                      Random &, const VegetationEnvironment &);
// TallGrassFeature::place(BlockSource&, BlockPos const&, Random&, int, int)
// 0x1b663c0 itself: `count` attempts (r8) with gaussian(`spread`) x/z (r9).
// OverworldDecorator::decorate 0x1da5926 calls it with (23, 3) after a tree.
bool place_tall_grass(DecorationRegion &, DecorationRegion::Position, FeatureBlock grass,
                      Random &, const VegetationEnvironment &, int count, int spread);

// FlowerFeature::place 0x1b5ef70 -> _placeMushroom 0x1b5efc0.
bool place_flower(DecorationRegion &, DecorationRegion::Position, FeatureBlock flower,
                  Random &, const VegetationEnvironment &);

// PumpkinFeature::place 0x1b52430.
bool place_pumpkin(DecorationRegion &, DecorationRegion::Position, FeatureBlock pumpkin,
                   Random &, const VegetationEnvironment &, FeatureBlock grass);

// CactusFeature::place 0x1b5d3b0.
bool place_cactus(DecorationRegion &, DecorationRegion::Position, FeatureBlock cactus,
                  Random &, const VegetationEnvironment &);

// WaterlilyFeature::place 0x1b53000.
bool place_waterlily(DecorationRegion &, DecorationRegion::Position, FeatureBlock lily,
                     Random &, const VegetationEnvironment &);

// ReedsFeature::place 0x1b5d070.
bool place_reed(DecorationRegion &, DecorationRegion::Position, FeatureBlock reed,
                Random &, const VegetationEnvironment &);

// VinesFeature::place 0x1b531a0. Draws no Random value.
bool place_vine(DecorationRegion &, DecorationRegion::Position, FeatureBlock vine,
                Random &, const VegetationEnvironment &, int max_height);
} // namespace bds
