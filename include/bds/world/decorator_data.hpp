#pragma once
// Per-biome OverworldDecorator data of the original (BDS 1.6.0.15, Linux),
// generated into src/world/decorator_data.cpp by tools/re/make_decorator_table.py.
#include <vector>

namespace bds {
struct DecoratorData {
    int biome;
    const char *klass;
    int waterlily;       // +0x1e0
    float trees;         // +0x1e4
    int flowers;         // +0x1e8
    int grass;           // +0x1ec
    int dead_bushes;     // +0x1f0
    int mushrooms;       // +0x1f4
    int reeds;           // +0x1f8
    int cactus;          // +0x1fc
    int gravel;          // +0x200
    int sand;            // +0x204
    int clay;            // +0x208
    int field_20c, field_210;
    int huge_mushrooms;  // +0x214
    int field_218;
    bool springs;        // +0x21c
    int subclass;        // +0x228
    int clay_size;       // ClayFeature (+0x8) +0x18
    int sand_radius;     // SandFeature (+0x10) +0x20
    int gravel_radius;   // SandFeature (+0x18) +0x20
};
const std::vector<DecoratorData> &decorator_table();
// The decorator of a biome id (nullptr for biomes without one).
const DecoratorData *decorator_data(int biome);
} // namespace bds
