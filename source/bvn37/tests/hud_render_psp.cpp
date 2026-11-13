// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include <array>
#include <algorithm>
#include <string>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN HUD GPU QA",PSP_MODULE_USER,0,58);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct HudGpuQA{unsigned status,error,cases,pixels,mismatch,sprite,offset,alpha,flip,linear,x,y,expected,actual;}g_hudGpuQa;}
namespace {
alignas(64) std::array<unsigned,512*272> referencePixels,nativePixels;
alignas(64) std::array<unsigned,512*512> expanded;
unsigned frames=0;
void capture(std::array<unsigned,512*272>& pixels){
    auto* vram=static_cast<std::byte*>(sceGeEdramGetAddr());auto* drawn=vram+(++frames%2?0:512*272*4);
    sceKernelDcacheWritebackInvalidateRange(pixels.data(),sizeof(pixels));
    sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,drawn,0,0,512,pixels.data());sceGuTexSync();
}
void reference(const bvn::UiSprite& sprite,unsigned alpha,bool flip,unsigned offset,bool linear){
    struct Vertex{float u,v;unsigned color;float x,y,z;};
    constexpr double scale=272./600;constexpr float pillar=(480-float(800*scale))/2;
    sceGuTexMode(GU_PSM_8888,0,0,GU_FALSE);sceGuTexImage(0,sprite.stride,sprite.rows,sprite.stride,expanded.data());
    sceGuTexFunc(linear?GU_TFX_ADD:GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(linear?GU_LINEAR:GU_NEAREST,linear?GU_LINEAR:GU_NEAREST);sceGuTexFlush();
    const unsigned tint=(alpha<<24)|(linear?offset*0x010101u:0xffffff);
    auto* v=static_cast<Vertex*>(sceGuGetMemory(2*sizeof(Vertex)));
    v[0]={flip?float(sprite.width):0,0,tint,pillar+34,68,0};
    v[1]={flip?0:float(sprite.width),float(sprite.height),tint,pillar+306,204,0};
    sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,nullptr,v);
}
bool run(const std::string& folder){
    bvn::UiAsset art;if(!art.open((folder+"HUD.BVU").c_str())){g_hudGpuQa.error=1;return false;}
    constexpr std::array<const char*,21> keys{"hp-red","energy-low","energy-bleach-low","energy-naruto-low","qi-max","musou-0","musou-1","musou-2","musou-3","musou-4","musou-5","musou-6","musou-7","musou-8","musou-9","musou-10","musou-11","musou-12","musou-13","musou-14","musou-15"};
    bvn::StageRenderer renderer;renderer.start();
    for(unsigned index=0;index<keys.size();++index){
        const auto* sprite=art.find(keys[index]);if(!sprite){g_hudGpuQa.error=2;return false;}
        const auto* raw=static_cast<const unsigned short*>(art.pixels(*sprite));
        for(unsigned offset:{0u,50u,150u})for(unsigned linear=0;linear<2;++linear){
            for(unsigned i=0;i<unsigned(sprite->stride)*sprite->rows;++i){
                const unsigned value=raw[i];unsigned color=((value>>12)&15)*17<<24;
                for(unsigned c=0;c<3;++c)color|=std::min(255u,((value>>(c*4))&15)*17+(linear?0:offset))<<(c*8);
                expanded[i]=color;
            }
            sceKernelDcacheWritebackRange(expanded.data(),sizeof(expanded));
            for(unsigned alpha:{64u,192u,255u})for(unsigned flip=0;flip<2;++flip){
                renderer.begin();renderer.beginUi();renderer.box(0,0,800,600,0xff437cb9);reference(*sprite,alpha,flip,offset,linear);capture(referencePixels);renderer.end();
                renderer.begin();renderer.beginUi();renderer.box(0,0,800,600,0xff437cb9);
                if(!linear)sceGuTexFilter(GU_NEAREST,GU_NEAREST);
                renderer.imageTint(art,keys[index],75,150,600,300,flip,alpha,offset);capture(nativePixels);renderer.end();
                const auto* a=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(referencePixels.data())|0x40000000u);
                const auto* b=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(nativePixels.data())|0x40000000u);
                for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x){++g_hudGpuQa.pixels;if(a[y*512+x]!=b[y*512+x]){
                    g_hudGpuQa.error=3;++g_hudGpuQa.mismatch;g_hudGpuQa.sprite=index;g_hudGpuQa.offset=offset;g_hudGpuQa.alpha=alpha;
                    g_hudGpuQa.flip=flip;g_hudGpuQa.linear=linear;g_hudGpuQa.x=x;g_hudGpuQa.y=y;g_hudGpuQa.expected=a[y*512+x];g_hudGpuQa.actual=b[y*512+x];return false;
                }}++g_hudGpuQa.cases;
            }
        }
    }
    return true;
}
}
int main(int argc,char** argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_hudGpuQa.status=1;const bool pass=run(folder);g_hudGpuQa.status=pass?2:3;
    sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
