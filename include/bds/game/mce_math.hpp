#pragma once
// mce::Math::sin / cos (0x26ddb60, 0x26ddae0): a 65536-entry table filled by
// mce::Math::initMth (0x26dd920) with (float)sin(i / 10430.378f).
#include <array>
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
} // namespace bds::game
