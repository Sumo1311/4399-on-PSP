// Opaque-backdrop HARDLIGHT and SCREEN coefficients for the fixed-function GE.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <span>
namespace bvn {
enum class EffectSurface : unsigned { Color, HardlightMultiply, HardlightAdd, Screen };
inline void effectSurfacePixels(std::span<std::byte> rgba,EffectSurface mode) {
    if(mode==EffectSurface::Color)return;
    for(std::size_t i=0;i<rgba.size();i+=4) {
        const unsigned alpha=std::to_integer<unsigned>(rgba[i+3]);
        for(unsigned c=0;c<3;++c) {
            const unsigned source=std::to_integer<unsigned>(rgba[i+c]);
            unsigned value=0;
            if(mode==EffectSurface::Screen)value=(alpha*source+127)/255;
            else if(mode==EffectSurface::HardlightAdd)value=(alpha*std::max(int(source)*2-255,0)+127)/255;
            else value=255-(alpha*unsigned(std::abs(int(source)*2-255))+127)/255;
            rgba[i+c]=std::byte(value);
        }
        rgba[i+3]=std::byte{255};
    }
}
}
