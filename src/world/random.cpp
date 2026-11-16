#include "bds/world/random.hpp"
#include <cmath>

namespace bds {
namespace {
constexpr std::int32_t N = 624, M = 397;
constexpr std::uint32_t upper = 0x80000000U, lower = 0x7fffffffU;
constexpr std::array<std::uint32_t, 2> mag01{0U, 0x9908b0dfU}; // rng::mMagic01 0x2824540
std::uint32_t next_init(std::uint32_t previous, std::int32_t index) {
    return 1812433253U * (previous ^ (previous >> 30)) + static_cast<std::uint32_t>(index); // imul 0x6c078965
}
} // namespace

Random::Random(std::uint32_t seed) {
    set_seed(seed);
}

void Random::set_seed(std::uint32_t seed) {
    // _setSeed: store seed, mti = 625, clear the cached gaussian, fast init.
    seed_ = seed;
    mti_ = N + 1;
    have_gaussian_ = false;
    gaussian_ = 0.0f;
    init_fast(seed);
}

void Random::init_fast(std::uint32_t seed) {
    // Only the first 398 (0x18e) words are initialised; _genRandInt32
    // completes the rest lazily, one word per draw.
    mt_[0] = seed;
    for (std::int32_t i = 1; i < 0x18e; ++i)
        mt_[static_cast<std::size_t>(i)] = next_init(mt_[static_cast<std::size_t>(i - 1)], i);
    mti_ = N;
    initialised_ = 0x18e;
}

void Random::init(std::uint32_t seed) {
    mt_[0] = seed;
    for (mti_ = 1; mti_ < N; ++mti_)
        mt_[static_cast<std::size_t>(mti_)] = next_init(mt_[static_cast<std::size_t>(mti_ - 1)], mti_);
    initialised_ = N;
}

std::uint32_t Random::next_uint() {
    if (mti_ >= N + 1) {
        init(5489);
        mti_ = 0;
    } else if (mti_ == N) {
        mti_ = 0;
    }
    const auto i = static_cast<std::size_t>(mti_);
    if (mti_ < N - M) {
        const auto y = (mt_[i] & upper) | (mt_[i + 1] & lower);
        mt_[i] = mt_[i + M] ^ (y >> 1) ^ mag01[y & 1U];
        if (initialised_ < N) {
            const auto k = static_cast<std::size_t>(initialised_);
            mt_[k] = next_init(mt_[k - 1], initialised_);
            ++initialised_;
        }
    } else if (mti_ < N - 1) {
        const auto y = (mt_[i] & upper) | (mt_[i + 1] & lower);
        mt_[i] = mt_[i + M - N] ^ (y >> 1) ^ mag01[y & 1U];
    } else {
        const auto y = (mt_[N - 1] & upper) | (mt_[0] & lower);
        mt_[N - 1] = mt_[M - 1] ^ (y >> 1) ^ mag01[y & 1U];
    }
    auto y = mt_[static_cast<std::size_t>(mti_++)];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= y >> 18;
    return y;
}

std::int32_t Random::next_int(std::int32_t n) {
    if (n == 0)
        return 0;
    return static_cast<std::int32_t>(next_uint() % static_cast<std::uint32_t>(n));
}

std::int32_t Random::next_int(std::int32_t min, std::int32_t max) {
    if (min >= max)
        return min;
    return min + next_int(max - min);
}

double Random::next_double() {
    return static_cast<double>(next_uint()) * 2.3283064365386963e-10; // constant at 0x2827e28
}

float Random::next_gaussian() {
    if (have_gaussian_) {
        have_gaussian_ = false;
        return gaussian_;
    }
    float v1, v2, s;
    do {
        v1 = 2.0f * next_float() - 1.0f;
        v2 = 2.0f * next_float() - 1.0f;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1.0f || s == 0.0f);
    const float multiplier = std::sqrt(-2.0f * std::log(s) / s); // std::log(float), std::sqrt(float)
    gaussian_ = v2 * multiplier;
    have_gaussian_ = true;
    return v1 * multiplier;
}
} // namespace bds
