#pragma once
#include "filter_pixels.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace doubleedged {
using FilterRasterKey=std::array<std::uint32_t,21>;
// Exact pre-filter raster checkpoints. Keys contain raw vertex bits and texture
// identity; any changed input limits reuse to the preceding complete checkpoint.
class FilterPrefixCache {
    struct Snapshot {
        unsigned operations=0,mask_level=0;
        bool submitting=false;
        std::vector<std::uint32_t> pixels;
        std::vector<std::uint8_t> stencil;
    };
    int x_=0,y_=0,width_=0,height_=0;
    unsigned slots_=0;
    std::array<unsigned,3> targets_{};
    std::vector<FilterRasterKey> keys_;
    std::array<Snapshot,3> snapshots_;
public:
    unsigned resume(const std::vector<FilterRasterKey>& keys,glorton::FilterSurface& surface){
        if(x_!=surface.left||y_!=surface.top||width_!=surface.width||height_!=surface.height){
            keys_.clear();for(auto& s:snapshots_)s=Snapshot{};
            x_=surface.left;y_=surface.top;width_=surface.width;height_=surface.height;
        }
        slots_=std::min(3u,unsigned((1024u*1024u)/(surface.pixels.size()*5u)));
        unsigned common=0;
        while(common<keys.size()&&common<keys_.size()&&keys[common]==keys_[common])++common;
        unsigned target=common?common:std::min(32u,unsigned(keys.size()));
        for(unsigned i=0;i<slots_;++i)targets_[i]=std::max(1u,target>>i);
        Snapshot* best=nullptr;
        for(unsigned i=0;i<slots_;++i){auto& s=snapshots_[i];
            if(s.operations>common)s.operations=0;
            if(s.operations&&(!best||s.operations>best->operations))best=&s;
        }
        if(best){
            surface.pixels=best->pixels;surface.stencil=best->stencil;
            surface.mask_level=best->mask_level;surface.submitting_mask=best->submitting;
            return best->operations;
        }
        return 0;
    }
    void checkpoint(unsigned operations,const glorton::FilterSurface& surface){
        for(unsigned i=0;i<slots_;++i)if(operations&&operations==targets_[i]){
            auto& s=snapshots_[i];s.operations=operations;
            s.pixels=surface.pixels;s.stencil=surface.stencil;s.mask_level=surface.mask_level;s.submitting=surface.submitting_mask;
        }
    }
    void finish(std::vector<FilterRasterKey>&& keys){keys_=std::move(keys);}
};
}
