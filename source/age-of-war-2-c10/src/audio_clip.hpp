#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
namespace aow {
struct AudioClip {
 std::vector<int16_t> pcm;
 size_t duration=0; // Includes the original all-zero tail; timing is retained.
 bool mono=false; // Only used when the original stereo channels match exactly.
};
inline void mixClip(const AudioClip& clip,size_t cursor,int32_t* mixed,size_t count,float gain){
 size_t available=clip.pcm.size()*(clip.mono?2:1);if(cursor>=available)return;
 count=std::min(count,available-cursor);
 for(size_t i=0;i<count;++i)mixed[i]+=int(clip.pcm[clip.mono?(cursor+i)/2:cursor+i]*gain);
}
}
