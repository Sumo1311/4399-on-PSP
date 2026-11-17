#include "pcm_output_delay.hpp"
#include "pcm_resident_bank.hpp"
#include "pcm_read_ahead.hpp"
#include "bank_io.hpp"
#include "source_pcm_mix.hpp"
#include "voice_active_flag.hpp"
// PSP PCM playback for the AS3 compatibility layer. Bank format and buffered
// output are derived from the previous local port; no previous release changes.
#include "audio_psp.hpp"
#include "quickjs.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#include <pspaudio.h>
#include <pspkernel.h>
#include <pspiofilemgr.h>
namespace {
#ifndef DE_RESIDENT_PCM_MB
#define DE_RESIDENT_PCM_MB 4
#endif
constexpr unsigned ResidentPcmBytes=DE_RESIDENT_PCM_MB*1024*1024;
constexpr unsigned Rate=44100,Block=512,CacheFrames=1024,VoiceCount=32;
struct Record {uint32_t offset,frames,samples,rate,flags,source;};
static_assert(sizeof(Record)==24);
struct Envelope {uint32_t sample;float left,right;};
struct Voice {
    unsigned generation=0,record=0,position=0,start=0,end=0,loops=0,cache_first=0,cache_count=0;
    VoiceActiveFlag active;
    float left=1,right=1;
    std::vector<Envelope> envelope;
    std::array<int16_t,CacheFrames*2> cache{};
};
class Audio {
    PcmOutputDelay<Block,DE_AUDIO_DELAY_BLOCKS> output_delay_;
    FILE* bank_=nullptr;FILE* trace_=nullptr;SceUID bank_fd_=-1;
    std::vector<Record> records_;std::map<unsigned,unsigned> sources_;
    ResidentPcmBank resident_;
    PcmReadAhead<> stream_cache_;
    std::array<Voice,VoiceCount> voices_;
    int semaphore_=-1,channel_=-1,thread_=-1;
    std::atomic<bool> running_{false},paused_{false};
    unsigned generation_=0,frame_=0;
    unsigned status_queries_=0;
    bool media_profile_=false;
    struct MixStats {unsigned blocks=0,reads=0,read_frames=0,max_mix_us=0,max_read_us=0,max_wait_us=0;uint64_t mix_us=0,read_us=0,wait_us=0;} mix_stats_;
    void lock(){sceKernelWaitSema(semaphore_,1,nullptr);}
    void unlock(){sceKernelSignalSema(semaphore_,1);}
    Voice* voice(unsigned token){if(!token)return nullptr;auto& v=voices_[(token-1)%VoiceCount];return v.generation==(token-1)/VoiceCount?&v:nullptr;}
    static int entry(SceSize,void* argument){(*static_cast<Audio**>(argument))->mix();return 0;}
    void mix(){
        alignas(64) int16_t buffers[2][Block*2];unsigned current=0;
        while(running_.load()){
            auto output=buffers[current];auto wait_begin=media_profile_?sceKernelGetSystemTimeWide():0;lock();
            auto mix_begin=media_profile_?sceKernelGetSystemTimeWide():0;
            if(media_profile_){unsigned wait=unsigned(mix_begin-wait_begin);mix_stats_.wait_us+=wait;mix_stats_.max_wait_us=std::max(mix_stats_.max_wait_us,wait);}
            mix_source_pcm_block<Block>(voices_,output,[&](){return paused_.load();},[&](Voice& v){
                if(resident_.contains(v.record))return resident_.read(v.record,v.position,v.cache_count,v.cache.data());
                const auto& r=records_[v.record];
                auto began=media_profile_?sceKernelGetSystemTimeWide():0;
                bool valid=stream_cache_.read(v.record,r.frames,v.position,v.cache_count,v.cache.data(),[&](unsigned first,unsigned count,int16_t* output){
                    unsigned offset=r.offset+first*4,bytes=count*4;
                    if(bank_fd_>=0)return sceIoLseek32(bank_fd_,offset,PSP_SEEK_SET)>=0&&sceIoRead(bank_fd_,output,bytes)==int(bytes);
                    return !std::fseek(bank_,offset,SEEK_SET)&&std::fread(output,4,count,bank_)==count;
                });
                if(media_profile_){unsigned elapsed=unsigned(sceKernelGetSystemTimeWide()-began);++mix_stats_.reads;mix_stats_.read_frames+=v.cache_count;mix_stats_.read_us+=elapsed;mix_stats_.max_read_us=std::max(mix_stats_.max_read_us,elapsed);}
                return valid;
            });
            if(media_profile_){unsigned elapsed=unsigned(sceKernelGetSystemTimeWide()-mix_begin);++mix_stats_.blocks;mix_stats_.mix_us+=elapsed;mix_stats_.max_mix_us=std::max(mix_stats_.max_mix_us,elapsed);}
            unlock();sceAudioOutputBlocking(channel_,PSP_AUDIO_VOLUME_MAX,output_delay_.push(output));current^=1;
        }
    }
public:
    Audio(){
        if(FILE* marker=std::fopen("SOURCE_PROFILE.QA","rb")){media_profile_=true;std::fclose(marker);}
        bank_=std::fopen("audio.pak","rb");if(!bank_)return;
        uint32_t header[2];if(std::fread(header,sizeof(header),1,bank_)!=1||header[0]!=0x53414e44||header[1]>2000)return;
        records_.resize(header[1]);if(std::fread(records_.data(),sizeof(Record),records_.size(),bank_)!=records_.size())return;
        for(unsigned i=0;i<records_.size();i++)if(!(records_[i].flags&(1u<<16)))sources_[records_[i].source]=i;
        if(!resident_.warm(bank_,records_,Rate*4,ResidentPcmBytes))return;
        // Bounded optional reserve. Allocation failure keeps the original
        // exact-read path; it does not prevent startup or grow the PSP heap.
        bank_fd_=open_psp_bank("audio.pak");
        if(bank_fd_>=0)stream_cache_.initialize();
        semaphore_=sceKernelCreateSema("DE audio lock",0,1,1,nullptr);channel_=sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,Block,PSP_AUDIO_FORMAT_STEREO);if(semaphore_<0||channel_<0)return;
        thread_=sceKernelCreateThread("Double Edged audio",entry,0x12,32768,PSP_THREAD_ATTR_USER,nullptr);if(thread_<0)return;
        bool diagnostics=true;
#ifndef DE_AUDIO_PROBE
        FILE* marker=std::fopen("TRACE.QA","rb");diagnostics=marker!=nullptr;if(marker)std::fclose(marker);
#endif
        trace_=diagnostics?std::fopen("AUDIO.JSONL","w"):nullptr;
        if(diagnostics)if(FILE* memory=std::fopen("AUDIO_MEMORY.JSON","w")){
            std::fprintf(memory,"{\"scope\":\"Immutable short PCM preload; longer clips retain original streaming path\",\"resident_bytes\":%u,\"budget_bytes\":%u,\"maximum_clip_bytes\":%u}\n",unsigned(resident_.bytes()),ResidentPcmBytes,Rate*4);std::fclose(memory);
        }
        running_=true;Audio* self=this;if(sceKernelStartThread(thread_,sizeof(self),&self)<0)running_=false;
    }
    ~Audio(){running_=false;if(thread_>=0){sceKernelWaitThreadEnd(thread_,nullptr);sceKernelDeleteThread(thread_);}if(channel_>=0)sceAudioChRelease(channel_);if(semaphore_>=0)sceKernelDeleteSema(semaphore_);if(bank_fd_>=0)sceIoClose(bank_fd_);if(bank_)std::fclose(bank_);if(trace_)std::fclose(trace_);}
    bool opened()const{return running_;}
    void frame(unsigned f){frame_=f;if(trace_&&f%120==0)std::fflush(trace_);}
    void flush(){if(trace_)std::fflush(trace_);if(media_profile_){lock();auto s=mix_stats_;auto cache=stream_cache_.stats;unsigned cache_bytes=stream_cache_.bytes();unlock();if(FILE* f=std::fopen("AUDIO_MIX_STATS.JSON","w")){
        std::fprintf(f,"{\"scope\":\"Whole-run elapsed guest mix and cache/read time under original semaphore; includes scheduling, excludes audio output blocking; optional SOURCE_PROFILE only\",\"blocks\":%u,\"mix_us\":%llu,\"max_mix_us\":%u,\"semaphore_wait_us\":%llu,\"max_wait_us\":%u,\"stream_reads\":%u,\"stream_read_frames\":%u,\"stream_read_us\":%llu,\"max_read_us\":%u,\"native_stream_io\":%u,\"read_ahead_bytes\":%u,\"page_hits\":%u,\"page_misses\":%u,\"physical_reads\":%u,\"physical_read_frames\":%llu,\"fallbacks\":%u}\n",s.blocks,(unsigned long long)s.mix_us,s.max_mix_us,(unsigned long long)s.wait_us,s.max_wait_us,s.reads,s.read_frames,(unsigned long long)s.read_us,s.max_read_us,unsigned(bank_fd_>=0),cache_bytes,cache.hits,cache.misses,cache.physical_reads,(unsigned long long)cache.physical_frames,cache.fallbacks);std::fclose(f);}}}
    void pause(bool value){paused_=value;}
    unsigned start(unsigned source,unsigned begin,unsigned end,unsigned loops,bool no_multiple,std::vector<Envelope> envelope){
        auto found=sources_.find(source);if(found==sources_.end())return 0;lock();
        if(no_multiple)for(const auto& v:voices_)if(v.active&&v.record==found->second){unlock();return 0;}
        unsigned slot=0;while(slot<VoiceCount&&voices_[slot].active)++slot;
        if(slot==VoiceCount){unlock();if(trace_)std::fprintf(trace_,"{\"frame\":%u,\"error\":\"voice_capacity\"}\n",frame_);return 0;}
        auto& v=voices_[slot];const auto& r=records_[found->second];v.record=found->second;v.generation=++generation_;v.start=std::min<unsigned>(begin,r.frames);v.end=std::min<unsigned>(end,r.frames);
        v.position=v.start;v.loops=std::max(1u,loops);v.left=v.right=1;v.cache_count=0;v.envelope=std::move(envelope);v.active=v.start<v.end;
        unsigned token=v.generation*VoiceCount+slot+1;unlock();
        if(trace_)std::fprintf(trace_,"{\"frame\":%u,\"event\":\"start\",\"source\":%u,\"token\":%u,\"begin\":%u,\"end\":%u,\"loops\":%u,\"time_us\":%llu}\n",frame_,source,token,begin,end,loops,sceKernelGetSystemTimeWide());return token;
    }
    void stop(unsigned token){lock();auto v=voice(token);if(v)v->active=false;unlock();if(trace_)std::fprintf(trace_,"{\"frame\":%u,\"event\":\"stop\",\"token\":%u}\n",frame_,token);}
    void transform(unsigned token,double volume,double pan){lock();auto v=voice(token);if(v){volume=std::max(0.,volume);pan=std::clamp(pan,-1.,1.);v->left=float(volume*(pan>0?1-pan:1));v->right=float(volume*(pan<0?1+pan:1));}unlock();if(trace_)std::fprintf(trace_,"{\"frame\":%u,\"event\":\"transform\",\"token\":%u,\"volume\":%.9g,\"pan\":%.9g}\n",frame_,token,volume,pan);}
    unsigned position(unsigned token){lock();auto v=voice(token);unsigned p=v?v->position:0;unlock();return unsigned(uint64_t(p)*1000/Rate);}
    bool active(unsigned token){
        // Generation is owned by this same script worker (start/stop/query).
        // The audio thread never changes it. Its only concurrent write to the
        // queried state is the atomic active flag when mixing reaches the end.
        ++status_queries_;auto v=voice(token);return v&&v->active.playing();
    }
    unsigned query_count()const{return status_queries_;}
}* audio=nullptr;
unsigned uint_arg(JSContext* c,int argc,JSValueConst* argv,int at){uint32_t n=0;if(at<argc)JS_ToUint32(c,&n,argv[at]);return n;}
JSValue start(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    std::vector<Envelope> points;
    if(argc>5){JSValue length=JS_GetPropertyStr(c,argv[5],"length");uint32_t count=0;JS_ToUint32(c,&count,length);JS_FreeValue(c,length);if(count>255)return JS_ThrowRangeError(c,"Sound envelope too long");
        for(unsigned i=0;i<count;i++){JSValue p=JS_GetPropertyUint32(c,argv[5],i);unsigned values[3];for(unsigned j=0;j<3;j++){JSValue v=JS_GetPropertyUint32(c,p,j);uint32_t n=0;JS_ToUint32(c,&n,v);JS_FreeValue(c,v);values[j]=n;}JS_FreeValue(c,p);points.push_back({values[0],values[1]/32768.f,values[2]/32768.f});}}
    return JS_NewUint32(c,audio->start(uint_arg(c,argc,argv,0),uint_arg(c,argc,argv,1),uint_arg(c,argc,argv,2),uint_arg(c,argc,argv,3),argc>4&&JS_ToBool(c,argv[4]),std::move(points)));
}
JSValue stop(JSContext* c,JSValueConst,int argc,JSValueConst* argv){audio->stop(uint_arg(c,argc,argv,0));return JS_UNDEFINED;}
JSValue transform(JSContext* c,JSValueConst,int argc,JSValueConst* argv){double v=1,p=0;if(argc>1)JS_ToFloat64(c,&v,argv[1]);if(argc>2)JS_ToFloat64(c,&p,argv[2]);audio->transform(uint_arg(c,argc,argv,0),v,p);return JS_UNDEFINED;}
JSValue position(JSContext* c,JSValueConst,int argc,JSValueConst* argv){return JS_NewUint32(c,audio->position(uint_arg(c,argc,argv,0)));}
JSValue active(JSContext* c,JSValueConst,int argc,JSValueConst* argv){return JS_NewBool(c,audio->active(uint_arg(c,argc,argv,0)));}
}
int de_audio_bind(JSContext* c){audio=new Audio;if(!audio->opened())return 1;JSValue g=JS_GetGlobalObject(c);struct Binding{const char* name;JSCFunction* fn;int length;};for(auto b:{Binding{"nativeAudioStart",start,6},Binding{"nativeAudioStop",stop,1},Binding{"nativeAudioTransform",transform,3},Binding{"nativeAudioPosition",position,1},Binding{"nativeAudioActive",active,1}})JS_SetPropertyStr(c,g,b.name,JS_NewCFunction(c,b.fn,b.name,b.length));JS_FreeValue(c,g);return 0;}
void de_audio_begin_frame(unsigned frame){if(audio)audio->frame(frame);}
void de_audio_pause(bool paused){if(audio)audio->pause(paused);}
void de_audio_flush(){if(audio)audio->flush();}
unsigned de_audio_query_count(){return audio?audio->query_count():0;}
void de_audio_shutdown(){delete audio;audio=nullptr;}
