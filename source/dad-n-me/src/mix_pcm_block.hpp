#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
// Algebraically identical signed accumulation, one pass per active voice.
// A voice cannot be changed by the script while the audio semaphore is held.
template<unsigned Block,class Voices,class Read> void mix_pcm_block(Voices& voices,int master,bool paused,int16_t* output,Read read) {
    std::array<int,Block*2> sums{};
    if(!paused)for(auto& v:voices) {
        if(v.state!=1 || v.paused)continue;
        int left_gain=v.volume*master/100,right_gain=left_gain;
        if(v.pan>0)left_gain=left_gain*(100-v.pan)/100;
        if(v.pan<0)right_gain=right_gain*(100+v.pan)/100;
        for(unsigned n=0;n<Block;++n) {
            if(v.position>=v.end) {
                if(v.loops>0){--v.loops;v.position=v.start;v.cache_count=0;}
                else {v.state=2;break;}
            }
            int lg=left_gain,rg=right_gain;
            if(!v.envelope.empty()) {
                auto a=&v.envelope.front(),b=a;
                for(auto& point:v.envelope){if(point.m_mark44<=v.position)a=&point;b=&point;if(point.m_mark44>=v.position)break;}
                float t=b->m_mark44>a->m_mark44?float(v.position-a->m_mark44)/(b->m_mark44-a->m_mark44):0;
                t=std::clamp(t,0.f,1.f);
                lg=int(lg*(a->m_level0+(b->m_level0-a->m_level0)*t)/32768.f);
                rg=int(rg*(a->m_level1+(b->m_level1-a->m_level1)*t)/32768.f);
            }
            if(lg || rg) {
                if((!v.cache_count || v.position<v.cache_first || v.position>=v.cache_first+v.cache_count) && !read(v)){v.state=2;break;}
                unsigned index=(v.position-v.cache_first)*2;
                sums[n*2]+=v.cache[index]*lg/100;sums[n*2+1]+=v.cache[index+1]*rg/100;
            }
            ++v.position;
        }
    }
    for(unsigned n=0;n<Block*2;++n)output[n]=int16_t(std::clamp(sums[n],-32768,32767));
}
