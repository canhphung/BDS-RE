#pragma once
// mce::Math::sin / cos (0x26ddb60, 0x26ddae0): a 65536-entry table filled by
// mce::Math::initMth (0x26dd920) with (float)sin(i / 10430.378f).
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>

namespace bds::game {
inline const std::array<float, 65536> &mce_sin_table() {
    static const auto table = [] {
        std::array<float, 65536> t{};
        for (int i = 0; i < 65536; ++i)
            t[static_cast<std::size_t>(i)] =
                static_cast<float>(std::sin(static_cast<double>(static_cast<float>(i) / 10430.378f)));
        return t;
    }();
    return table;
}
inline float mce_sin(float x) {
    return mce_sin_table()[static_cast<std::size_t>(static_cast<int>(x * 10430.378f) & 0xffff)];
}
inline float mce_cos(float x) {
    return mce_sin_table()[static_cast<std::size_t>(static_cast<int>(x * 10430.378f + 16384.0f) & 0xffff)];
}
// mce::Math::invSqrt -> fastInvSqrt (0x26dd9c0): one Newton step from 0x5f3759df.
inline float mce_inv_sqrt(float x) {
    const float half = 0.5f * x;
    float y = std::bit_cast<float>(0x5f3759df - (std::bit_cast<std::int32_t>(x) >> 1));
    y = y * (1.5f - half * y * y);
    return y;
}
} // namespace bds::game
