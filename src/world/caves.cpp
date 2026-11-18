// The overworld carvers translated from BDS 1.6.0.15 (Linux):
// LargeCaveFeature (0x1b2e7c0..0x1b301b0), CanyonFeature (0x1b497b0..0x1b4ae20)
// and their Underwater subclasses, which override only carve (and
// isDiggable). loadChunk 0x1b38610 calls LargeCaveFeature::apply /
// CanyonFeature::apply directly for each of the four features; addTunnel
// reaches the subclass through the virtual carve (vtable +0x10) and
// CanyonFeature::apply reaches addFeature through vtable +0x18.
//
// Float/double conversions follow the original instruction by instruction:
// the tunnel state is single precision, the trigonometry is done in double
// (sin/cos of the float widened) and stored back as float.
#include "bds/world/overworld.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace bds {
namespace {
constexpr float kPi = std::bit_cast<float>(0x40490fdbU); // mce::Math::PI 0x28784c8

bool legacy_is(const TerrainBlock &b, const TerrainBlock &type) { return std::strcmp(b.name, type.name) == 0; }

// mce::Math::clamp(int, int, int) 0x1818730.
int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Random::nextGaussianFloat 0x1b30420: nextFloat() - nextFloat().
float next_gaussian_float(Random &random) {
    const float a = random.next_float();
    return a - random.next_float();
}

struct Vec3f {
    float x, y, z;
};
struct ChunkPosI {
    int x, z;
};

enum class Kind { LargeCave, UnderwaterLargeCave, Canyon, UnderwaterCanyon };

// BlockVolume (16 x 128 x 16) over the TerrainVolume: index (x * 16 + z) * 128 + y.
struct Volume {
    TerrainVolume &v;
    TerrainBlock &block(int index) { return v.blocks[static_cast<std::size_t>(index)]; }
    static int index(int x, int y, int z) { return (x * 16 + z) * TerrainVolume::height + y; } // 0x1b30380
};

class Carver {
  public:
    Carver(Kind kind, const BiomeLayers &layers, std::uint32_t seed) : kind_(kind), layers_(layers), seed_(seed) {}

    // LargeCaveFeature::apply 0x1b2e7c0 / CanyonFeature::apply 0x1b497b0
    // (identical apart from the addFeature they call).
    void apply(Volume &volume, ChunkPosI chunk, Random &random) const {
        random.set_seed(seed_); // BiomeSource::getSeed
        const int a = random.next_int() / 2 * 2 + 1;
        const int b = random.next_int() / 2 * 2 + 1;
        for (int x = chunk.x - 8; x <= chunk.x + 8; ++x) {
            const int ax = static_cast<int>(static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(a));
            for (int z = chunk.z - 8; z <= chunk.z + 8; ++z) {
                const auto mixed = static_cast<std::uint32_t>(ax) +
                                   static_cast<std::uint32_t>(z) * static_cast<std::uint32_t>(b);
                random.set_seed(mixed ^ seed_);
                if (canyon())
                    canyon_add_feature(volume, chunk, random, x, z);
                else
                    cave_add_feature(volume, chunk, random, x, z);
            }
        }
    }

  private:
    bool canyon() const { return kind_ == Kind::Canyon || kind_ == Kind::UnderwaterCanyon; }
    bool underwater() const { return kind_ == Kind::UnderwaterLargeCave || kind_ == Kind::UnderwaterCanyon; }

    // ---- LargeCaveFeature -------------------------------------------------

    // LargeCaveFeature::addFeature 0x1b2e920.
    void cave_add_feature(Volume &volume, ChunkPosI chunk, Random &random, int x, int z) const {
        const int n1 = random.next_int(40) + 1;
        const int n2 = random.next_int(n1) + 1;
        int count = random.next_int(n2);
        if (random.next_int(15) != 0)
            count = 0;
        for (int i = 0; i < count; ++i) {
            const int bz = z * 16 + random.next_int(16);
            const int by = random.next_int(random.next_int(120) + 8);
            const int bx = x * 16 + random.next_int(16);
            const Vec3f pos{static_cast<float>(bx), static_cast<float>(by), static_cast<float>(bz)};
            int tunnels = 1;
            if (random.next_int(4) == 0) {
                cave_add_room(volume, random, chunk, pos, pos);
                tunnels += random.next_int(4);
            }
            for (int t = 0; t < tunnels; ++t) {
                const float yaw = random.next_float() * kPi * 2.0f;
                const float pitch = (random.next_float() - 0.5f) * 2.0f / 8.0f;
                const float t1 = random.next_float() * 2.0f;
                const float thickness = t1 + random.next_float();
                cave_add_tunnel(volume, random, chunk, pos, pos, thickness, yaw, pitch, 0, 0, 1.0f);
            }
        }
    }

    // LargeCaveFeature::addRoom 0x1b2ebd0.
    void cave_add_room(Volume &volume, Random &random, ChunkPosI chunk, const Vec3f &pos, const Vec3f &origin) const {
        const float thickness = 1.0f + random.next_float() * 6.0f;
        cave_add_tunnel(volume, random, chunk, pos, origin, thickness, 0.0f, 0.0f, -1, -1, 0.5f);
    }

    // LargeCaveFeature::addTunnel 0x1b2ecc0.
    void cave_add_tunnel(Volume &volume, Random &random, ChunkPosI chunk, const Vec3f &start, const Vec3f &origin,
                         float thickness, float yaw, float pitch, int step, int max_step, float y_scale) const {
        Vec3f p = start;
        float d_yaw = 0.0f, d_pitch = 0.0f; // -0x8c, -0x90
        Random local(static_cast<std::uint32_t>(random.next_int()));
        if (max_step <= 0) {
            const int range = 112;
            max_step = range - local.next_int(range / 4);
        }
        bool room = false;
        if (step == -1) {
            step = max_step / 2;
            room = true;
        }
        const int split = local.next_int(max_step / 2) + max_step / 4;
        const bool steep = local.next_int(6) == 0;
        for (; step < max_step; ++step) {
            const float r = static_cast<float>(
                1.5 + std::sin(static_cast<double>(static_cast<float>(step) * kPi / static_cast<float>(max_step))) *
                          static_cast<double>(thickness) * 1.0);
            const float ry = r * y_scale;
            const float cp = static_cast<float>(std::cos(static_cast<double>(pitch)));
            const float sp = static_cast<float>(std::sin(static_cast<double>(pitch)));
            p.x = static_cast<float>(static_cast<double>(p.x) + std::cos(static_cast<double>(yaw)) * static_cast<double>(cp));
            p.y = sp + p.y;
            p.z = static_cast<float>(static_cast<double>(p.z) + std::sin(static_cast<double>(yaw)) * static_cast<double>(cp));
            if (steep)
                pitch = 0.92f * pitch;
            else
                pitch = 0.7f * pitch;
            pitch = 0.1f * d_pitch + pitch;
            yaw = 0.1f * d_yaw + yaw;
            d_pitch = 0.9f * d_pitch;
            d_yaw = 0.75f * d_yaw;
            float g = next_gaussian_float(local);
            d_pitch = g * local.next_float() * 2.0f + d_pitch;
            g = next_gaussian_float(local);
            d_yaw = g * local.next_float() * 4.0f + d_yaw;
            if (!room && step == split && thickness > 1.0f) {
                float t = local.next_float() * 0.5f + 0.5f;
                cave_add_tunnel(volume, local, chunk, p, origin, t, yaw - kPi / 2.0f, pitch / 3.0f, step, max_step, 1.0f);
                t = local.next_float() * 0.5f + 0.5f;
                cave_add_tunnel(volume, local, chunk, p, origin, t, yaw + kPi / 2.0f, pitch / 3.0f, step, max_step, 1.0f);
                return;
            }
            if (!room && local.next_int(4) == 0)
                continue;
            if (!step_tail(volume, local, chunk, p, origin, thickness, r, ry, step, max_step, room, nullptr))
                return;
        }
    }

    // ---- CanyonFeature ----------------------------------------------------

    // CanyonFeature::addFeature 0x1b4aa80.
    void canyon_add_feature(Volume &volume, ChunkPosI chunk, Random &random, int x, int z) const {
        if (random.next_int(150) != 0)
            return;
        Vec3f pos{};
        pos.x = static_cast<float>(x * 16 + random.next_int(16));
        const int n = random.next_int(40);
        pos.y = static_cast<float>(random.next_int(n + 8) + 20);
        // The original compares an integer draw with 0.2: only a 0 adds 15.
        if (0.2f >= static_cast<float>(random.next_int()))
            pos.y = 15.0f + pos.y;
        pos.z = static_cast<float>(z * 16 + random.next_int(16));
        for (int i = 0; i < 1; ++i) {
            const float yaw = random.next_float() * kPi * 2.0f;
            const float pitch = (random.next_float() - 0.5f) * 2.0f / 8.0f;
            const float t1 = 3.0f * random.next_float();
            float thickness = t1 + random.next_float() * 3.0f;
            float y_scale = 4.0f;
            // CanyonFeature +0x8: true for CanyonFeature (0x1b3ac00), false
            // for UnderwaterCanyonFeature (0x1b42990).
            if (kind_ == Kind::Canyon && 0.05f > random.next_float()) {
                thickness = 2.0f * thickness;
                y_scale = 40.0f;
            }
            canyon_add_tunnel(volume, random, chunk, pos, pos, thickness, yaw, pitch, 0, 0, y_scale);
        }
    }

    // CanyonFeature::addTunnel 0x1b49ad0.
    void canyon_add_tunnel(Volume &volume, Random &random, ChunkPosI chunk, const Vec3f &start, const Vec3f &origin,
                           float thickness, float yaw, float pitch, int step, int max_step, float y_scale) const {
        Vec3f p = start;
        float d_yaw = 0.0f, d_pitch = 0.0f; // -0x94, -0x98
        Random local(static_cast<std::uint32_t>(random.next_int()));
        if (max_step <= 0) {
            const int range = 112;
            max_step = range - local.next_int(range / 4);
        }
        bool room = false;
        if (step == -1) {
            step = max_step / 2;
            room = true;
        }
        std::array<float, 128> rs{};
        float f = 1.0f;
        for (int i = 0; i < 128; ++i) {
            if (i == 0 || local.next_int(3) == 0) {
                const float a = local.next_float();
                f = 1.0f + a * local.next_float();
            }
            rs[static_cast<std::size_t>(i)] = f * f;
        }
        for (; step < max_step; ++step) {
            float r = static_cast<float>(
                1.5 + std::sin(static_cast<double>(static_cast<float>(step) * kPi / static_cast<float>(max_step))) *
                          static_cast<double>(thickness) * 1.0);
            float ry = r * y_scale;
            r = (local.next_float() * 0.25f + 0.75f) * r;
            ry = (local.next_float() * 0.25f + 0.75f) * ry;
            const float cp = static_cast<float>(std::cos(static_cast<double>(pitch)));
            const float sp = static_cast<float>(std::sin(static_cast<double>(pitch)));
            p.x = static_cast<float>(static_cast<double>(p.x) + std::cos(static_cast<double>(yaw)) * static_cast<double>(cp));
            p.y = sp + p.y;
            p.z = static_cast<float>(static_cast<double>(p.z) + std::sin(static_cast<double>(yaw)) * static_cast<double>(cp));
            pitch = 0.7f * pitch;
            pitch = 0.05f * d_pitch + pitch;
            yaw = 0.05f * d_yaw + yaw;
            d_pitch = 0.8f * d_pitch;
            d_yaw = 0.5f * d_yaw;
            float g = next_gaussian_float(local);
            d_pitch = g * local.next_float() * 2.0f + d_pitch;
            g = next_gaussian_float(local);
            d_yaw = g * local.next_float() * 4.0f + d_yaw;
            if (!room && local.next_int(4) == 0)
                continue;
            if (!step_tail(volume, local, chunk, p, origin, thickness, r, ry, step, max_step, room, &rs))
                return;
        }
    }

    // The common tail of both addTunnel loops: the distance cut-off, the
    // chunk range test, the carve box and the virtual carve. Returns false
    // where addTunnel returns: past the reach, when carve fails, and after a
    // successful carve of a room tunnel (step started at -1).
    bool step_tail(Volume &volume, Random &local, ChunkPosI chunk, const Vec3f &p, const Vec3f &origin,
                   float thickness, float r, float ry, int step, int max_step, bool room,
                   const std::array<float, 128> *rs) const {
        const float cx = static_cast<float>(chunk.x * 16 + 8), cz = static_cast<float>(chunk.z * 16 + 8);
        const float dx = p.x - cx, dz = p.z - cz;
        const float left = static_cast<float>(max_step - step);
        const float reach = 2.0f + thickness + 16.0f;
        if (dx * dx + dz * dz - left * left > reach * reach)
            return false;
        if (cx - 16.0f - 2.0f * r > p.x || cz - 16.0f - 2.0f * r > p.z || p.x > 16.0f + cx + 2.0f * r ||
            p.z > 16.0f + cz + 2.0f * r)
            return true; // next step
        int x0 = static_cast<int>(std::floor(static_cast<double>(p.x - r))) - chunk.x * 16 - 1;
        int x1 = static_cast<int>(std::floor(static_cast<double>(p.x + r))) - chunk.x * 16 + 1;
        int y0 = static_cast<int>(std::floor(static_cast<double>(p.y - ry))) - 1;
        int y1 = static_cast<int>(std::floor(static_cast<double>(p.y + ry))) + 1;
        int z0 = static_cast<int>(std::floor(static_cast<double>(p.z - r))) - chunk.z * 16 - 1;
        int z1 = static_cast<int>(std::floor(static_cast<double>(p.z + r))) - chunk.z * 16 + 1;
        if (x0 < 0)
            x0 = 0;
        if (x1 > 16)
            x1 = 16;
        y1 = clamp_int(y1, 1, 120);
        y0 = clamp_int(y0, 1, y1);
        if (z0 < 0)
            z0 = 0;
        if (z1 > 16)
            z1 = 16;
        bool carved;
        switch (kind_) {
        case Kind::LargeCave:
            carved = cave_carve(volume, chunk, p, origin, x0, x1, y0, y1, z0, z1, r, ry);
            break;
        case Kind::UnderwaterLargeCave:
            carved = underwater_cave_carve(volume, local, chunk, p, origin, x0, x1, y0, y1, z0, z1, r, ry);
            break;
        case Kind::Canyon:
            carved = canyon_carve(volume, chunk, p, origin, x0, x1, y0, y1, z0, z1, r, ry, *rs);
            break;
        default:
            carved = underwater_canyon_carve(volume, local, chunk, p, origin, x0, x1, y0, y1, z0, z1, r, ry, *rs);
            break;
        }
        return carved && !room;
    }

    // ---- carve ------------------------------------------------------------

    // The origin biome test of every carve: BiomeSource::getBiome(BlockPos(origin))
    // compared with VanillaBiomeTypes::Ocean (the type of biome 0).
    bool origin_is_ocean(const Vec3f &origin) const {
        const int id = biome_at(layers_, static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.z)));
        const auto *b = biome(id & 255);
        return b && b->type == biome(0)->type;
    }

    // LargeCaveFeature::isDiggable 0x1b2f850.
    static bool diggable(const TerrainBlock &b, const TerrainBlock &above) {
        using namespace terrain_block;
        if (legacy_is(b, stone) || legacy_is(b, dirt) || legacy_is(b, grass) || legacy_is(b, hardened_clay) ||
            legacy_is(b, stained_hardened_clay) || legacy_is(b, sandstone) || legacy_is(b, red_sandstone) ||
            legacy_is(b, mycelium) || legacy_is(b, podzol))
            return true;
        return legacy_is(b, sand) && !legacy_is(above, water);
    }

    // UnderwaterLargeCaveFeature::isDiggable 0x1b43290.
    static bool underwater_diggable(const TerrainBlock &b) {
        using namespace terrain_block;
        for (const auto *t : {&stone, &dirt, &grass, &hardened_clay, &stained_hardened_clay, &sandstone, &red_sandstone,
                              &mycelium, &podzol, &sand, &gravel, &water, &flowing_water, &lava, &flowing_lava,
                              &obsidian, &air})
            if (legacy_is(b, *t))
                return true;
        return false;
    }

    // LargeCaveFeature::detectWater 0x1b2fa30 (CanyonFeature::detectWater
    // 0x1b49930 is the same code): flowing or still water on the shell of the
    // box; interior cells jump to y0 - 1.
    static bool detect_water(Volume &volume, int x0, int x1, int y0, int y1, int z0, int z1) {
        for (int x = x0; x < x1; ++x)
            for (int z = z0; z < z1; ++z)
                for (int y = y1 + 1; y >= y0 - 1; --y) {
                    const auto &b = volume.block(Volume::index(x, y, z));
                    if (legacy_is(b, terrain_block::flowing_water) || legacy_is(b, terrain_block::water))
                        return true;
                    if (y != y0 - 1 && x != x0 && x != x1 - 1 && z != z0 && z != z1 - 1)
                        y = y0;
                }
        return false;
    }

    // LargeCaveFeature::_thinSand 0x1b2f730 on the span starting at `index`.
    static bool thin_sand(Volume &volume, int index, int height, int y) {
        if (y >= height - 3)
            return false;
        const auto &s1 = volume.block(index + 1), &s2 = volume.block(index + 2), &s3 = volume.block(index + 3);
        return legacy_is(s1, terrain_block::sand) && legacy_is(s1, s2) && legacy_is(s2, s3);
    }

    // The block written for water carvers at y >= 10 (9 for the cave):
    // UnderwaterLargeCaveFeature::carve 0x1b438ef / UnderwaterCanyonFeature::carve
    // 0x1b42dce. ChunkBlockPos(unsigned char) 0x1b3b9d0 is built from the low
    // byte of the volume index, so x, z and y all equal that byte; its
    // horizontal neighbours (Facing::BY2DDATA = north, south, west, east)
    // that are inside the volume and air make the cell flowing water.
    static void water_or_flowing(Volume &volume, int index) {
        static constexpr int kStep[4][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}; // Facing::DIRECTION
        const auto byte = static_cast<std::uint8_t>(index);
        for (const auto &d : kStep) {
            const auto nx = static_cast<std::uint8_t>(byte + d[0]);
            const auto ny = static_cast<std::int16_t>(byte + d[1]);
            const auto nz = static_cast<std::uint8_t>(byte + d[2]);
            if (!(nx < 16 && ny >= 0 && ny < TerrainVolume::height && nz < 16)) // BlockVolume::isInBounds 0x1b43090
                continue;
            if (legacy_is(volume.block((nx * 16 + nz) * TerrainVolume::height + ny), terrain_block::air)) {
                volume.block(index) = terrain_block::flowing_water;
                return;
            }
        }
        volume.block(index) = terrain_block::water;
    }

    // LargeCaveFeature::carve 0x1b2fbd0. The cell at local y is the block at
    // index(x, y + 1, z) (the index starts at y1 while y starts at y1 - 1).
    bool cave_carve(Volume &volume, ChunkPosI chunk, const Vec3f &p, const Vec3f &origin, int x0, int x1, int y0,
                    int y1, int z0, int z1, float r, float ry) const {
        if (detect_water(volume, x0, x1, y0, y1, z0, z1))
            return true;
        bool checked = false;
        for (int x = x0; x < x1; ++x) {
            const float dx = (static_cast<float>(x + chunk.x * 16) + 0.5f - p.x) / r;
            const float dx2 = dx * dx;
            for (int z = z0; z < z1; ++z) {
                const float dz = (static_cast<float>(z + chunk.z * 16) + 0.5f - p.z) / r;
                const float dz2 = dz * dz;
                bool grass = false;
                if (!(1.0f > dx2 + dz2))
                    continue;
                int index = Volume::index(x, y1, z);
                for (int y = y1 - 1; y >= y0; --y, --index) {
                    const float dy = (static_cast<float>(y) + 0.5f - p.y) / ry;
                    if (!(static_cast<double>(dy) > -0.7) || !(1.0f > dx2 + dy * dy + dz2))
                        continue;
                    auto &cur = volume.block(index);
                    auto &above = volume.block(index + 1);
                    if (legacy_is(cur, terrain_block::grass))
                        grass = true;
                    if (!diggable(cur, above))
                        continue;
                    if (!checked && origin_is_ocean(origin))
                        return false;
                    checked = true;
                    if (y < 10) {
                        cur = terrain_block::lava;
                        continue;
                    }
                    if (thin_sand(volume, index, TerrainVolume::height, y)) {
                        above = terrain_block::sandstone;
                        if (volume.block(Volume::index(x, y + 1, z)).data == 1)
                            above = terrain_block::red_sandstone;
                    }
                    cur = terrain_block::air;
                    if (grass && legacy_is(volume.block(index - 1), terrain_block::dirt))
                        volume.block(index - 1) = terrain_block::grass;
                }
            }
        }
        return true;
    }

    // UnderwaterLargeCaveFeature::carve 0x1b43550.
    bool underwater_cave_carve(Volume &volume, Random &local, ChunkPosI chunk, const Vec3f &p, const Vec3f &origin,
                               int x0, int x1, int y0, int y1, int z0, int z1, float r, float ry) const {
        bool checked = false;
        for (int x = x0; x < x1; ++x) {
            const float dx = (static_cast<float>(x + chunk.x * 16) + 0.5f - p.x) / r;
            const float dx2 = dx * dx;
            for (int z = z0; z < z1; ++z) {
                const float dz = (static_cast<float>(z + chunk.z * 16) + 0.5f - p.z) / r;
                const float dz2 = dz * dz;
                int index = Volume::index(x, y1, z);
                if (!(1.0f > dx2 + dz2))
                    continue;
                for (int y = y1 - 1; y >= y0; --y, --index) {
                    const float dy = (static_cast<float>(y) + 0.5f - p.y) / ry;
                    if (!(static_cast<double>(dy) > -0.7) || !(1.0f > dx2 + dy * dy + dz2) || y >= 62)
                        continue;
                    auto &cur = volume.block(index);
                    if (!underwater_diggable(cur))
                        continue;
                    if (!checked && !origin_is_ocean(origin))
                        return false;
                    checked = true;
                    if (y == 9)
                        cur = 0.25f > local.next_float() ? terrain_block::magma : terrain_block::obsidian;
                    else if (y < 9)
                        cur = terrain_block::lava;
                    else
                        water_or_flowing(volume, index);
                }
            }
        }
        return true;
    }

    // CanyonFeature::carve 0x1b4a560.
    bool canyon_carve(Volume &volume, ChunkPosI chunk, const Vec3f &p, const Vec3f &origin, int x0, int x1, int y0,
                      int y1, int z0, int z1, float r, float ry, const std::array<float, 128> &rs) const {
        if (detect_water(volume, x0, x1, y0, y1, z0, z1))
            return true;
        bool checked = false;
        for (int x = x0; x < x1; ++x) {
            const float dx = (static_cast<float>(x + chunk.x * 16) + 0.5f - p.x) / r;
            const float dx2 = dx * dx;
            for (int z = z0; z < z1; ++z) {
                const float dz = (static_cast<float>(z + chunk.z * 16) + 0.5f - p.z) / r;
                const float dz2 = dz * dz;
                bool grass = false;
                if (!(1.0f > dx2 + dz2))
                    continue;
                int index = Volume::index(x, y1, z);
                for (int y = y1 - 1; y >= y0; --y, --index) {
                    const float dy = (static_cast<float>(y) + 0.5f - p.y) / ry;
                    const float h = dx2 + dz2;
                    if (!(1.0f > h * rs[static_cast<std::size_t>(y - 1)] + dy * dy / 6.0f))
                        continue;
                    auto &cur = volume.block(index);
                    auto &above = volume.block(index + 1);
                    if (legacy_is(cur, terrain_block::grass))
                        grass = true;
                    if (!diggable(cur, above))
                        continue;
                    if (!checked && origin_is_ocean(origin))
                        return false;
                    checked = true;
                    if (y < 10) {
                        cur = terrain_block::lava;
                        continue;
                    }
                    cur = terrain_block::air;
                    if (grass && legacy_is(volume.block(index - 1), terrain_block::dirt))
                        volume.block(index - 1) = terrain_block::grass;
                }
            }
        }
        return true;
    }

    // UnderwaterCanyonFeature::carve 0x1b429f0.
    bool underwater_canyon_carve(Volume &volume, Random &local, ChunkPosI chunk, const Vec3f &p, const Vec3f &origin,
                                 int x0, int x1, int y0, int y1, int z0, int z1, float r, float ry,
                                 const std::array<float, 128> &rs) const {
        bool checked = false;
        for (int x = x0; x < x1; ++x) {
            const float dx = (static_cast<float>(x + chunk.x * 16) + 0.5f - p.x) / r;
            const float dx2 = dx * dx;
            for (int z = z0; z < z1; ++z) {
                const float dz = (static_cast<float>(z + chunk.z * 16) + 0.5f - p.z) / r;
                const float dz2 = dz * dz;
                if (!(1.0f > dx2 + dz2))
                    continue;
                int index = Volume::index(x, y1, z);
                for (int y = y1 - 1; y >= y0; --y, --index) {
                    const float dy = (static_cast<float>(y) + 0.5f - p.y) / ry;
                    const float h = dx2 + dz2;
                    if (!(1.0f > h * rs[static_cast<std::size_t>(y - 1)] + dy * dy / 6.0f) || y >= 62)
                        continue;
                    auto &cur = volume.block(index);
                    if (!underwater_diggable(cur))
                        continue;
                    if (!checked && !origin_is_ocean(origin))
                        return false;
                    checked = true;
                    if (y < 10) {
                        const float f = local.next_float();
                        if (0.0625f > f)
                            cur = terrain_block::magma;
                        else if (0.25f > f)
                            cur = terrain_block::stone;
                        else
                            cur = terrain_block::obsidian;
                    } else
                        water_or_flowing(volume, index);
                }
            }
        }
        return true;
    }

    Kind kind_;
    const BiomeLayers &layers_;
    std::uint32_t seed_;
};
} // namespace

void OverworldGenerator::carve(TerrainVolume &terrain, int chunk_x, int chunk_z, Random &random) const {
    Volume volume{terrain};
    const ChunkPosI chunk{chunk_x, chunk_z};
    for (const Kind kind : {Kind::UnderwaterLargeCave, Kind::UnderwaterCanyon, Kind::LargeCave, Kind::Canyon})
        Carver(kind, layers_, seed_).apply(volume, chunk, random);
}
} // namespace bds
