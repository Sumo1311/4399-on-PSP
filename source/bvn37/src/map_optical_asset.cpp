// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_optical_asset.hpp"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace bvn {
namespace {
struct Header {
    char magic[8];
    std::uint32_t children,palette,tiles,indices,indicesStart,reserved;
};
static_assert(sizeof(Header)==32&&sizeof(OpticalRect)==32&&sizeof(OpticalTile)==16);
template<class T,std::size_t N>bool read(FILE* file,std::span<T,N> items){return items.empty()||std::fread(items.data(),sizeof(T),items.size(),file)==items.size();}
}
bool MapOpticalAsset::open(const char* path,unsigned expectedChildren){
    const std::unique_ptr<FILE,decltype(&std::fclose)> file(std::fopen(path,"rb"),std::fclose);
    if(!file||std::fseek(file.get(),0,SEEK_END))return false;
    const auto length=std::ftell(file.get());std::rewind(file.get());Header header{};
    if(length<long(sizeof(Header))||std::fread(&header,sizeof(header),1,file.get())!=1||
       std::memcmp(header.magic,"BVNOPT01",8)||header.children!=expectedChildren||
       !header.children||header.children>12||!header.palette||header.palette>60000||
       !header.tiles||header.tiles>512||!header.indices||header.indices>131072||header.reserved)return false;
    const std::size_t end=sizeof(Header)+header.children*sizeof(OpticalRect)+6*sizeof(Layout)+header.tiles*sizeof(OpticalTile);
    if(header.indicesStart!=((end+15)&~std::size_t(15))||
       std::size_t(length)!=header.indicesStart+std::size_t(header.indices)*sizeof(std::uint32_t))return false;
    MapOpticalAsset next;next.paletteFrames_=header.palette;
    next.children_.resize(header.children);next.tiles_.resize(header.tiles);next.indices_.resize(header.indices);
    if(!read(file.get(),std::span(next.children_))||!read(file.get(),std::span(next.layouts_))||!read(file.get(),std::span(next.tiles_))||
       std::fseek(file.get(),header.indicesStart,SEEK_SET)||!read(file.get(),std::span(next.indices_)))return false;
    for(const auto& child:next.children_)if(!std::isfinite(child.x)||!std::isfinite(child.y)||
        !std::isfinite(child.width)||!std::isfinite(child.height)||child.empty())return false;
    unsigned first=0;
    for(const auto layout:next.layouts_){
        if(layout.first!=first||!layout.count||layout.count>header.tiles-first)return false;
        first+=layout.count;
    }
    if(first!=header.tiles)return false;
    first=0;
    for(const auto& tile:next.tiles_){
        if(!std::isfinite(tile.x)||!std::isfinite(tile.y)||tile.dependencies>=(1u<<header.children)||tile.firstIndex!=first)return false;
        const unsigned count=1u<<std::popcount(tile.dependencies);
        if(count>header.indices-first)return false;
        first+=count;
    }
    if(first!=header.indices)return false;
    for(auto value:next.indices_)if(value!=OpticalView::Missing&&value>=header.palette)return false;
    *this=std::move(next);return true;
}
unsigned MapOpticalAsset::texture(const OpticalTile& tile,unsigned mask)const{
    unsigned local=0,index=0,dependencies=tile.dependencies;
    while(dependencies){const unsigned bit=std::countr_zero(dependencies);local|=((mask>>bit)&1u)<<index++;dependencies&=dependencies-1;}
    return indices_[tile.firstIndex+local];
}
} // namespace bvn
