#pragma once
#include <algorithm>
#include <cstdint>
#include <new>
#include <vector>

namespace doubleedged {
// Art entries are immutable and sorted by shape ID, ratio and raster level.
// Retain the first matching entry, including duplicate LODs, exactly as the
// original global lower_bound. Large/unknown tables keep that original path.
class ArtShapeIndex {
    struct Range {std::uint16_t first=0,last=0;};
    std::vector<Range> ranges_;
public:
    template<class Entries> void build(const Entries& entries){
        ranges_.clear();if(entries.empty()||entries.size()>65535)return;
        auto maximum=entries.back().id;if(maximum>65535)return;
        try{ranges_.resize(std::size_t(maximum)+1);}catch(const std::bad_alloc&){return;}
        for(std::size_t i=0;i<entries.size();++i){auto& r=ranges_[entries[i].id];
            if(r.first==r.last)r.first=std::uint16_t(i);r.last=std::uint16_t(i+1);}
    }
    template<class Entries> auto find(const Entries& entries,std::uint32_t id,std::uint16_t ratio)const->decltype(entries.begin()){
        if(!ranges_.empty()){
            if(id>=ranges_.size())return entries.end();const auto& r=ranges_[id];if(r.first==r.last)return entries.end();
            auto first=entries.begin()+r.first,last=entries.begin()+r.last;
            if(first->ratio==ratio)return first;
            auto it=std::lower_bound(first,last,ratio,[](const auto& e,std::uint16_t value){return e.ratio<value;});
            return it!=last&&it->ratio==ratio?it:entries.end();
        }
        auto it=std::lower_bound(entries.begin(),entries.end(),std::make_pair(id,ratio),
            [](const auto& e,auto key){return e.id<key.first||(e.id==key.first&&e.ratio<key.second);});
        return it!=entries.end()&&it->id==id&&it->ratio==ratio?it:entries.end();
    }
    std::size_t bytes()const{return ranges_.size()*sizeof(Range);}
};
}
