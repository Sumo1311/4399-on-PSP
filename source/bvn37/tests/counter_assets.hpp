// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fighter_asset.hpp"
#include <cstdio>
#include <vector>
#include <zlib.h>
namespace bvn::counter_qa {
struct Shape { float x,y;std::uint16_t width,height,stride,rows; };
static_assert(sizeof(Shape)==16);
inline uLong poseCrc(uLong crc,const FighterPose& p) {
    const Shape shape{p.x,p.y,p.width,p.height,p.stride,p.rows};
    return crc32(crc,reinterpret_cast<const Bytef*>(&shape),sizeof(shape));
}
inline bool pixels(const char* bank,const char* fixture,unsigned& frames,unsigned& error) {
    FighterAsset asset;
    if(!asset.open(bank,FighterAsset::TextureResidency::LowMemory)){error=201;return false;}
    auto* input=std::fopen(fixture,"r");
    if(!input){error=202;return false;}
    std::vector<std::byte> rgba;
    unsigned clip,phase,expected;
    while(std::fscanf(input,"%u %u %u",&clip,&phase,&expected)==3) {
        if(clip<9||clip>12||phase>=asset.clip(MoveClip(clip)).count){error=203;break;}
        const auto& pose=asset.frame(MoveClip(clip),phase).pose;
        auto crc=poseCrc(0,pose);
        for(const auto& tile:asset.poseTiles(pose)) {
            crc=poseCrc(crc,tile);
            rgba.resize(FighterAsset::textureSize(tile));
            if(!asset.decodePixels(tile,rgba)){error=204;break;}
            crc=crc32(crc,reinterpret_cast<const Bytef*>(rgba.data()),rgba.size());
        }
        if(error)break;
        if(crc!=expected){error=205;break;}
        ++frames;
    }
    if(!std::feof(input)&&!error)error=206;
    std::fclose(input);
    return !error&&frames==80;
}
} // namespace bvn::counter_qa
