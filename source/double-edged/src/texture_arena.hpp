#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace doubleedged {
// A fixed backing allocation keeps small script allocations out of decoded
// texture blocks. Blocks split/coalesce in powers of two; pixels stay intact
// until the caller's existing GPU fence releases their allocation.
template<std::size_t CapacityBytes> class TextureArena {
    static constexpr std::uint32_t Quantum=256,Units=CapacityBytes/Quantum,None=~0u;
    static constexpr std::uint8_t Interior=255,Allocated=128;
    static_assert(CapacityBytes%Quantum==0 && Units>0);
    std::uint8_t* base_=nullptr;
    std::array<std::uint8_t,Units> tags_{};
    std::array<std::uint32_t,32> heads_{};
    std::size_t used_=0;
    std::uint32_t link(std::uint32_t index,unsigned word)const {
        std::uint32_t value;std::memcpy(&value,base_+std::size_t(index)*Quantum+word*4,4);return value;
    }
    void link(std::uint32_t index,unsigned word,std::uint32_t value){std::memcpy(base_+std::size_t(index)*Quantum+word*4,&value,4);}
    void add(std::uint32_t index,unsigned order){
        tags_[index]=order;link(index,0,heads_[order]);link(index,1,None);
        if(heads_[order]!=None)link(heads_[order],1,index);heads_[order]=index;
    }
    void remove(std::uint32_t index,unsigned order){
        auto next=link(index,0),previous=link(index,1);
        if(previous==None)heads_[order]=next;else link(previous,0,next);
        if(next!=None)link(next,1,previous);tags_[index]=Interior;
    }
public:
    void initialize(void* storage){
        base_=static_cast<std::uint8_t*>(storage);used_=0;tags_.fill(Interior);heads_.fill(None);
        std::uint32_t index=0,left=Units;
        while(left){unsigned order=0;while(order<30&&(std::uint32_t(1)<<(order+1))<=left)++order;
            while(index&((std::uint32_t(1)<<order)-1))--order;
            add(index,order);auto count=std::uint32_t(1)<<order;index+=count;left-=count;}
    }
    bool ready()const{return base_!=nullptr;}
    bool contains(const void* pointer)const {
        auto address=reinterpret_cast<std::uintptr_t>(pointer),start=reinterpret_cast<std::uintptr_t>(base_);
        return base_&&address>=start&&address-start<CapacityBytes;
    }
    void* allocate(std::size_t bytes){
        if(!base_||!bytes||bytes>CapacityBytes)return nullptr;
        unsigned order=0;std::size_t block=Quantum;while(block<bytes){block*=2;++order;}
        unsigned available=order;while(available<heads_.size()&&heads_[available]==None)++available;
        if(available==heads_.size())return nullptr;
        auto index=heads_[available];remove(index,available);
        while(available>order){--available;add(index+(std::uint32_t(1)<<available),available);}
        tags_[index]=Allocated|order;used_+=block;return base_+std::size_t(index)*Quantum;
    }
    bool release(void* pointer){
        if(!contains(pointer))return false;
        auto offset=std::size_t(static_cast<std::uint8_t*>(pointer)-base_);if(offset%Quantum)return false;
        auto index=std::uint32_t(offset/Quantum);auto tag=tags_[index];
        if(tag==Interior||!(tag&Allocated))return false;
        unsigned order=tag&~Allocated;used_-=std::size_t(Quantum)<<order;tags_[index]=Interior;
        while(order<31){auto buddy=index^(std::uint32_t(1)<<order);
            if(buddy>=Units||tags_[buddy]!=order)break;
            remove(buddy,order);index=std::min(index,buddy);++order;
        }
        add(index,order);return true;
    }
    std::size_t used_bytes()const{return used_;}
    static constexpr std::size_t capacity_bytes(){return CapacityBytes;}
};
}
