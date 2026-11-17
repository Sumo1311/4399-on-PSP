#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace doubleedged {
struct FilterTilePixels {
    unsigned width,height;
    std::vector<std::uint32_t> pixels;
};
inline unsigned filter_power2(unsigned value){unsigned result=1;while(result<value)result*=2;return result;}
// Keep each upload <=256x256, with a one-pixel neighbor gutter. The gutter
// preserves linear sampling across tile seams at fractional screen positions.
// Outer samples follow the former full-image power-of-two padding/clamp rule.
inline FilterTilePixels pack_filter_tile(const std::uint32_t* source,unsigned source_width,unsigned source_height,unsigned x,unsigned y,unsigned width,unsigned height){
    FilterTilePixels result{filter_power2(width+2),filter_power2(height+2),{}};
    result.pixels.resize(result.width*result.height);
    const int outer_width=filter_power2(source_width),outer_height=filter_power2(source_height);
    for(unsigned row=0;row<height+2;row++)for(unsigned column=0;column<width+2;column++){
        int sx=std::clamp(int(x+column)-1,0,outer_width-1),sy=std::clamp(int(y+row)-1,0,outer_height-1);
        if(unsigned(sx)<source_width&&unsigned(sy)<source_height)result.pixels[row*result.width+column]=source[sy*source_width+sx];
    }
    return result;
}
}
