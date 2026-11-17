#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace bds {
// Translation of the original `Random` class (BDS 1.6.0.15 Linux), the RNG
// behind world generation. Object layout in the original (0x9d4 bytes):
// +0 seed, +4 mt[624], +0x9c4 mti, +0x9c8 haveNextGaussian,
// +0x9cc nextGaussian (float), +0x9d0 number of initialised mt words.
// Addresses refer to the original binary; outputs are checked against the
// original functions executed under gdb (tests/reference/random_vectors.json).
class Random {
  public:
    // Random::Random(unsigned) 0x187e350. The original default constructor
    // (0x187d430) seeds from std::random_device; callers here pass a seed.
    explicit Random(std::uint32_t seed);
    void set_seed(std::uint32_t seed);              // Random::setSeed / _setSeed 0x187e380
    std::uint32_t seed() const { return seed_; }
    // Optional diagnostic hook: one notification per _genRandInt32 result,
    // including draws made by float/gaussian/shuffle callers. It never draws
    // from the generator itself; reseeding preserves the observer.
    using DrawObserver = std::function<void(std::uint32_t)>;
    void observe_draws(DrawObserver observer) { observer_ = std::move(observer); }

    std::uint32_t next_uint();                      // _genRandInt32 0x187e460
    std::int32_t next_int() { return static_cast<std::int32_t>(next_uint() >> 1); } // nextInt() 0x187d470
    // nextInt(int) 0x19c27f0: 0 for n == 0, else unsigned remainder.
    std::int32_t next_int(std::int32_t n);
    // nextInt(int, int) 0x1b3b670: min when min >= max.
    std::int32_t next_int(std::int32_t min, std::int32_t max);
    double next_double();                           // _genRandReal2 0x1932550: u32 * 2^-32
    float next_float() { return static_cast<float>(next_double()); } // nextFloat() 0x19323e0
    float next_float(float max) { return next_float() * max; }     // nextFloat(float) 0x1daa430
    float next_float(float min, float max) { return min + next_float() * (max - min); } // 0x1968b10
    bool next_boolean() { return (next_uint() & 0x8000000U) != 0; } // nextBoolean 0x1b5cff0 (bit 27)
    // nextGaussian 0x1b02c70: Marsaglia polar method in single precision.
    float next_gaussian();

  private:
    void init(std::uint32_t seed);      // _initGenRand 0x187e710
    void init_fast(std::uint32_t seed); // _initGenRandFast 0x187e3d0

    std::uint32_t seed_ = 0;
    std::array<std::uint32_t, 624> mt_{};
    std::int32_t mti_ = 625;
    bool have_gaussian_ = false;
    float gaussian_ = 0.0f;
    std::int32_t initialised_ = 0;
    DrawObserver observer_;
};
} // namespace bds
