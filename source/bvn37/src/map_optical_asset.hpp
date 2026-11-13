// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "map_optical.hpp"
#include <cstdint>
#include <vector>

namespace bvn {
struct OpticalTile {
    float x,y;
    std::uint32_t dependencies,firstIndex;
};
class MapOpticalAsset {
public:
    bool open(const char* path,unsigned expectedChildren);
    std::span<const OpticalRect> children()const{return children_;}
    std::span<const OpticalTile> tiles(unsigned kind)const{
        const auto layout=layouts_[kind];return std::span(tiles_).subspan(layout.first,layout.count);
    }
    unsigned paletteFrames()const{return paletteFrames_;}
    unsigned texture(const OpticalTile& tile,unsigned mask)const;
private:
    struct Layout{std::uint32_t first,count;};
    std::array<Layout,6> layouts_{};
    std::vector<OpticalRect> children_;
    std::vector<OpticalTile> tiles_;
    std::vector<std::uint32_t> indices_;
    unsigned paletteFrames_=0;
};
} // namespace bvn
