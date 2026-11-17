#pragma once
// Opt-in, bounded QA telemetry. No game state, timing policy or pixel changes.
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <pspkernel.h>
#include <psppower.h>
#include <malloc.h>
namespace perf {
enum Counter { IO,DECODE,EVICT,SYNC,VBLANK,FILTER,ALLOC,COUNTERS };
inline std::atomic<bool> enabled{false};
inline unsigned counters[COUNTERS]{},decoded=0,misses=0,evictions=0;
inline std::atomic<unsigned> audio_mix{0},audio_blocks{0},audio_io{0},audio_max{0},audio_lock{0};
inline unsigned long long start=0,previous=0;
inline unsigned present_offset=0;
inline void present(){if(enabled)present_offset=unsigned(sceKernelGetSystemTimeWide()-start);}
inline unsigned prior_audio[5]{},write_us=0,limit=0,count=0,used=0;
inline FILE* file=nullptr;
struct Record { unsigned tick,rendered,phase,interval_us,work_us,logic_us,draw_us,finish_us,io_us,decode_us,evict_us,sync_us,vblank_us,filter_us,alloc_us,decoded_bytes,misses,evictions,heap_free,audio_mix_us,audio_blocks,audio_io_us,audio_max_us,audio_lock_us,write_us,lag_us,present_offset_us; };
inline Record records[512];
struct Scope {Counter counter;unsigned long long begin;Scope(Counter c):counter(c),begin(enabled?sceKernelGetSystemTimeWide():0){}~Scope(){if(begin)counters[counter]+=unsigned(sceKernelGetSystemTimeWide()-begin);}};
inline unsigned long long now(){return enabled?sceKernelGetSystemTimeWide():0;}
inline unsigned elapsed(unsigned long long t){return t?unsigned(sceKernelGetSystemTimeWide()-t):0;}
inline void init(){if(FILE* config=std::fopen("PERF.QA","r")){std::fscanf(config,"%u",&limit);std::fclose(config);file=std::fopen("PERF.BIN","wb");if(file){int clock=scePowerGetCpuClockFrequencyInt();unsigned header[4]={0x31465250,sizeof(Record),2,clock>0?unsigned(clock):0};std::fwrite(header,sizeof(header),1,file);enabled=true;}}}
inline void begin(){if(!enabled)return;start=sceKernelGetSystemTimeWide();for(auto& n:counters)n=0;decoded=misses=evictions=0;present_offset=0;}
inline void end(unsigned tick,unsigned rendered,unsigned phase,unsigned logic,unsigned draw,unsigned finish,unsigned lag,bool done){
 if(!enabled)return;auto end=sceKernelGetSystemTimeWide();unsigned a[5]={audio_mix.load(),audio_blocks.load(),audio_io.load(),audio_max.exchange(0),audio_lock.load()};
 records[used++]={tick,rendered,phase,previous?unsigned(start-previous):0,unsigned(end-start),logic,draw,finish,counters[IO],counters[DECODE],counters[EVICT],counters[SYNC],counters[VBLANK],counters[FILTER],counters[ALLOC],decoded,misses,evictions,unsigned(mallinfo().fordblks),a[0]-prior_audio[0],a[1]-prior_audio[1],a[2]-prior_audio[2],a[3],a[4]-prior_audio[4],write_us,lag,present_offset};
 previous=start;for(unsigned i=0;i<5;++i)prior_audio[i]=a[i];write_us=0;++count;
 if(used==512||done||count>=limit){auto t=sceKernelGetSystemTimeWide();std::fwrite(records,sizeof(Record),used,file);used=0;write_us=unsigned(sceKernelGetSystemTimeWide()-t);}
 if(done||count>=limit){std::fclose(file);file=nullptr;enabled=false;}
}
}
