#pragma once
// Terrain noise translated from BDS 1.6.0.15 (Linux): ImprovedNoise,
// PerlinNoise, SimplexNoise and PerlinSimplexNoise. Everything is single
// precision as in the original (Vec3/Vec2 of floats); addresses are cited in
// noise.cpp.
#include "bds/world/random.hpp"
#include <array>
#include <vector>

namespace bds {
struct NoiseVec3 {
    float x = 0, y = 0, z = 0;
};

// ImprovedNoise (0x1b77600..0x1b78b10): origin (+0x0) and permutation (+0xc).
class ImprovedNoise {
  public:
    explicit ImprovedNoise(Random &random);
    float value(NoiseVec3 pos) const; // _getValue -> _noise
    // _readArea 0x1b78400: adds noise * (1 / pow) for x, z, y (y innermost);
    // y == 1 takes the 2D branch.
    void read_area(float *buffer, NoiseVec3 pos, int sx, int sy, int sz, NoiseVec3 scale, float pow) const;

  private:
    void blend_corners(NoiseVec3 frac, int x, int y, int z, float fade_x, float &a, float &b, float &c,
                       float &d) const;
    NoiseVec3 origin_;
    std::array<int, 512> p_{};
};

// PerlinNoise (0x1b735f0..0x1b73b60): `levels` octaves, the first `min_level`
// of them skipped.
class PerlinNoise {
  public:
    PerlinNoise(Random &random, int levels, int min_level = 0);
    float value(NoiseVec3 pos) const;
    void region(float *buffer, NoiseVec3 pos, int sx, int sy, int sz, NoiseVec3 scale) const;
    // getRegion(Vec2) 0x1b73aa0: (x, 10, z) with scale (sx, 1, sz) and sy 1.
    void region2d(float *buffer, float x, float z, int sx, int sz, float scale_x, float scale_z) const;

  private:
    int levels_, min_level_;
    std::vector<ImprovedNoise> noises_;
};

// SimplexNoise (0x1b74960..0x1b760d0): origin from three nextFloat * 256
// passed as Vec3(third, second, first), permutation as ImprovedNoise.
class SimplexNoise {
  public:
    explicit SimplexNoise(Random &random);
    float value2d(float x, float y) const; // _getValue(Vec2) 0x1b74b40
    // _add(float*, float, float, int, int, float, float, float) 0x1b75c20.
    void add2d(float *buffer, float x, float y, int w, int h, float scale_x, float scale_y, float amp) const;

  private:
    NoiseVec3 origin_;
    std::array<int, 512> p_{};
};

class PerlinSimplexNoise {
  public:
    PerlinSimplexNoise(Random &random, int levels);
    float value2d(float x, float y) const; // getValue(float, float) 0x1b762d0
    // getRegion(float*, Vec2, int, int, Vec2, float, float) 0x1b76510.
    void region2d(float *buffer, float x, float y, int w, int h, float scale_x, float scale_y, float freq,
                  float amp) const;
    float normalize() const { return normalize_; }

  private:
    int levels_;
    std::vector<SimplexNoise> noises_;
    float normalize_ = 1.0f; // +0x20
};
} // namespace bds
