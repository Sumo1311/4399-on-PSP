// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_effects.hpp"
#include <algorithm>
#include <cstddef>
namespace bvn {
inline void tintShadowPixels(std::span<std::byte> pixels, ShadowTint tint) {
    const int offset[]{tint.red, tint.green, tint.blue};
    for (std::size_t at = 0; at + 3 < pixels.size(); at += 4)
        for (unsigned channel = 0; channel < 3; ++channel)
            pixels[at + channel] = std::byte(std::clamp(std::to_integer<int>(pixels[at + channel]) + offset[channel], 0, 255));
}
}
