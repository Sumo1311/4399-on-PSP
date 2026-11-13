// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include <array>
#include <cstdio>
#include <string>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Map Blur GPU QA",PSP_MODULE_USER,0,49);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(16384);
extern "C" {
struct MapBlurGpuQA {unsigned status,error,cases,pixels,mismatch,firstCase,x,y,expected,actual;} g_mapBlurGpuQa;
extern volatile struct RendererQA {unsigned frames,additive,darken,color,alpha,combatDraws,hardlight,lighten,screen,textureMisses,combatTiles;}g_rendererQa;
extern volatile struct BlurRendererQA {unsigned layers,tiles,missing,culled;}g_blurRendererQa;
}
namespace {
alignas(64) std::array<unsigned,512*272> referencePixels,actualPixels;
unsigned frames=0;
void copy(std::array<unsigned,512*272>& pixels){
    void* vram=static_cast<std::byte*>(sceGeEdramGetAddr());void* drawn=static_cast<std::byte*>(vram)+(++frames%2?0:512*272*4);
    sceKernelDcacheWritebackInvalidateRange(pixels.data(),sizeof(pixels));
    sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,drawn,0,0,512,pixels.data());sceGuTexSync();
}
bool run(const std::string& folder){
    FILE* input=std::fopen((folder+"BLURGPU.IN").c_str(),"r");if(!input){g_mapBlurGpuQa.error=1;return false;}
    unsigned count;if(std::fscanf(input,"%u",&count)!=1||count!=95){g_mapBlurGpuQa.error=2;return false;}
    bvn::StageRenderer renderer;renderer.start();
    for(unsigned row=0;row<count;++row){
        char name[64],expected[64];unsigned variant;
        if(std::fscanf(input,"%63s %u %63s",name,&variant,expected)!=3||variant>=5){g_mapBlurGpuQa.error=3;return false;}
        renderer.clearFighterTextures();bvn::StageAsset stage,reference;bvn::FighterAsset bank;
        if(!stage.open((folder+name+".BVN").c_str())||!reference.open((folder+expected).c_str())||
            !bank.open((folder+name+".BVB").c_str(),bvn::FighterAsset::TextureResidency::LowMemory)){g_mapBlurGpuQa.error=4;return false;}
        for(unsigned cameraCase=0;cameraCase<2;++cameraCase)for(unsigned quality:{0U,3U}){
            const bvn::CameraView camera{{cameraCase?450.:0.,cameraCase?300.:0.},cameraCase?2.2:1.};
            bvn::SceneEffects view;view.shiftX=cameraCase?5:-3;view.shiftY=cameraCase?-2:4;view.shadeAlpha=cameraCase?179:0;
            renderer.quality(0);renderer.begin();renderer.arena(reference,camera,{},view);copy(referencePixels);renderer.end();
            view.blur.x=bvn::MapBlurStrengths[variant];renderer.quality(quality);renderer.begin();
            renderer.arena(stage,camera,{},view,nullptr,nullptr,nullptr,&bank);copy(actualPixels);renderer.end();
            const auto* a=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(referencePixels.data())|0x40000000U);
            const auto* b=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(actualPixels.data())|0x40000000U);
            for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x){++g_mapBlurGpuQa.pixels;
                if(a[y*512+x]!=b[y*512+x]){g_mapBlurGpuQa.error=5;g_mapBlurGpuQa.firstCase=g_mapBlurGpuQa.cases;
                    g_mapBlurGpuQa.x=x;g_mapBlurGpuQa.y=y;g_mapBlurGpuQa.expected=a[y*512+x];g_mapBlurGpuQa.actual=b[y*512+x];++g_mapBlurGpuQa.mismatch;return false;}
            }
            if(g_rendererQa.textureMisses||g_blurRendererQa.missing){g_mapBlurGpuQa.error=6;return false;}
            ++g_mapBlurGpuQa.cases;
        }
    }
    std::fclose(input);return true;
}
}
int main(int argc,char** argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_mapBlurGpuQa.status=1;const bool pass=run(folder);
    g_mapBlurGpuQa.status=pass?2:3;sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
