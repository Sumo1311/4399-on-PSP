#ifndef DNM_RESIDENT_PCM_MB
#define DNM_RESIDENT_PCM_MB 10
#endif
#include "pcm_output_delay.hpp"
#include "mix_pcm_block.hpp"
#include "pcm_resident_bank.hpp"
#include "frame_profile.hpp"
#include "audio_psp.hpp"
#include "gameswf/gameswf_sound.h"
#include "gameswf/gameswf_as_classes/as_sound.h"
#include "gameswf/gameswf_types.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
extern "C" void dnm_audio_trace(const char*);

namespace {
using namespace gameswf;
constexpr unsigned Rate=44100,Block=512,CacheFrames=4096,VoiceCount=32;
struct Record { uint32_t offset,frames,samples,rate,flags,source; };
static_assert(sizeof(Record)==24);
struct Voice {
    int handle=-1,state=0,loops=0,volume=100,pan=0;
    uint32_t position=0,start=0,end=0,cache_first=0,cache_count=0;
    bool paused=false;
    as_object* owner=nullptr;
    std::vector<sound_envelope> envelope;
    std::array<int16_t,CacheFrames*2> cache{};
};
class Audio final : public sound_handler {
    PcmOutputDelay<Block,DNM_SOURCE_LOOKAHEAD*3> output_delay_;
    FILE* bank_=nullptr;
    SceUID stream_fd_=-1;
    ResidentPcmBank resident_pcm_;
    std::vector<Record> records_;
    std::array<Voice,VoiceCount> voices_;
    std::array<gc_ptr<as_object>,VoiceCount> owners_;
    std::vector<int> volumes_;
    unsigned cursor_=0;
    int semaphore_=-1,channel_=-1,thread_=-1,master_=100;
    std::atomic<bool> running_{false},paused_{false};
    void lock(){auto t=perf::now();sceKernelWaitSema(semaphore_,1,nullptr);if(t)perf::audio_lock.fetch_add(perf::elapsed(t));}
    void unlock(){sceKernelSignalSema(semaphore_,1);}
    bool valid(int handle) const{return handle>=0 && unsigned(handle)<records_.size();}
    static int entry(SceSize,void* argument) { (*static_cast<Audio**>(argument))->mix();return 0; }
    void mix() {
        // The PSP channel may still consume the submitted buffer after the
        // blocking call returns. Mix into the other buffer until the next call.
        alignas(64) int16_t buffers[2][Block*2];unsigned current=0;
        while(running_.load()) {
            auto mix_begin=perf::now();auto output=buffers[current];
            lock();
            mix_pcm_block<Block>(voices_,master_,paused_.load(),output,[&](Voice& v){
                v.cache_first=v.position;v.cache_count=std::min<uint32_t>(CacheFrames,v.end-v.position);
                const auto& record=records_[v.handle];auto io_begin=perf::now();
                bool cached=resident_pcm_.contains(v.handle);
                bool valid;
                if(cached)valid=resident_pcm_.read(v.handle,v.position,v.cache_count,v.cache.data());
                else {
                    auto stride=dnm_pcm_stride(record.flags);
                    if(stream_fd_>=0)valid=sceIoLseek32(stream_fd_,record.offset+v.position*stride,PSP_SEEK_SET)>=0 &&
                        sceIoRead(stream_fd_,v.cache.data(),v.cache_count*stride)==int(v.cache_count*stride);
                    else valid=std::fseek(bank_,record.offset+v.position*stride,SEEK_SET)==0 && std::fread(v.cache.data(),stride,v.cache_count,bank_)==v.cache_count;
                    if(valid&&stride==2)dnm_expand_mono16(v.cache.data(),v.cache_count);
                }
                if(io_begin && !cached)perf::audio_io.fetch_add(perf::elapsed(io_begin));
                if(!valid)dnm_audio_trace("PCM read failed\n");
                return valid;
            });
            unlock();
            if(mix_begin){auto us=perf::elapsed(mix_begin);perf::audio_mix.fetch_add(us);perf::audio_blocks.fetch_add(1);if(us>perf::audio_max.load())perf::audio_max.store(us);}
            sceAudioOutputBlocking(channel_,PSP_AUDIO_VOLUME_MAX,output_delay_.push(output));
            current^=1;
        }
    }
public:
    Audio() {
        bank_=std::fopen("audio.pak","rb");if(!bank_)return;
        uint32_t header[2];
        if(std::fread(header,sizeof(header),1,bank_)!=1 || header[0]!=0x53414e44 || header[1]>2000)std::abort();
        records_.resize(header[1]);volumes_.resize(header[1],100);
        if(std::fread(records_.data(),sizeof(Record),records_.size(),bank_)!=records_.size())std::abort();
// PSP-2000: keep complete short PCM clips in RAM. Music retains the
        // checked streaming path; sample bytes and voice scheduling are unchanged.
        if(!resident_pcm_.warm(bank_,records_,1048576,DNM_RESIDENT_PCM_MB*1024*1024))std::abort();
        // A single bounded PSP read avoids repeated small stdio refills while
        // streaming music. Immutable PCM bytes and voice/cache boundaries stay.
        stream_fd_=sceIoOpen("audio.pak",PSP_O_RDONLY,0);
        semaphore_=sceKernelCreateSema("Dad audio lock",0,1,1,nullptr);
        channel_=sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,Block,PSP_AUDIO_FORMAT_STEREO);
        if(semaphore_<0 || channel_<0)return;
        thread_=sceKernelCreateThread("Dad audio",entry,0x12,32768,PSP_THREAD_ATTR_USER,nullptr);
        if(thread_<0)return;
        running_=true;Audio* self=this;
        if(sceKernelStartThread(thread_,sizeof(self),&self)<0)running_=false;
    }
    ~Audio() override {
        running_=false;
        if(thread_>=0){sceKernelWaitThreadEnd(thread_,nullptr);sceKernelDeleteThread(thread_);}
        if(channel_>=0)sceAudioChRelease(channel_);
        if(semaphore_>=0)sceKernelDeleteSema(semaphore_);
        if(stream_fd_>=0)sceIoClose(stream_fd_);
        if(bank_)std::fclose(bank_);
    }
    bool is_open() override {return running_;}
    int create_sound(void*,int,int samples,format_type format,int rate,bool stereo) override {
        if(cursor_>=records_.size())std::abort();
        unsigned index=cursor_++;const auto& r=records_[index];
        if(r.samples!=unsigned(samples) || r.rate!=unsigned(rate) || (r.flags&255)!=unsigned(format) || ((r.flags>>8)&255)!=(stereo?2u:1u)) {
            char message[180];std::snprintf(message,sizeof(message),"sound index mismatch %u source=%lu: %d/%d/%d/%d\n",index,(unsigned long)r.source,samples,int(format),rate,int(stereo));
            dnm_audio_trace(message);std::abort();
        }
        return r.frames?int(index):-1;
    }
    int load_sound(const char*) override {dnm_audio_trace("Unexpected external sound request\n");return -1;}
    void append_sound(int,void*,int) override {} // Source timeline PCM is already compiled into the checked bank.
    void play_sound(as_object* owner,int handle,int loops) override {play_sound_info(owner,handle,loops,0,0xffffffff,nullptr,0,false);}
    void play_sound_info(as_object* owner,int handle,int loops,Uint32 begin,Uint32 end,const sound_envelope* envelope,int count,bool no_multiple) override {
        if(!valid(handle))return;
        lock();
        if(no_multiple)for(auto& v:voices_)if(v.handle==handle && v.state==1){unlock();return;}
        unsigned slot=VoiceCount;
        for(unsigned n=0;n<VoiceCount;n++)if(voices_[n].state!=1){slot=n;break;}
        if(slot==VoiceCount){unlock();dnm_audio_trace("Voice capacity exceeded\n");return;}
        auto& v=voices_[slot];owners_[slot]=owner;v.owner=owner;v.handle=handle;v.state=1;
        v.start=std::min<uint32_t>(begin,records_[handle].frames);v.end=std::min<uint32_t>(end,records_[handle].frames);
        v.position=v.start;v.loops=loops;v.cache_count=0;v.paused=false;
        auto sound=cast_to<as_sound>(owner);v.volume=sound?sound->m_volume:volumes_[handle];v.pan=sound?sound->m_pan:0;
        char trace[160];std::snprintf(trace,sizeof(trace),"audio play source=%lu handle=%d volume=%d loops=%d time=%llu\n",(unsigned long)records_[handle].source,handle,v.volume,loops,sceKernelGetSystemTimeWide());dnm_audio_trace(trace);
        v.envelope.clear();if(envelope && count)v.envelope.assign(envelope,envelope+count);
        if(v.start>=v.end)v.state=2;
        unlock();
    }
    void set_volume(int handle,int value) override {if(!valid(handle))return;lock();volumes_[handle]=value;for(auto& v:voices_)if(v.handle==handle)v.volume=value;unlock();}
    void set_object_volume(as_object* owner,int,int value) override {lock();for(auto& v:voices_)if(v.owner==owner)v.volume=value;unlock();}
    void set_object_pan(as_object* owner,int,int value) override {lock();for(auto& v:voices_)if(v.owner==owner)v.pan=value;unlock();}
    void set_max_volume(int value) override {lock();master_=std::clamp(value,0,200);unlock();}
    void stop_sound(int handle) override {if(!valid(handle))return;lock();for(unsigned n=0;n<VoiceCount;n++)if(voices_[n].handle==handle){voices_[n].state=0;owners_[n]=nullptr;}unlock();}
    void stop_all_sounds() override {lock();for(unsigned n=0;n<VoiceCount;n++){voices_[n].state=0;owners_[n]=nullptr;}unlock();}
    void delete_sound(int handle) override {stop_sound(handle);}
    void pause(int handle,bool value) override {if(!valid(handle))return;lock();for(auto& v:voices_)if(v.handle==handle)v.paused=value;unlock();}
    int get_position(int handle) override {lock();unsigned position=0;for(auto& v:voices_)if(v.handle==handle && v.state==1){position=v.position;break;}unlock();return int(uint64_t(position)*1000/Rate);}
    void global_pause(bool value){paused_=value;}
    void advance(float) override {
        std::vector<gc_ptr<as_object>> complete;
        lock();for(unsigned n=0;n<VoiceCount;n++)if(voices_[n].state==2){if(owners_[n])complete.push_back(owners_[n]);owners_[n]=nullptr;voices_[n].state=0;}unlock();
        for(auto& owner:complete)owner->on_event(event_id::ON_SOUND_COMPLETE);
    }
};
}
gameswf::sound_handler* make_psp_audio(){return new Audio;}
void pause_psp_audio(gameswf::sound_handler* audio,bool value){static_cast<Audio*>(audio)->global_pause(value);}
