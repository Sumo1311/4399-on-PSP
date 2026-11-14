#include "audio.hpp"
#include "audio_effects.hpp"
#include "audio_clip.hpp"
#include "boot_trace.hpp"
#include "audio_limiter.hpp"
#include "runtime_memory.hpp"
#include <pspaudio.h>
#include <pspkernel.h>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <unistd.h>
namespace aow {
namespace {
struct Request {std::string name;float volume;};
struct Voice {std::shared_ptr<AudioClip> clip;size_t cursor;float volume;};
std::vector<Request> requests;std::map<std::string,std::shared_ptr<AudioClip>> clips;std::vector<Voice> voices;
std::string desiredMusic,baseDirectory;float musicVolume=1,effectVolume=1;volatile bool running=false;volatile int memoryBytes=0;
SceUID lock=-1,thread=-1,musicThread=-1;int channel=-1;
// A 1024-frame block covers 23.22 ms. Keep two output blocks and all resource
// reads off the mixer thread. SFX are immutable after startup preloading.
alignas(64) int16_t output[2][2048],musicBlock[2048];
alignas(64) int32_t mixed[2048];
constexpr unsigned MusicCapacity=65536,MusicReadSamples=32768;
alignas(64) int16_t musicRing[MusicCapacity],musicRead[MusicReadSamples];
unsigned musicReadAt=0,musicWriteAt=0,musicCount=0,musicGeneration=0;
unsigned musicReadMaxUs=0;alignas(64) char musicReadBuffer[65536];
void enter(){sceKernelWaitSema(lock,1,nullptr);}void leave(){sceKernelSignalSema(lock,1);}
std::string effectKey(std::string name){for(char& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));return name;}
bool preloadEffects(){
 const std::string path=baseDirectory+"/data/audio/";
 for(unsigned index=0;index<EffectCount;++index){
  const auto& info=EffectFiles[index];const std::string name=info.name,key=effectKey(name);
  if(!largeMemory()&&!info.required)continue;
  if(info.alias<index){auto original=clips.find(effectKey(EffectFiles[info.alias].name));if(original==clips.end())return false;clips[key]=original->second;continue;}
  FILE* file=std::fopen((path+name+".pcm").c_str(),"rb");if(!file)return false;
  std::fseek(file,0,SEEK_END);long bytes=std::ftell(file);std::rewind(file);
  if(bytes!=long(info.samples*2)||info.storedSamples>info.samples){std::fclose(file);return false;}
  auto clip=std::make_shared<AudioClip>();clip->duration=info.samples;clip->mono=info.mono;clip->pcm.resize(info.storedSamples/(info.mono?2:1));
  char tag[160];std::snprintf(tag,sizeof(tag),"audio-read %s destination=%p bytes=%u",info.name,static_cast<void*>(clip->pcm.data()),unsigned(info.storedSamples*2));bootTrace(tag);
  // Keep actual device reads in one aligned static buffer, then copy to the
  // resident clip. This also performs lossless dual-mono storage reduction.
  bool ok=true;unsigned cursor=0;
  while(cursor<info.storedSamples){unsigned n=std::min(MusicReadSamples,info.storedSamples-cursor);if(std::fread(musicRead,2,n,file)!=n){ok=false;break;}if(info.mono){for(unsigned i=0;i<n;i+=2)clip->pcm[(cursor+i)/2]=musicRead[i];}else std::memcpy(clip->pcm.data()+cursor,musicRead,n*2);cursor+=n;}
  std::fclose(file);
  bootTrace(ok?"audio-read-complete":"audio-read-failed");
  if(!ok)return false;clips[key]=clip;memoryBytes+=clip->pcm.size()*2;
 }
 return clips.size()==(largeMemory()?EffectCount:RequiredEffectCount)&&unsigned(memoryBytes)==(largeMemory()?EffectResidentBytes:RequiredEffectBytes);
}
int streamMusic(SceSize,void*){
 FILE* file=nullptr;unsigned generation=~0u;
 while(running){
  enter();std::string song=desiredMusic;unsigned requested=musicGeneration,free=MusicCapacity-musicCount;leave();
  if(requested!=generation){
   if(file)std::fclose(file);file=nullptr;generation=requested;
   if(!song.empty()){file=std::fopen((baseDirectory+"/data/audio/"+song+".pcm").c_str(),"rb");if(file)std::setvbuf(file,musicReadBuffer,_IOFBF,sizeof(musicReadBuffer));}
  }
  if(!file||free<MusicReadSamples){sceKernelDelayThread(2000);continue;}
  auto begin=sceKernelGetSystemTimeWide();size_t n=std::fread(musicRead,2,MusicReadSamples,file);
  if(n<MusicReadSamples){std::rewind(file);n+=std::fread(musicRead+n,2,MusicReadSamples-n,file);}
  unsigned elapsed=unsigned(sceKernelGetSystemTimeWide()-begin);
  enter();musicReadMaxUs=std::max(musicReadMaxUs,elapsed);
  if(generation==musicGeneration&&n&&MusicCapacity-musicCount>=n){
   unsigned first=std::min(unsigned(n),MusicCapacity-musicWriteAt);
   std::memcpy(musicRing+musicWriteAt,musicRead,first*2);std::memcpy(musicRing,musicRead+first,(n-first)*2);
   musicWriteAt=(musicWriteAt+n)%MusicCapacity;musicCount+=n;
  }
  leave();if(!n)sceKernelDelayThread(10000);
 }
 if(file)std::fclose(file);return 0;
}
int run(SceSize,void*){
 AudioLimiter limiter;unsigned outputIndex=0,generation=~0u;bool primed=false;
 bool diagnostics=false;FILE* probe=std::fopen((baseDirectory+"/QA_AUDIO.txt").c_str(),"rb");if(probe){diagnostics=true;std::fclose(probe);}
 unsigned maxMixUs=0,maxVoices=0,outputErrors=0,musicStarved=0;uint64_t blocks=0;
 while(running){
  auto begin=sceKernelGetSystemTimeWide();std::fill(musicBlock,musicBlock+2048,0);
  enter();auto batch=std::move(requests);requests.clear();float mv=musicVolume,ev=effectVolume;
  if(generation!=musicGeneration){generation=musicGeneration;primed=false;}
  if(!primed&&musicCount>=MusicReadSamples)primed=true;
  unsigned n=primed?std::min(2048u,musicCount):0;
  if(primed&&n<2048&&!desiredMusic.empty())++musicStarved;
  if(n){unsigned first=std::min(n,MusicCapacity-musicReadAt);std::memcpy(musicBlock,musicRing+musicReadAt,first*2);std::memcpy(musicBlock+first,musicRing,(n-first)*2);musicReadAt=(musicReadAt+n)%MusicCapacity;musicCount-=n;}
  leave();
  for(auto& q:batch)if(q.volume>0){auto it=clips.find(effectKey(q.name));if(it!=clips.end())voices.push_back({it->second,0,q.volume});}
  for(int i=0;i<2048;i++)mixed[i]=int(musicBlock[i]*mv);
  for(auto& v:voices){size_t n=std::min(size_t(2048),v.clip->duration-v.cursor);mixClip(*v.clip,v.cursor,mixed,n,v.volume*ev);v.cursor+=n;}
  // Keep the conservative output trim for both speakers and headphones.
  // No new firmware module/import is needed just to detect the jack.
  limiter.process(mixed,output[outputIndex],1024,false);
  if(diagnostics){maxVoices=std::max(maxVoices,unsigned(voices.size()));maxMixUs=std::max(maxMixUs,unsigned(sceKernelGetSystemTimeWide()-begin));}
  voices.erase(std::remove_if(voices.begin(),voices.end(),[](const Voice& v){return v.cursor>=v.clip->duration;}),voices.end());
  if(sceAudioOutputPannedBlocking(channel,0x8000,0x8000,output[outputIndex])<0)++outputErrors;
  outputIndex^=1;++blocks;
 }
 // Test-only evidence is written after playback, never on a mixer deadline.
 if(diagnostics){enter();unsigned readMax=musicReadMaxUs;leave();FILE* log=std::fopen((baseDirectory+"/QA_AUDIO_RESULT.tsv").c_str(),"wb");if(log){const auto& s=limiter.diagnostics();std::fprintf(log,"blocks\tpeak_input\tpeak_output\tlimited_frames\tsafety_clamps\tmax_voices\tmax_mix_us\tmusic_read_max_us\tmusic_starved_blocks\toutput_errors\tpreloaded_clips\tpreloaded_bytes\n%llu\t%u\t%u\t%llu\t%llu\t%u\t%u\t%u\t%u\t%u\t%u\t%d\n",(unsigned long long)blocks,unsigned(s.peakInput),unsigned(s.peakOutput),(unsigned long long)s.limitedFrames,(unsigned long long)s.safetyClamps,maxVoices,maxMixUs,readMax,musicStarved,outputErrors,unsigned(clips.size()),int(memoryBytes));std::fclose(log);}}
 return 0;
}
}
bool Audio::init(){
 char cwd[1024];baseDirectory=getcwd(cwd,sizeof(cwd))?cwd:".";lock=sceKernelCreateSema("aow_audio_lock",0,1,1,nullptr);if(lock<0)return false;
 try {if(!preloadEffects()){bootTrace("audio-files-failed");clips.clear();memoryBytes=0;return false;}}
 catch(...){bootTrace("audio-memory-failed");clips.clear();memoryBytes=0;return false;}
 bootTrace("audio-files-ready");channel=sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,1024,PSP_AUDIO_FORMAT_STEREO);if(channel<0){bootTrace("audio-channel-failed");return false;}
 running=true;musicThread=sceKernelCreateThread("aow_music_io",streamMusic,0x30,64*1024,PSP_THREAD_ATTR_USER,nullptr);
 thread=sceKernelCreateThread("aow_audio",run,0x18,64*1024,PSP_THREAD_ATTR_USER,nullptr);
 if(musicThread<0||thread<0){running=false;bootTrace("audio-create-thread-failed");return false;}
 int musicResult=sceKernelStartThread(musicThread,0,nullptr);int mixerResult=sceKernelStartThread(thread,0,nullptr);
 if(musicResult<0||mixerResult<0){running=false;bootTrace("audio-start-thread-failed");return false;}
 return true;
}
void Audio::stop(){running=false;for(SceUID id:{thread,musicThread})if(id>=0){sceKernelWaitThreadEnd(id,nullptr);sceKernelDeleteThread(id);}if(channel>=0)sceAudioChRelease(channel);if(lock>=0)sceKernelDeleteSema(lock);clips.clear();voices.clear();requests.clear();memoryBytes=0;}
void Audio::music(const std::string& name){if(lock<0||!running)return;enter();if(name!=desiredMusic){desiredMusic=name;++musicGeneration;musicCount=musicReadAt=musicWriteAt=0;}leave();}
void Audio::effect(const std::string& name,float volume){if(lock<0||!running)return;enter();requests.push_back({name,volume});leave();}
void Audio::volumes(float music,float effects){if(lock<0||!running)return;enter();musicVolume=music;effectVolume=effects;leave();}
int Audio::residentBytes(){return memoryBytes;}
}
