#include "bds/world/overworld.hpp"
#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace bds {
bool TerrainBlock::operator==(const TerrainBlock &o) const {
    return data == o.data && std::strcmp(name, o.name) == 0;
}
TerrainBlock parse_terrain_block(const char *text) {
    static std::map<std::string, std::string> names;
    const std::string s(text);
    const auto colon = s.find(':');
    const auto &name = names.try_emplace(s.substr(0, colon), s.substr(0, colon)).first->second;
    return {name.c_str(), static_cast<std::uint16_t>(colon == std::string::npos ? 0 : std::stoi(s.substr(colon + 1)))};
}

namespace {
float math_sqrt(float v) { return static_cast<float>(std::sqrt(static_cast<double>(v))); } // mce::Math::sqrt 0x26dd990
// mce::Math::clampedLerp 0x26de380.
float clamped_lerp(float a, float b, float t) {
    if (0.0f > t)
        return a;
    if (t > 1.0f)
        return b;
    return a + (b - a) * t;
}
constexpr int kSeaLevel = 63; // Dimension::getSeaLevel
} // namespace

OverworldGenerator::OverworldGenerator(std::uint32_t seed) : seed_(seed), layers_(overworld_layers(seed)) {
    // OverworldGenerator::OverworldGenerator 0x1b36090: one Random(seed) for
    // all the noises, in this order.
    Random random(seed);
    min_limit_ = std::make_unique<PerlinNoise>(random, 16);
    max_limit_ = std::make_unique<PerlinNoise>(random, 16);
    main_ = std::make_unique<PerlinNoise>(random, 8);
    surface_ = std::make_unique<PerlinSimplexNoise>(random, 4);
    scale_ = std::make_unique<PerlinNoise>(random, 10);
    depth_ = std::make_unique<PerlinNoise>(random, 16);
    forest_ = std::make_unique<PerlinNoise>(random, 8);
    Random swamp(2345); // OverworldDecorator::OverworldDecorator 0x1da31e0
    swamp_noise_ = std::make_unique<PerlinSimplexNoise>(swamp, 1);
    // Biome weights: 10 / sqrt(dx * dx + dz * dz + 0.2) for dx, dz in [-2, 2].
    for (int a = -2; a <= 2; ++a)
        for (int b = -2; b <= 2; ++b)
            biome_weights_[static_cast<std::size_t>((a + 2) + (b + 2) * 5)] =
                10.0f / math_sqrt(static_cast<float>(a * a + b * b) + 0.2f);
}

void OverworldGenerator::heights(float *out, const std::array<int, 100> &biomes, int x, int y, int z) const {
    // getHeights 0x1b373d0.
    float depth_buffer[25], main_buffer[425], min_buffer[425], max_buffer[425];
    depth_->region2d(depth_buffer, static_cast<float>(x), static_cast<float>(z), 5, 5, 200.0f, 200.0f);
    const NoiseVec3 at{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    main_->region(main_buffer, at, 5, 17, 5,
                  {std::bit_cast<float>(0x4108e1e5U), std::bit_cast<float>(0x4088e1e5U), std::bit_cast<float>(0x4108e1e5U)});
    min_limit_->region(min_buffer, at, 5, 17, 5,
                       {std::bit_cast<float>(0x442b1a5eU), std::bit_cast<float>(0x4455e0f6U),
                        std::bit_cast<float>(0x442b1a5eU)});
    max_limit_->region(max_buffer, at, 5, 17, 5,
                       {std::bit_cast<float>(0x442b1a5eU), std::bit_cast<float>(0x442b1a5eU),
                        std::bit_cast<float>(0x442b1a5eU)});
    int index = 0, column = 0;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j) {
            float scale_sum = 0.0f, depth_sum = 0.0f, weight_sum = 0.0f;
            const auto *centre = biome(biomes[static_cast<std::size_t>((i + 2) + (j + 2) * 10)]);
            // range_incl(-1, 1) (radius -0x165c = 1): a 3 x 3 blend with the
            // inner weights of the 5 x 5 table; dx outer, dz inner.
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto *b = biome(biomes[static_cast<std::size_t>((i + dx + 2) + (j + dz + 2) * 10)]);
                    const float depth = b->depth, scale = b->scale;
                    float weight = biome_weights_[static_cast<std::size_t>((dx + 2) + (dz + 2) * 5)] / (depth + 2.0f);
                    if (b->depth > centre->depth)
                        weight /= 2.0f;
                    scale_sum = scale * weight + scale_sum;
                    depth_sum = depth * weight + depth_sum;
                    weight_sum = weight + weight_sum;
                }
            float scale = scale_sum / weight_sum, depth = depth_sum / weight_sum;
            scale = 0.9f * scale + 0.1f;
            depth = (4.0f * depth - 1.0f) / 8.0f;
            float noise = depth_buffer[column++] / 8000.0f;
            if (0.0f > noise)
                noise = -noise * 0.3f;
            noise = 3.0f * noise - 2.0f;
            if (0.0f > noise) {
                noise /= 2.0f;
                if (-1.0f > noise)
                    noise = -1.0f;
                noise = noise / 1.4f;
                noise = noise / 2.0f;
            } else {
                if (noise > 1.0f)
                    noise = 1.0f;
                noise /= 8.0f;
            }
            depth = 0.2f * noise + depth;
            depth = 8.5f * depth / 8.0f;
            const float base = 8.5f + 4.0f * depth;
            for (int k = 0; k < 17; ++k) {
                float offset = (static_cast<float>(k) - base) * 12.0f * 128.0f / 128.0f / scale;
                if (0.0f > offset)
                    offset = 4.0f * offset;
                const float low = min_buffer[index] / 256.0f;
                const float high = max_buffer[index] / 512.0f;
                const float t = (main_buffer[index] / 10.0f + 1.0f) / 2.0f;
                float value = clamped_lerp(low, high, t) - offset;
                if (k > 13) {
                    const float f = static_cast<float>(k - 13) / 3.0f;
                    value = value * (1.0f - f) + -10.0f * f;
                }
                out[index++] = value;
            }
        }
}

void OverworldGenerator::prepare_heights(TerrainVolume &volume, int chunk_x, int chunk_z) const {
    // prepareHeights 0x1b36ac0: raw biomes at (x * 4 - 2, z * 4 - 2), the
    // density grid, then trilinear interpolation (y steps of 1/8, x and z of
    // 1/4) into stone, water below the sea level, or air.
    const auto biomes = raw_biomes(layers_, chunk_x * 4 - 2, chunk_z * 4 - 2);
    float grid[425];
    heights(grid, biomes, chunk_x * 4, 0, chunk_z * 4);
    for (int xc = 0; xc < 4; ++xc) {
        const int x0 = xc * 5, x1 = (xc + 1) * 5;
        for (int zc = 0; zc < 4; ++zc) {
            const int i00 = (x0 + zc) * 17, i01 = (x0 + zc + 1) * 17, i10 = (x1 + zc) * 17, i11 = (x1 + zc + 1) * 17;
            for (int yc = 0; yc < 16; ++yc) {
                float s0 = grid[i00 + yc], s1 = grid[i01 + yc], s2 = grid[i10 + yc], s3 = grid[i11 + yc];
                const float d0 = (grid[i00 + yc + 1] - s0) * 0.125f, d1 = (grid[i01 + yc + 1] - s1) * 0.125f;
                const float d2 = (grid[i10 + yc + 1] - s2) * 0.125f, d3 = (grid[i11 + yc + 1] - s3) * 0.125f;
                for (int sy = 0; sy < 8; ++sy) {
                    float a = s0, b = s1;
                    const float da = (s2 - s0) * 0.25f, db = (s3 - s1) * 0.25f;
                    for (int sx = 0; sx < 4; ++sx) {
                        const float dz = (b - a) * 0.25f;
                        float value = a - dz;
                        const int y = yc * 8 + sy;
                        for (int sz = 0; sz < 4; ++sz) {
                            value = dz + value;
                            auto &block = volume.at(xc * 4 + sx, y, zc * 4 + sz);
                            if (value > 0.0f)
                                block = terrain_block::stone;
                            else if (y < kSeaLevel)
                                block = terrain_block::water;
                            else
                                block = terrain_block::air;
                        }
                        a = da + a;
                        b = db + b;
                    }
                    s0 = d0 + s0;
                    s1 = d1 + s1;
                    s2 = d2 + s2;
                    s3 = d3 + s3;
                }
            }
        }
    }
}

namespace {
bool same_legacy(const TerrainBlock &a, const TerrainBlock &b) { return std::strcmp(a.name, b.name) == 0; }

// OverworldBiome::_placeBedrock 0x1db5b00: bedrock for y in range_incl(nextInt(1, 5)),
// i.e. y 0 up to and including the draw.
void place_bedrock(TerrainVolume &v, Random &random, int x, int z) {
    const int top = random.next_int(1, 5);
    for (int y = 0; y <= top; ++y)
        v.at(x, y, z) = terrain_block::bedrock;
}

// OverworldBiome::buildSurfaceAt 0x1db5c10 with the biome's current top and
// filler (the subclasses write them before calling it).
void default_surface(const BiomeData &b, TerrainBlock biome_top, TerrainBlock biome_filler, TerrainVolume &v,
                     Random &random, int x, int z, float noise, int sea_level) {
    TerrainBlock top = biome_top, filler = biome_filler;
    int state = -1;
    const int run = static_cast<int>(noise / 3.0f + 3.0f + random.next_float() * 0.25f);
    place_bedrock(v, random, x, z);
    for (int y = TerrainVolume::height - 1; y > 0; --y) {
        auto &block = v.at(x, y, z);
        if (same_legacy(block, terrain_block::bedrock))
            break;
        if (same_legacy(block, terrain_block::air)) {
            state = -1;
            continue;
        }
        if (!same_legacy(block, terrain_block::stone))
            continue;
        if (state == -1) {
            if (run <= 0) {
                top = terrain_block::air;
                filler = terrain_block::stone;
            } else if (y >= sea_level - 4 && y <= sea_level + 1) {
                top = biome_top;
                filler = biome_filler;
            }
            // Biome::getTemperatureWorldGen 0x1dc1c50 is the plain temperature at y <= 64.
            if (y < sea_level && same_legacy(top, terrain_block::air))
                top = 0.15f > b.temperature ? terrain_block::ice : terrain_block::water;
            state = run;
            if (y >= sea_level - 1)
                block = top;
            else if (y < sea_level - b.floor_depth - run) {
                top = terrain_block::air;
                filler = terrain_block::stone;
                block = parse_terrain_block(b.floor);
            } else
                block = filler;
        } else if (state > 0) {
            --state;
            block = filler;
            if (state == 0 && same_legacy(filler, terrain_block::sand)) {
                state = random.next_int(4) + std::max(0, y - 63);
                filler = terrain_block::sandstone;
            }
        }
    }
}
} // namespace

void OverworldGenerator::build_surfaces(TerrainVolume &volume, int chunk_x, int chunk_z, Random &random) const {
    // buildSurfaces 0x1b37f80: the surface noise (+0x68) over the chunk at
    // scale 0.125, then Biome::buildSurfaceAt (vtable +0x90) per column, x
    // outer and z inner.
    float noise[256];
    surface_->region2d(noise, static_cast<float>(chunk_x * 16), static_cast<float>(chunk_z * 16), 16, 16, 0.125f,
                       0.125f, 1.0f, 0.5f);
    const auto biomes = chunk_biomes(layers_, chunk_x, chunk_z);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z) {
            const auto *b = biome(biomes[static_cast<std::size_t>(x + z * 16)] & 255);
            const float n = noise[x + z * 16];
            const int wx = chunk_x * 16 + x, wz = chunk_z * 16 + z;
            if (b->kind == BiomeClass::Mutated)
                b = biome(b->parent); // MutatedBiome::buildSurfaceAt 0x1dba960
            TerrainBlock top = b->top ? parse_terrain_block(b->top) : terrain_block::air;
            TerrainBlock filler = b->filler ? parse_terrain_block(b->filler) : terrain_block::air;
            switch (b->kind) {
            case BiomeClass::Nether:
            case BiomeClass::TheEnd:
                continue; // Biome::buildSurfaceAt 0x1da06a0 does nothing
            case BiomeClass::Taiga: // TaigaBiome::buildSurfaceAt 0x1da0ea0
                if (b->subtype == 1 || b->subtype == 2) {
                    top = terrain_block::grass;
                    filler = terrain_block::dirt;
                    if (static_cast<double>(n) > 1.75)
                        top = terrain_block::coarse_dirt;
                    else if (static_cast<double>(n) > -0.95)
                        top = terrain_block::podzol;
                }
                break;
            case BiomeClass::ExtremeHills: // ExtremeHillsBiome::buildSurfaceAt 0x1d8edc0
                top = terrain_block::grass;
                filler = terrain_block::dirt;
                if ((-0.1f > n || n > 0.2f) && b->subtype == 2) {
                    top = terrain_block::gravel;
                    filler = terrain_block::gravel;
                } else if (static_cast<double>(n) > 1.0 && b->subtype != 1) {
                    top = terrain_block::stone;
                    filler = terrain_block::stone;
                }
                break;
            case BiomeClass::Savanna: // SavannaBiome::buildSurfaceAt 0x1dc5490
                if (b->subtype & 1) {
                    top = terrain_block::grass;
                    filler = terrain_block::dirt;
                    if (static_cast<double>(n) > 1.75) {
                        top = terrain_block::stone;
                        filler = terrain_block::stone;
                    } else if (static_cast<double>(n) > -0.5)
                        top = terrain_block::coarse_dirt;
                }
                break;
            case BiomeClass::Swamp: { // SwampBiome::buildSurfaceAt 0x1da20b0
                const float v = swamp_noise_->value2d(static_cast<float>(wx) * 0.25f, static_cast<float>(wz) * 0.25f);
                // reverse_range(128): from y 127 down to the first block whose
                // Material isType(0) (air) -- the original checks the air block,
                // so with air above the surface it stops at y 127.
                if (v > 0.0f)
                    for (int y = TerrainVolume::height - 1; y >= 0; --y) {
                        auto &block = volume.at(x, y, z);
                        if (!same_legacy(block, terrain_block::air))
                            continue;
                        if (y == 62 && !same_legacy(block, terrain_block::water)) {
                            block = terrain_block::water;
                            if (0.12f > v)
                                volume.at(x, y + 1, z) = terrain_block::waterlily;
                        }
                        break;
                    }
                break;
            }
            case BiomeClass::Mesa:
            case BiomeClass::OceanFrozen:
                // NOT TRANSLATED YET: MesaBiome::buildSurfaceAt 0x1dc9020 and
                // OceanFrozenBiome::buildSurfaceAt 0x1dcfa80 (the default is used).
                break;
            default:
                break;
            }
            default_surface(*b, top, filler, volume, random, x, z, n, kSeaLevel);
        }
}

TerrainVolume OverworldGenerator::generate_surface(int chunk_x, int chunk_z, Random &random) const {
    TerrainVolume volume;
    random.set_seed(static_cast<std::uint32_t>(static_cast<std::int64_t>(chunk_x) * 0x14609048LL +
                                               static_cast<std::int64_t>(chunk_z) * 0x7ebe2d5LL));
    prepare_heights(volume, chunk_x, chunk_z);
    build_surfaces(volume, chunk_x, chunk_z, random);
    return volume;
}

TerrainVolume OverworldGenerator::generate(int chunk_x, int chunk_z) const {
    Random random(0);
    auto volume = generate_surface(chunk_x, chunk_z, random);
    carve(volume, chunk_x, chunk_z, random);
    return volume;
}
} // namespace bds
