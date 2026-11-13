// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#if __has_include("psp_allocation_recovery.hpp")
#include "psp_allocation_recovery.hpp"
#define HAS_RECOVERY 1
#else
#define HAS_RECOVERY 0
#endif
#include <array>
#include <cstdlib>
#include <new>
#include <string>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Allocation QA",PSP_MODULE_USER,0,68);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(-1024);
extern "C" {
struct AllocationPressureQA {
 unsigned status,error,stage,feature,warmTextures,warmBytes,heldBlocks,heldBytes;
 unsigned workerFailed,workerReleased,lockedFailed,lockedReleased,criticalSuccess;
 unsigned requests,releases,releasedBytes,pixels,colored,mismatch,hashLow,hashHigh;
}g_allocationPressureQa;
#if HAS_RECOVERY
extern volatile struct AllocationRecoveryQA {unsigned installed,ownerThread,requests,releases,bytes,unavailable;}g_allocationRecoveryQa;
#endif
}
namespace {
using namespace bvn;
alignas(64)std::array<unsigned,512*272> beforePixels,afterPixels;
std::array<void*,1024> pressure{};
std::array<FighterPose,128> poses{};
static StageRenderer renderer;
constexpr std::size_t Request=256*1024,Block=64*1024;
unsigned frames=0;
unsigned releases(){
#if HAS_RECOVERY
 return g_allocationRecoveryQa.releases;
#else
 return 0;
#endif
}
int allocateWorker(SceSize,void*){
 try{void* p=::operator new(Request);::operator delete(p);}catch(const std::bad_alloc&){g_allocationPressureQa.workerFailed=1;}
 return 0;
}
void draw(const FighterAsset& art,unsigned n){
 renderer.begin();++frames;
 for(unsigned i=0;i<n;++i)renderer.effectPose(art,poses[i],{70.+(i%6)*35,35.+(i%4)*35},1,1,0,EffectBlend::Normal);
}
void capture(std::array<unsigned,512*272>& into){
 auto* vram=static_cast<std::byte*>(sceGeEdramGetAddr());const auto drawn=frames%2?0:512*272*4;
 sceKernelDcacheWritebackInvalidateRange(into.data(),sizeof(into));sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,vram+drawn,0,0,512,into.data());sceGuTexSync();
}
const volatile unsigned* uncached(const std::array<unsigned,512*272>& p){return reinterpret_cast<const volatile unsigned*>(reinterpret_cast<std::uintptr_t>(p.data())|0x40000000u);}
bool run(const std::string& folder){
 auto& q=g_allocationPressureQa;q.stage=1;q.feature=HAS_RECOVERY;
#if HAS_RECOVERY
 if(!installGameAllocationRecovery(renderer)){q.error=1;return false;}
#endif
 FighterAsset art;if(!art.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)){q.error=2;return false;}renderer.start();
 unsigned count=0;std::size_t bytes=0;
 for(unsigned c=0;c<art.clipCount()&&bytes<1200*1024;++c)for(unsigned f=0;f<art.clip(MoveClip(c)).count&&bytes<1200*1024;++f)for(auto p:art.poseTiles(art.frame(MoveClip(c),f).pose)){
  if(!p.width||!p.height||FighterAsset::textureSize(p)<4096)continue;
  bool seen=false;for(unsigned i=0;i<count;++i)seen|=poses[i].offset==p.offset;if(seen)continue;
  if(count==poses.size()){q.error=3;return false;}p.x=p.y=0;poses[count++]=p;bytes+=FighterAsset::textureSize(p);
 }
 if(bytes<1200*1024){q.error=4;return false;}q.warmTextures=count;q.warmBytes=bytes;
 // Warm caches with a complete previous list, then mark every texture as used
 // by the unfinished current list before exhausting the shared heap.
 q.stage=2;draw(art,count);renderer.end();draw(art,count);
 const int worker=sceKernelCreateThread("allocation worker",allocateWorker,32,16*1024,PSP_THREAD_ATTR_USER,nullptr);if(worker<0){q.error=5;renderer.end();return false;}
 q.stage=3;unsigned held=0;while(held<pressure.size()){auto* p=std::malloc(Block);if(!p)break;pressure[held++]=p;}q.heldBlocks=held;q.heldBytes=held*Block;
 if(!held||held==pressure.size()){q.error=6;renderer.end();return false;}
 q.stage=4;const auto workerBefore=releases();sceKernelStartThread(worker,0,nullptr);sceKernelWaitThreadEnd(worker,nullptr);sceKernelDeleteThread(worker);q.workerReleased=releases()-workerBefore;
 q.stage=5;const auto lockedBefore=releases();try{void* p=::operator new(Request);::operator delete(p);}catch(const std::bad_alloc&){q.lockedFailed=1;}q.lockedReleased=releases()-lockedBefore;
 capture(beforePixels);renderer.end();q.stage=6;
 void* critical=nullptr;try{critical=::operator new(Request);q.criticalSuccess=1;static_cast<volatile unsigned char*>(critical)[0]=0x5a;static_cast<volatile unsigned char*>(critical)[Request-1]=0xa5;}catch(const std::bad_alloc&){}
 q.stage=7;::operator delete(critical);for(unsigned i=0;i<held;++i)std::free(pressure[i]);
 // Reclaimed pixels must decode and draw identically on the next list.
 q.stage=8;draw(art,count);capture(afterPixels);renderer.end();
 auto a=uncached(beforePixels),b=uncached(afterPixels);std::uint64_t hash=14695981039346656037ull;
 for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x){const auto index=y*512+x;++q.pixels;q.colored+=(a[index]&0xffffff)!=0;q.mismatch+=a[index]!=b[index];for(unsigned j=0;j<4;++j)hash=(hash^((b[index]>>(8*j))&255))*1099511628211ull;}
 q.hashLow=hash;q.hashHigh=hash>>32;
#if HAS_RECOVERY
 q.requests=g_allocationRecoveryQa.requests;q.releases=g_allocationRecoveryQa.releases;q.releasedBytes=g_allocationRecoveryQa.bytes;
#endif
 q.stage=9;if(!q.workerFailed||q.workerReleased||!q.lockedFailed||q.lockedReleased){q.error=10;return false;}if(q.mismatch||q.colored<100){q.error=11;return false;}
 if(!q.criticalSuccess){q.error=20;return false;}
 if(!HAS_RECOVERY||!q.releases||!q.releasedBytes){q.error=21;return false;}return true;
}
}
int main(int argc,char**argv){std::string folder=argc?argv[0]:"";const auto at=folder.find_last_of('/');folder=at==std::string::npos?"":folder.substr(0,at+1);scePowerSetClockFrequency(333,333,166);g_allocationPressureQa.status=1;const bool pass=run(folder);g_allocationPressureQa.status=pass?2:3;sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();}
