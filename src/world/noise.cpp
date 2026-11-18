#include "bds/world/noise.hpp"
#include <cmath>
#include <cstring>

namespace bds {
namespace {
// ImprovedNoise::_calcValues 0x1b77950: integer cell (floor, & 255), fade,
// and the fractional part.
float calc_values(float v, int &cell, float &fade) {
    int i = static_cast<int>(v);
    if (static_cast<float>(i) > v)
        --i;
    cell = i & 255;
    v -= static_cast<float>(i);
    fade = v * v * v * (v * (v * 6.0f - 15.0f) + 10.0f);
    return v;
}
// ImprovedNoise::_lerp 0x1b77de0: a + t * (b - a).
float lerp(float t, float a, float b) { return a + t * (b - a); }
// ImprovedNoise::_grad 0x1b78110: Vec3::dot with the uvMask table (x*a + y*b)
// + z*c. _grad2 0x1b77e20 is the same table with y = 0.
constexpr float kMask[16][3] = {{1, 1, 0},  {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                {1, 0, -1}, {-1, 0, -1}, {0, 1, 1},  {0, -1, 1},  {0, 1, -1}, {0, -1, -1},
                                {1, 1, 0},  {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};
float grad(int hash, float x, float y, float z) {
    const auto &m = kMask[hash & 15];
    return x * m[0] + y * m[1] + z * m[2];
}
// SimplexNoise::SQRT3 / F2 / G2 (static initialisers 0x1772880..0x17728e0).
const float kSqrt3 = static_cast<float>(std::sqrt(static_cast<double>(3.0f)));
const float kF2 = 0.5f * (kSqrt3 - 1.0f);
const float kG2 = (3.0f - kSqrt3) / 6.0f;
// grad3 table 0x2833f70 (int triples).
constexpr int kGrad3[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                               {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
int fast_floor(float v) { return v > 0.0f ? static_cast<int>(v) : static_cast<int>(v) - 1; } // 0x1b76050
float dot2(const int *g, float x, float y) {                                                 // 0x1b76090
    return static_cast<float>(g[0]) * x + static_cast<float>(g[1]) * y;
}
void init_permutation(std::array<int, 512> &p, Random &random) {
    for (int i = 0; i < 256; ++i)
        p[i] = i;
    for (int i = 0; i < 256; ++i) {
        const int j = random.next_int(256 - i) + i;
        std::swap(p[i], p[j]);
        p[i + 256] = p[i];
    }
}
} // namespace

ImprovedNoise::ImprovedNoise(Random &random) {
    // _init 0x1b77650.
    origin_.x = random.next_float() * 256.0f;
    origin_.y = random.next_float() * 256.0f;
    origin_.z = random.next_float() * 256.0f;
    init_permutation(p_, random);
}

void ImprovedNoise::blend_corners(NoiseVec3 f, int x, int y, int z, float u, float &o1, float &o2, float &o3,
                                  float &o4) const {
    // _blendCubeCorners 0x1b77a00.
    const int a = p_[x] + y, aa = p_[a] + z, ab = p_[a + 1] + z;
    const int b = p_[x + 1] + y, ba = p_[b] + z, bb = p_[b + 1] + z;
    const float x1 = f.x - 1.0f, y1 = f.y - 1.0f, z1 = f.z - 1.0f;
    o1 = lerp(u, grad(p_[aa], f.x, f.y, f.z), grad(p_[ba], x1, f.y, f.z));
    o2 = lerp(u, grad(p_[ab], f.x, y1, f.z), grad(p_[bb], x1, y1, f.z));
    o3 = lerp(u, grad(p_[aa + 1], f.x, f.y, z1), grad(p_[ba + 1], x1, f.y, z1));
    o4 = lerp(u, grad(p_[ab + 1], f.x, y1, z1), grad(p_[bb + 1], x1, y1, z1));
}

float ImprovedNoise::value(NoiseVec3 pos) const {
    // _noise 0x1b777c0: (pos + origin) per axis, corners, then y and z lerps.
    NoiseVec3 v{pos.x + origin_.x, pos.y + origin_.y, pos.z + origin_.z};
    int x, y, z;
    float u, w, t;
    v.x = calc_values(v.x, x, u);
    v.y = calc_values(v.y, y, w);
    v.z = calc_values(v.z, z, t);
    float o1, o2, o3, o4;
    blend_corners(v, x, y, z, u, o1, o2, o3, o4);
    return lerp(t, lerp(w, o1, o2), lerp(w, o3, o4));
}

void ImprovedNoise::read_area(float *buffer, NoiseVec3 pos, int sx, int sy, int sz, NoiseVec3 scale,
                              float pow) const {
    int index = 0;
    const float inverse = 1.0f / pow;
    if (sy == 1) {
        for (int ix = 0; ix < sx; ++ix) {
            int x;
            float u;
            const float fx = calc_values((pos.x + static_cast<float>(ix)) * scale.x + origin_.x, x, u);
            for (int iz = 0; iz < sz; ++iz) {
                int z;
                float t;
                const float fz = calc_values((pos.z + static_cast<float>(iz)) * scale.z + origin_.z, z, t);
                const int a = p_[x] + 0, aa = p_[a] + z, b = p_[x + 1] + 0, ba = p_[b] + z;
                const float v1 = lerp(u, grad(p_[aa], fx, 0.0f, fz), grad(p_[ba], fx - 1.0f, 0.0f, fz));
                const float v2 = lerp(u, grad(p_[aa + 1], fx, 0.0f, fz - 1.0f),
                                      grad(p_[ba + 1], fx - 1.0f, 0.0f, fz - 1.0f));
                const float result = lerp(t, v1, v2);
                buffer[index] = result * inverse + buffer[index];
                ++index;
            }
        }
        return;
    }
    // The corners are cached while the y cell stays the same (computed with
    // the fractional y of the first sample in that cell), as in the original.
    int last_y = -1;
    float o1 = 0, o2 = 0, o3 = 0, o4 = 0;
    for (int ix = 0; ix < sx; ++ix) {
        int x;
        float u;
        const float fx = calc_values((pos.x + static_cast<float>(ix)) * scale.x + origin_.x, x, u);
        for (int iz = 0; iz < sz; ++iz) {
            int z;
            float t;
            const float fz = calc_values((pos.z + static_cast<float>(iz)) * scale.z + origin_.z, z, t);
            for (int iy = 0; iy < sy; ++iy) {
                int y;
                float w;
                const float fy = calc_values((pos.y + static_cast<float>(iy)) * scale.y + origin_.y, y, w);
                if (iy == 0 || y != last_y) {
                    last_y = y;
                    blend_corners({fx, fy, fz}, x, y, z, u, o1, o2, o3, o4);
                }
                const float result = lerp(t, lerp(w, o1, o2), lerp(w, o3, o4));
                buffer[index] = result * inverse + buffer[index];
                ++index;
            }
        }
    }
}

PerlinNoise::PerlinNoise(Random &random, int levels, int min_level) : levels_(levels), min_level_(min_level) {
    // PerlinNoise(Random&, int, int) 0x1b73780 -> _init 0x1b735f0.
    noises_.reserve(static_cast<std::size_t>(levels));
    for (int i = 0; i < levels; ++i)
        noises_.emplace_back(random);
}

float PerlinNoise::value(NoiseVec3 pos) const {
    // getValue 0x1b737d0.
    float total = 0.0f, pow = 1.0f;
    int i = 0;
    for (; i < min_level_; ++i)
        pow /= 2.0f;
    for (; i < levels_; ++i) {
        total = noises_[static_cast<std::size_t>(i)].value({pos.x * pow, pos.y * pow, pos.z * pow}) / pow + total;
        pow /= 2.0f;
    }
    return total;
}

void PerlinNoise::region(float *buffer, NoiseVec3 pos, int sx, int sy, int sz, NoiseVec3 scale) const {
    // getRegion(Vec3) 0x1b738f0.
    std::memset(buffer, 0, sizeof(float) * static_cast<std::size_t>(sx * sy * sz));
    float pow = 1.0f;
    int i = 0;
    for (; i < min_level_; ++i)
        pow /= 2.0f;
    for (; i < levels_; ++i) {
        noises_[static_cast<std::size_t>(i)].read_area(buffer, pos, sx, sy, sz,
                                                       {scale.x * pow, scale.y * pow, scale.z * pow}, pow);
        pow /= 2.0f;
    }
}

void PerlinNoise::region2d(float *buffer, float x, float z, int sx, int sz, float scale_x, float scale_z) const {
    region(buffer, {x, 10.0f, z}, sx, 1, sz, {scale_x, 1.0f, scale_z});
}

SimplexNoise::SimplexNoise(Random &random) {
    // 0x1b74960: Vec3(third draw, second draw, first draw).
    const float first = random.next_float() * 256.0f;
    const float second = random.next_float() * 256.0f;
    const float third = random.next_float() * 256.0f;
    origin_ = {third, second, first};
    init_permutation(p_, random);
}

float SimplexNoise::value2d(float x, float y) const {
    // _getValue(Vec2) 0x1b74b40 with Vec3 helpers (z unused).
    const float s = (x + y) * kF2;
    const int i = fast_floor(x + s), j = fast_floor(y + s);
    const float t = static_cast<float>(i + j) * kG2;
    const float x0 = x - (static_cast<float>(i) - t), y0 = y - (static_cast<float>(j) - t);
    const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
    const float x1 = (x0 - static_cast<float>(i1)) + kG2, y1 = (y0 - static_cast<float>(j1)) + kG2;
    const float g2x2 = kG2 + kG2;
    const float x2 = (x0 - 1.0f) + g2x2, y2 = (y0 - 1.0f) + g2x2;
    const int ii = i & 255, jj = j & 255;
    const int gi0 = p_[ii + p_[jj]] % 12;
    const int gi1 = p_[ii + i1 + p_[jj + j1]] % 12;
    const int gi2 = p_[ii + 1 + p_[jj + 1]] % 12;
    const auto corner = [](float cx, float cy, int g) {
        const float c = 0.5f - cx * cx - cy * cy;
        if (0.0f > c)
            return 0.0f;
        return c * c * c * c * dot2(kGrad3[g], cx, cy);
    };
    const float n0 = corner(x0, y0, gi0), n1 = corner(x1, y1, gi1), n2 = corner(x2, y2, gi2);
    return 70.0f * (n0 + n1 + n2);
}

void SimplexNoise::add2d(float *buffer, float x, float y, int w, int h, float scale_x, float scale_y,
                         float amp) const {
    int index = 0;
    for (int iy = 0; iy < h; ++iy) {
        const float vy = (y + static_cast<float>(iy)) * scale_y + origin_.y;
        for (int ix = 0; ix < w; ++ix) {
            const float vx = (x + static_cast<float>(ix)) * scale_x + origin_.x;
            buffer[index] = value2d(vx, vy) * amp + buffer[index];
            ++index;
        }
    }
}

PerlinSimplexNoise::PerlinSimplexNoise(Random &random, int levels) : levels_(levels) {
    // _init 0x1b76180: the normalisation 1 / sum(1 / 2^-i).
    noises_.reserve(static_cast<std::size_t>(levels));
    for (int i = 0; i < levels; ++i)
        noises_.emplace_back(random);
    float pow = 1.0f, sum = 0.0f;
    for (int i = 0; i < levels; ++i) {
        sum = 1.0f / pow + sum;
        pow /= 2.0f;
    }
    normalize_ = 1.0f / sum;
}

float PerlinSimplexNoise::value2d(float x, float y) const {
    float total = 0.0f, pow = 1.0f;
    for (int i = 0; i < levels_; ++i) {
        total = noises_[static_cast<std::size_t>(i)].value2d(x * pow, y * pow) / pow + total;
        pow /= 2.0f;
    }
    return total;
}

void PerlinSimplexNoise::region2d(float *buffer, float x, float y, int w, int h, float scale_x, float scale_y,
                                  float freq, float amp) const {
    std::memset(buffer, 0, sizeof(float) * static_cast<std::size_t>(w * h));
    float a = 1.0f, b = 1.0f; // -0x3c (frequency), -0x38 (amplitude)
    for (int i = 0; i < levels_; ++i) {
        noises_[static_cast<std::size_t>(i)].add2d(buffer, x, y, w, h, scale_x * a * b, scale_y * a * b, 0.55f / b);
        a = freq * a;
        b = amp * b;
    }
}
} // namespace bds
