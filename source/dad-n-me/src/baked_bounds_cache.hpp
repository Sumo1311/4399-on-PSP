#pragma once
#include "gameswf/gameswf_types.h"
#include <cstring>
#include <cmath>
namespace psp60 {
// Raster bounds and the 2x2 part of a transform are reused across camera
// translations. Retain the original float operations on a cache miss; adding
// the common translation commutes with min/max for finite corner results.
// Draw matrices and gameplay bounds are never changed by this cache.
class BakedBoundsCache {
    struct Entry {
        bool valid=false;int id=0;unsigned ratio=0;
        float linear[4]{};gameswf::rect local,transformed;
    };
    struct Set {Entry entries[2];unsigned replace=0;};
    Set sets[128]{};
public:
#ifndef PSP
    unsigned hits=0,misses=0;
#endif
    void transform(int id,unsigned ratio,const gameswf::rect& local,const gameswf::matrix& m,gameswf::rect& out){
        const float linear[4]={m.m_[0][0],m.m_[0][1],m.m_[1][0],m.m_[1][1]};
        auto& set=sets[(unsigned(id)^(unsigned(id)>>7)^ratio)&127];
        for(const auto& e:set.entries)if(e.valid&&e.id==id&&e.ratio==ratio&&
            !std::memcmp(e.linear,linear,sizeof(linear))&&!std::memcmp(&e.local,&local,sizeof(local))){
            out=e.transformed;
            out.m_x_min+=m.m_[0][2];out.m_x_max+=m.m_[0][2];out.m_y_min+=m.m_[1][2];out.m_y_max+=m.m_[1][2];
#ifndef PSP
            ++hits;
#endif
            return;
        }
#ifndef PSP
        ++misses;
#endif
        auto base=local;auto untranslated=m;untranslated.m_[0][2]=0;untranslated.m_[1][2]=0;
        untranslated.transform(&base);
        if(!std::isfinite(base.m_x_min)||!std::isfinite(base.m_x_max)||!std::isfinite(base.m_y_min)||!std::isfinite(base.m_y_max)){
            out=local;m.transform(&out);return;
        }
        auto& e=set.entries[set.replace++&1];e.valid=true;e.id=id;e.ratio=ratio;e.local=local;e.transformed=base;std::memcpy(e.linear,linear,sizeof(linear));
        out=base;out.m_x_min+=m.m_[0][2];out.m_x_max+=m.m_[0][2];out.m_y_min+=m.m_[1][2];out.m_y_max+=m.m_[1][2];
    }
};
}
