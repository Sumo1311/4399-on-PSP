#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

// Immutable compressed bytes only; decoded textures and geometry are unchanged.
// Owned by the renderer under its existing GPU/resource mutex.
template<unsigned PageBytes=16384,unsigned Pages=4>
class BankReadCache {
    struct Page {unsigned first=0,count=0;std::uint64_t used=0;bool valid=false;};
    std::array<Page,Pages> pages_{};
    std::unique_ptr<unsigned char[]> data_;
    unsigned size_=0;std::uint64_t clock_=0;
public:
    struct Stats {unsigned requests=0,hits=0,misses=0,physical_reads=0,bypasses=0,fallbacks=0;std::uint64_t physical_bytes=0;} stats;
    bool initialize(unsigned size){size_=size;data_.reset(new(std::nothrow) unsigned char[PageBytes*Pages]);for(auto& p:pages_)p=Page{};return bool(data_);}
    unsigned bytes()const{return data_?PageBytes*Pages:0;}
    template<class Read>
    bool read(unsigned first,unsigned count,unsigned char* output,Read raw_read){
        ++stats.requests;if(first>size_||count>size_-first)return false;if(!count)return true;
        auto physical=[&](unsigned at,unsigned n,unsigned char* out){++stats.physical_reads;stats.physical_bytes+=n;return raw_read(at,n,out);};
        // Large tiles already amortize their native syscall. Do not split them.
        if(!data_||count>PageBytes/2){++stats.bypasses;return physical(first,count,output);}
        while(count){
            unsigned aligned=first/PageBytes*PageBytes,index=Pages;
            for(unsigned i=0;i<Pages;++i)if(pages_[i].valid&&pages_[i].first==aligned){index=i;break;}
            if(index==Pages){
                ++stats.misses;index=0;
                for(unsigned i=0;i<Pages;++i){if(!pages_[i].valid){index=i;break;}if(pages_[i].used<pages_[index].used)index=i;}
                auto& p=pages_[index];p.valid=false;unsigned n=std::min(PageBytes,size_-aligned);
                if(!physical(aligned,n,data_.get()+index*PageBytes)){++stats.fallbacks;return physical(first,count,output);}
                p.first=aligned;p.count=n;p.valid=true;
            }else ++stats.hits;
            auto& p=pages_[index];p.used=++clock_;unsigned offset=first-p.first,n=std::min(count,p.count-offset);
            std::memcpy(output,data_.get()+index*PageBytes+offset,n);first+=n;count-=n;output+=n;
        }
        return true;
    }
};
