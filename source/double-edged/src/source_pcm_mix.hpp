#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Preserve sample order, voice order, per-sample pause checks and float-to-int
// contribution rounding. Scripts cannot change voices while the lock is held.
template<unsigned Block,class Voice,std::size_t N,class Paused,class Read>
void mix_source_pcm_block(std::array<Voice,N>& voices,int16_t* output,Paused paused,Read read){
    // The caller holds the audio semaphore for the whole block. Script starts,
    // stops and gain changes cannot occur here. Only this mixer can deactivate
    // a voice, so keep a private playing bit instead of reloading the shared
    // atomic flag at every sample. Publish every original deactivation at the
    // same sample; unlocked script status queries still read that atomic flag.
    struct Active {Voice* voice;float left,right;bool constant,playing;};
    std::array<Active,N> active{};unsigned count=0;
    for(auto& voice:voices){
        if(!voice.active)continue;
        bool constant=voice.envelope.empty();float left=voice.left,right=voice.right;
        if(voice.envelope.size()==1&&std::isfinite(voice.envelope[0].left)&&std::isfinite(voice.envelope[0].right)){
            constant=true;left*=voice.envelope[0].left;right*=voice.envelope[0].right;
        }
        active[count++]={&voice,left,right,constant,true};
    }
    for(unsigned n=0;n<Block;++n){
        int left=0,right=0;
        if(!paused())for(unsigned i=0;i<count;++i){
            auto& item=active[i];auto& v=*item.voice;if(!item.playing)continue;
            if(v.position>=v.end){if(v.loops>1){--v.loops;v.position=v.start;v.cache_count=0;}else{v.active=false;item.playing=false;continue;}}
            float lg=item.left,rg=item.right;
            if(!item.constant){
                auto a=&v.envelope.front(),b=a;
                for(auto& p:v.envelope){if(p.sample<=v.position)a=&p;b=&p;if(p.sample>=v.position)break;}
                float t=b->sample>a->sample?(float(v.position)-float(a->sample))/(b->sample-a->sample):0;t=std::clamp(t,0.f,1.f);
                lg*=a->left+(b->left-a->left)*t;rg*=a->right+(b->right-a->right)*t;
            }
            if(lg||rg){
                if(!v.cache_count||v.position<v.cache_first||v.position>=v.cache_first+v.cache_count){
                    v.cache_first=v.position;v.cache_count=std::min<unsigned>(v.cache.size()/2,v.end-v.position);
                    if(!read(v)){v.active=false;item.playing=false;continue;}
                }
                unsigned at=(v.position-v.cache_first)*2;left+=int(v.cache[at]*lg);right+=int(v.cache[at+1]*rg);
            }
            ++v.position;
        }
        output[n*2]=int16_t(std::clamp(left,-32768,32767));output[n*2+1]=int16_t(std::clamp(right,-32768,32767));
    }
}
