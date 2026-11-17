#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

// Audio-thread owned, immutable bank pages. This changes only the physical
// read granularity: every voice still requests its original cache range at
// the original sample. Bounds are per source record, never per file alone.
template<unsigned Frames=4096,unsigned Pages=4>
class PcmReadAhead {
    static_assert(Frames&&Pages);
    static constexpr unsigned Missing=0xffffffffu;
    struct Page {unsigned record=Missing,first=0,count=0;std::uint64_t used=0;};
    std::array<Page,Pages> pages_{};
    std::unique_ptr<std::int16_t[]> data_;
    std::uint64_t clock_=0;
public:
    struct Stats {unsigned requests=0,hits=0,misses=0,physical_reads=0,fallbacks=0;std::uint64_t physical_frames=0;} stats;
    bool initialize(){data_.reset(new(std::nothrow) std::int16_t[Frames*Pages*2]);for(auto& p:pages_)p=Page{};return bool(data_);}
    unsigned bytes()const{return data_?Frames*Pages*4:0;}
    template<class Read>
    bool read(unsigned record,unsigned total,unsigned first,unsigned count,std::int16_t* output,Read raw_read){
        ++stats.requests;
        if(record==Missing||first>total||count>total-first)return false;
        if(!count)return true;
        auto physical=[&](unsigned begin,unsigned frames,std::int16_t* out){++stats.physical_reads;stats.physical_frames+=frames;return raw_read(begin,frames,out);};
        if(!data_){++stats.fallbacks;return physical(first,count,output);}
        while(count){
            unsigned aligned=first/Frames*Frames,index=Pages;
            for(unsigned i=0;i<Pages;++i)if(pages_[i].record==record&&pages_[i].first==aligned){index=i;break;}
            if(index==Pages){
                ++stats.misses;index=0;
                for(unsigned i=0;i<Pages;++i){if(pages_[i].record==Missing){index=i;break;}if(pages_[i].used<pages_[index].used)index=i;}
                // Invalidate before overwriting; a partial failed read must
                // never leave the evicted record mapped to corrupted bytes.
                auto& p=pages_[index];p.record=Missing;
                unsigned available=std::min(Frames,total-aligned);
                if(!physical(aligned,available,data_.get()+index*Frames*2)){
                    // A speculative future range may fail while the exact
                    // original voice request is still readable. Retry just
                    // that remaining request, retaining the previous behavior.
                    ++stats.fallbacks;return physical(first,count,output);
                }
                p.record=record;p.first=aligned;p.count=available;
            }else ++stats.hits;
            auto& p=pages_[index];p.used=++clock_;
            unsigned offset=first-p.first,frames=std::min(count,p.count-offset);
            std::memcpy(output,data_.get()+index*Frames*2+offset*2,std::size_t(frames)*4);
            first+=frames;count-=frames;output+=frames*2;
        }
        return true;
    }
};
