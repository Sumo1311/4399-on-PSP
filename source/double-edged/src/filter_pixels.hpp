#pragma once
#include "gameswf/gameswf_filters.h"
#include <cstdint>
#include <vector>

namespace glorton {
// Rasterized filtered subtrees use premultiplied RGBA internally. The result
// is converted to straight RGBA only when handed back to the PSP GU.
struct RasterVertex {float x,y,u,v;std::uint32_t color;};
struct TextureView {
    const std::uint32_t* pixels=nullptr;
    int width=0,height=0;
    bool linear=true,repeat=false;
    const std::uint8_t* indices=nullptr;
    const std::uint32_t* palette=nullptr;
    unsigned index_bits=8;
};
struct FilterSurface {
    int left,top,width,height;
    std::vector<std::uint32_t> pixels;
    std::vector<std::uint8_t> stencil;
    unsigned mask_level=0;bool submitting_mask=false;
    FilterSurface(int x,int y,int w,int h);
    void triangle(RasterVertex a,RasterVertex b,RasterVertex c,const TextureView& texture);
    void begin_mask();
    void end_mask();
    void pop_mask();
    void apply(const gameswf::filter_list& filters,float stage_x,float stage_y);
    void unpremultiply();
};
struct FilterPadding {int x=0,y=0;};
FilterPadding filter_padding(const gameswf::filter_list& filters,float stage_x,float stage_y);
}
