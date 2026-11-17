#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace doubleedged {
// Fixed, GPU-fenced storage with256-byte allocation units. Adjacent free
// ranges coalesce regardless of power-of-two boundaries. In particular,
// indices plus a small CLUT do not reserve twice the texture's byte count.
// Boundary metadata is outside texture memory; free-list links use free RAM.
template<std::size_t CapacityBytes> class CompactTextureArena {
    static constexpr std::uint32_t Quantum=256,Units=CapacityBytes/Quantum;
    static constexpr std::uint32_t None=~0u,Allocated=0x80000000u,Start=0x40000000u,Count=0x3fffffffu;
    static_assert(CapacityBytes%Quantum==0&&CapacityBytes/Quantum>0&&CapacityBytes/Quantum<=Count);
    std::uint8_t* base_=nullptr;
    std::array<std::uint32_t,Units> spans_{};
    std::array<std::uint32_t,32> heads_{};
    std::size_t used_=0;
    static unsigned bucket(std::uint32_t units){return 31u-unsigned(__builtin_clz(units));}
    std::uint32_t link(std::uint32_t index,unsigned word)const{
        std::uint32_t value;std::memcpy(&value,base_+std::size_t(index)*Quantum+word*4,4);return value;
    }
    void link(std::uint32_t index,unsigned word,std::uint32_t value){std::memcpy(base_+std::size_t(index)*Quantum+word*4,&value,4);}
    void span(std::uint32_t index,std::uint32_t count,bool allocated){
        auto tag=count|(allocated?Allocated:0u);
        spans_[index+count-1]=tag;spans_[index]=tag|Start;
    }
    void erase_span(std::uint32_t index,std::uint32_t count){spans_[index]=0;spans_[index+count-1]=0;}
    void add(std::uint32_t index){
        auto b=bucket(spans_[index]&Count);auto old=heads_[b];
        link(index,0,old);link(index,1,None);if(old!=None)link(old,1,index);heads_[b]=index;
    }
    void remove(std::uint32_t index){
        auto b=bucket(spans_[index]&Count),next=link(index,0),previous=link(index,1);
        if(previous==None)heads_[b]=next;else link(previous,0,next);
        if(next!=None)link(next,1,previous);
    }
public:
    void initialize(void* storage){
        base_=static_cast<std::uint8_t*>(storage);used_=0;spans_.fill(0);heads_.fill(None);
        if(base_){span(0,Units,false);add(0);}
    }
    bool ready()const{return base_!=nullptr;}
    bool contains(const void* pointer)const{
        auto address=reinterpret_cast<std::uintptr_t>(pointer),start=reinterpret_cast<std::uintptr_t>(base_);
        return base_&&address>=start&&address-start<CapacityBytes;
    }
    void* allocate(std::size_t bytes){
        if(!base_||!bytes||bytes>CapacityBytes)return nullptr;
        auto needed=std::uint32_t((bytes+Quantum-1)/Quantum);std::uint32_t index=None;
        for(unsigned b=bucket(needed);b<heads_.size()&&index==None;++b)
            for(auto i=heads_[b];i!=None;i=link(i,0))if((spans_[i]&Count)>=needed){index=i;break;}
        if(index==None)return nullptr;
        auto available=spans_[index]&Count;remove(index);erase_span(index,available);
        span(index,needed,true);
        if(available>needed){span(index+needed,available-needed,false);add(index+needed);}
        used_+=std::size_t(needed)*Quantum;return base_+std::size_t(index)*Quantum;
    }
    bool release(void* pointer){
        if(!contains(pointer))return false;
        auto offset=std::size_t(static_cast<std::uint8_t*>(pointer)-base_);if(offset%Quantum)return false;
        auto index=std::uint32_t(offset/Quantum),tag=spans_[index],count=tag&Count;
        if((tag&(Start|Allocated))!=(Start|Allocated)||!count||count>Units-index)return false;
        if((spans_[index+count-1]&(Allocated|Count))!=(Allocated|count))return false;
        std::uint32_t left=None,right=None,left_count=0,right_count=0;
        // Validate both boundaries before changing any live/free state. An
        // interior pointer, including an allocated range's end tag, is rejected.
        if(index){auto previous=spans_[index-1],n=previous&Count;if(!n||n>index)return false;
            if(!(previous&Allocated)){left=index-n;left_count=n;if(spans_[left]!=(Start|n))return false;}}
        if(index+count<Units){auto next=spans_[index+count],n=next&Count;
            if(!(next&Start)||!n||n>Units-index-count)return false;
            if(!(next&Allocated)){right=index+count;right_count=n;}}
        erase_span(index,count);used_-=std::size_t(count)*Quantum;
        if(left!=None){remove(left);erase_span(left,left_count);}
        if(right!=None){remove(right);erase_span(right,right_count);}
        auto begin=left==None?index:left;span(begin,count+left_count+right_count,false);add(begin);return true;
    }
    std::size_t used_bytes()const{return used_;}
    static constexpr std::size_t capacity_bytes(){return CapacityBytes;}
};
}
