// SPDX-License-Identifier: GPL-3.0-or-later
// Actual game renderer: every retained blend-layer texture, independent RGB oracle.
#include "psp_stage_renderer.hpp"
#include "resource_diagnostics.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <pspkernel.h>
#include <psppower.h>
#include <pspctrl.h>
PSP_MODULE_INFO("BVN Layer Blend Audit",PSP_MODULE_USER,0,1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(-1024);
extern "C" {
struct LayerAuditQA {unsigned status,error,banks,textures,frames,pixels,failed,maxError,black,bank,texture,quality,alpha,backend;} g_layerAuditQa;
extern volatile struct TextureCacheQA {unsigned budget,allocation,decode,requested,resident,locked,retryAllocation,retryDecode,evicted;}g_textureCacheQa;
extern volatile struct GeSubmissionQA {unsigned lists,maxBytes,submitErrors,deferredLists;}g_geSubmissionQa;
}
namespace {
using namespace bvn;
constexpr float Scale=StageTextureScale,Pillar=(480-800*Scale)/2;
alignas(64) std::array<std::uint32_t,512*272> pixels;
StageRenderer renderer;StageAsset empty;
std::vector<std::byte> decoded;
FILE* report=nullptr;
unsigned serial=0;
unsigned bg(unsigned x,unsigned y,unsigned phase){return ((x/16+y/16+phase)&1)?0xff29ad5b:0xffd549cd;}
void box(float x,float y,float w,float h,unsigned color){renderer.box((x-Pillar)/Scale,y/Scale,w/Scale,h/Scale,color);}
unsigned ideal(const FighterAsset& a,const FighterPose& pose,unsigned x,unsigned y,unsigned background,unsigned opacity){
    const auto modes=a.textureLayerModes(pose);const auto count=a.textureLayers(pose),density=a.textureScale(pose);
    const auto plane=a.pixelBytes(pose)/count;unsigned out=0;
    for(unsigned c=0;c<3;++c){
        const double back=(background>>(8*c))&255;double value=back;
        for(unsigned p=0;p<count;++p){
            const auto* src=reinterpret_cast<const unsigned char*>(decoded.data())+p*plane+4*(y*pose.stride*density+x);
            const double s=src[c],alpha=src[3]/255.;const auto mode=(modes>>(4+4*p))&15u;
            if(mode==4)value=value+(255-value)*s/255.;
            else if(mode==10)value=s+(1-2*s/255.)*value;
            else if(mode==15)value=value*s/255.;
            else if(mode==8)value=std::min(255.,value+s*alpha);
            else if(mode==9)value=std::max(0.,value-s*alpha);
            else if(mode==5)value=value*(1-alpha)+std::max(value,s)*alpha;
            else if(mode==6)value=value*(1-alpha)+std::min(value,s)*alpha;
            else value=value*(1-alpha)+s*alpha;
        }
        value=back+(value-back)*opacity/255.;out|=unsigned(std::clamp(std::lround(value),0L,255L))<<(8*c);
    }
    return out;
}
bool check(const FighterAsset& a,FighterPose pose,unsigned alpha,bool flip,unsigned quality,bool deferred){
    auto& q=g_layerAuditQa;q.alpha=alpha;q.quality=quality;q.backend=deferred;
    decoded.resize(a.pixelBytes(pose));if(!a.decodePixels(pose,decoded)){q.error=3;return false;}
    double zoom=1.;while(pose.width*zoom>240||pose.height*zoom>176)zoom*=.5;
    const unsigned w=unsigned(std::ceil(pose.width*zoom)),h=unsigned(std::ceil(pose.height*zoom)),density=a.textureScale(pose);
    const unsigned phase=serial++;renderer.deferred(deferred);renderer.quality(quality);renderer.begin();
    for(unsigned y=0;y<192;y+=16)for(unsigned x=0;x<256;x+=16)box(64+x,16+y,16,16,bg(x,y,phase));
    pose.x=pose.y=0;
    const FighterView view{a,pose,{(64-Pillar)/(Scale*zoom)+(flip?pose.width/Scale:0),16/(Scale*zoom)},flip?-1:1,nullptr,alpha};
    SceneEffects scene{};scene.hideMap=true;renderer.arena(empty,{{},zoom},std::span(&view,1),scene);renderer.end();
    if(!renderer.readback(pixels)){q.error=4;return false;}q.frames+=1;
    const auto* actual=reinterpret_cast<const volatile std::uint32_t*>(reinterpret_cast<std::uintptr_t>(pixels.data())|0x40000000u);
    unsigned bad=0,worst=0,firstX=0,firstY=0,firstActual=0,firstExpected=0;
    auto sample=[&](unsigned x,unsigned y){
        const unsigned observed=actual[(16+y)*512+64+x]&0xffffff,background=bg(x,y,phase)&0xffffff;
        unsigned best=~0u,expected=background;
        if(x+.5<pose.width*zoom&&y+.5<pose.height*zoom){
            const unsigned sx=std::min(unsigned((x+.5)*density/zoom),unsigned(pose.width*density-1));
            const unsigned sy=std::min(unsigned((y+.5)*density/zoom),unsigned(pose.height*density-1));
            for(int dy=-1;dy<=1&&best>0;++dy)for(int dx=-1;dx<=1&&best>0;++dx){
                if(zoom==1&&density==1&&(dx||dy))continue;
                const unsigned tx=std::clamp(int(sx)+dx,0,int(pose.width*density-1)),ty=std::clamp(int(sy)+dy,0,int(pose.height*density-1));
                const auto color=ideal(a,pose,flip?pose.width*density-1-tx:tx,ty,background,alpha);unsigned error=0;
                for(unsigned c=0;c<3;++c)error=std::max(error,unsigned(std::abs(int(observed>>(8*c)&255)-int(color>>(8*c)&255))));
                if(error<best){best=error;expected=color;}
            }
        }else{best=0;for(unsigned c=0;c<3;++c)best=std::max(best,unsigned(std::abs(int(observed>>(8*c)&255)-int(background>>(8*c)&255))));}
        q.pixels+=1;worst=std::max(worst,best);
        const unsigned tolerance=3+(a.textureLayers(pose)+1)/2;
        if(best>tolerance){if(!bad){firstX=x;firstY=y;firstActual=observed;firstExpected=expected;}++bad;q.black+=observed==0&&expected!=0;}
    };
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)sample(x*(w-1)/15,y*(h-1)/15);
    for(unsigned y:{0u,h/2,h-1})for(unsigned x:{0u,w/2,w-1})sample(x,y);
    sample(std::min(w+2,255u),std::min(h+2,191u));q.maxError=std::max(q.maxError,worst);
    if(bad){q.failed+=1;if(q.failed<128)std::fprintf(report,"FAIL bank=%u texture=%u layers=%u alpha=%u flip=%u quality=%u queued=%u bad=%u max=%u xy=%u,%u expected=%06x actual=%06x\n",q.bank,pose.offset,a.textureLayers(pose),alpha,flip,quality,deferred,bad,worst,firstX,firstY,firstExpected,firstActual);}
    return true;
}
bool run(const std::string& folder){
#ifdef BVN_RESOURCE_DIAGNOSTICS
    resourceDiagnostics.synchronousOnly=true;
#endif
    report=std::fopen((folder+"LAYER_AUDIT.TXT").c_str(),"w");if(!report){g_layerAuditQa.error=1;return false;}
    std::fprintf(report,"Actual game renderer, all unique authored layer textures; independent normal/screen/add/subtract/multiply/min/max RGB oracle; native-density sampling, alternating checker background, flipped/faded groups, DIRECT and queued. Not a hardware/FPS claim.\n");
    std::ifstream names(folder+"LAYER_BANKS.TXT");std::string name;renderer.start(false);
    while(std::getline(names,name)){
        renderer.clearFighterTextures();FighterAsset a;
        if(!a.open((folder+"../BVN37/"+name).c_str(),FighterAsset::TextureResidency::LowMemory)){g_layerAuditQa.error=2;return false;}
        std::map<unsigned,FighterPose> poses;
        for(unsigned c=0;c<a.clipCount();++c)for(unsigned f=0;f<a.clip(MoveClip(c)).count;++f)
            for(const auto& p:a.poseTiles(a.frame(MoveClip(c),f).pose))if(a.blendTransfer(p))poses[p.offset]=p;
        std::fprintf(report,"BANK %u %s layered_textures=%u\n",g_layerAuditQa.bank,name.c_str(),unsigned(poses.size()));std::fflush(report);
        for(const auto&[id,pose]:poses){
            g_layerAuditQa.texture=id;
            if(!check(a,pose,255,false,0,true)||!check(a,pose,128,true,3,false))return false;
            if(g_layerAuditQa.textures%64==0){if(!check(a,pose,64,false,3,true)||!check(a,pose,192,true,0,true))return false;}
            g_layerAuditQa.textures+=1;
        }
        g_layerAuditQa.banks+=1;g_layerAuditQa.bank+=1;renderer.clearFighterTextures();
    }
    return g_layerAuditQa.banks>0&&g_layerAuditQa.textures>0;
}
}
int main(int argc,char**argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_layerAuditQa.status=1;bool complete=false;
    try{complete=run(folder);}catch(...){g_layerAuditQa.error=99;}
    auto& q=g_layerAuditQa;const bool pass=complete&&!q.failed&&!q.error&&!g_textureCacheQa.budget&&!g_textureCacheQa.allocation&&!g_textureCacheQa.decode&&!g_geSubmissionQa.submitErrors;
    if(report){std::fprintf(report,"RESULT pass=%u banks=%u textures=%u frames=%u pixels=%u failed=%u max=%u black=%u error=%u cache=%u,%u,%u GE=%u,%u\n",pass,q.banks,q.textures,q.frames,q.pixels,q.failed,q.maxError,q.black,q.error,unsigned(g_textureCacheQa.budget),unsigned(g_textureCacheQa.allocation),unsigned(g_textureCacheQa.decode),unsigned(g_geSubmissionQa.maxBytes),unsigned(g_geSubmissionQa.submitErrors));std::fclose(report);}
    q.status=pass?2:3;for(;;){SceCtrlData p{};sceCtrlReadBufferPositive(&p,1);if(p.Buttons&PSP_CTRL_CROSS)break;sceKernelDelayThread(20000);}sceKernelExitGame();
}
