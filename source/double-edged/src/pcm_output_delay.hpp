#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

// Delay only the device transport. Voice positions, mixing, loop completion
// and ActionScript callbacks retain their original clock. Two spare blocks
// keep submitted memory alive until the PSP audio channel has consumed it.
template<unsigned Frames,unsigned DelayBlocks>
class PcmOutputDelay {
    static constexpr unsigned Bytes=Frames*2*sizeof(std::int16_t);
    static constexpr unsigned Capacity=DelayBlocks+2;
    std::vector<std::uint8_t> storage;
    unsigned write=0;
public:
    PcmOutputDelay():storage(Bytes*Capacity+63,0){}
    std::int16_t* push(const std::int16_t* input){
        auto base=reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(storage.data())+63)&~std::uintptr_t(63));
        std::memcpy(base+write*Bytes,input,Bytes);
        unsigned read=(write+Capacity-DelayBlocks)%Capacity;write=(write+1)%Capacity;
        return reinterpret_cast<std::int16_t*>(base+read*Bytes);
    }
};
