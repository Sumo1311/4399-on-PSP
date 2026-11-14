#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aow {
// A stereo-linked, 128-frame lookahead limiter. The original PCM and voice
// timing stay unchanged; only an overloaded mix loses gain. The delay is
// 2.90 ms at the PSP's 44100 Hz output rate. No allocations or I/O here.
class AudioLimiter {
public:
 static constexpr unsigned Lookahead=128;
 static constexpr float Ceiling=29490.f; // 0.90 full scale, before output trim
 struct Stats {uint32_t peakInput=0,peakOutput=0;uint64_t limitedFrames=0,safetyClamps=0;};
 void process(const int32_t* input,int16_t* output,unsigned frames,bool headphones){
  const float targetMaster=headphones?1.f:.625f;
  for(unsigned f=0;f<frames;++f,++cursor){
   const int32_t left=input[f*2],right=input[f*2+1];
   uint32_t peak=std::max(magnitude(left),magnitude(right));
   stats.peakInput=std::max(stats.peakInput,peak);
   while(head!=tail && cursor-indices[head]>Lookahead)head=(head+1)%Queue;
   while(head!=tail){unsigned previous=(tail+Queue-1)%Queue;if(peaks[previous]>peak)break;tail=previous;}
   indices[tail]=cursor;peaks[tail]=peak;tail=(tail+1)%Queue;
   float target=peaks[head]>Ceiling?Ceiling/peaks[head]:1.f;
   // Fast attack is completed during lookahead; a 113 ms release avoids
   // discontinuous block-by-block normalisation and audible pumping.
   gain=target+(gain-target)*(target<gain?.90f:.9998f);
   const unsigned slot=cursor%Lookahead;
   int32_t delayedLeft=delay[slot][0],delayedRight=delay[slot][1];
   delay[slot][0]=left;delay[slot][1]=right;
   uint32_t delayedPeak=std::max(magnitude(delayedLeft),magnitude(delayedRight));
   float boundedGain=delayedPeak>Ceiling?std::min(gain,Ceiling/delayedPeak):gain;
   master+=(targetMaster-master)*.002f;
   if(boundedGain<.999f)++stats.limitedFrames;
   for(unsigned c=0;c<2;++c){
    float value=(c?delayedRight:delayedLeft)*boundedGain*master;
    int32_t sample=int32_t(std::lround(value));
    if(sample<-32768||sample>32767){++stats.safetyClamps;sample=std::max<int32_t>(-32768,std::min<int32_t>(32767,sample));}
    output[f*2+c]=int16_t(sample);
    stats.peakOutput=std::max(stats.peakOutput,magnitude(sample));
   }
  }
 }
 const Stats& diagnostics()const{return stats;}
private:
 static uint32_t magnitude(int32_t v){return v<0?uint32_t(-int64_t(v)):uint32_t(v);}
 static constexpr unsigned Queue=Lookahead+2;
 int32_t delay[Lookahead][2]{};
 uint32_t indices[Queue]{},peaks[Queue]{},cursor=0;
 unsigned head=0,tail=0;
 float gain=1.f,master=.625f;
 Stats stats;
};
}
