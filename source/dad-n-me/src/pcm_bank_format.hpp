#pragma once
#include <cstdint>
// Bank-only representation flag; original Flash format/channel bits stay intact.
constexpr std::uint32_t DnmPcmMono16 = 1u << 24;
inline unsigned dnm_pcm_stride(std::uint32_t flags){return flags&DnmPcmMono16?2u:4u;}
inline void dnm_expand_mono16(std::int16_t* data,unsigned frames){
    // In-place expansion from the end cannot overwrite unread mono samples.
    for(unsigned i=frames;i>0;--i){auto sample=data[i-1];data[(i-1)*2]=sample;data[(i-1)*2+1]=sample;}
}
