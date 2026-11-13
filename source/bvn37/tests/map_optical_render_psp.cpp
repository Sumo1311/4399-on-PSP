// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include <array>
#include <bit>
#include <cstdio>
#include <string>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Map Optical GPU QA",PSP_MODULE_USER,0,50);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(16384);
extern "C" {
struct OpticalGpuQA {unsigned status,error,cases,pixels,mismatch,firstCase,x,y,expected,actual,visibleCases,alphaCases,alphaPixels;} g_opticalGpuQa;
extern volatile struct RendererQA {unsigned frames,additive,darken,color,alpha,combatDraws,hardlight,lighten,screen,textureMisses,combatTiles;}g_rendererQa;
extern volatile struct BlurRendererQA {unsigned layers,tiles,missing,culled;}g_blurRendererQa;
extern volatile struct OpticalRendererQA {unsigned layers,tiles,missing,cached,normal;}g_opticalRendererQa;
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
    FILE* input=std::fopen((folder+"OPTICALGPU.IN").c_str(),"r");if(!input){g_opticalGpuQa.error=1;return false;}
    unsigned count;if(std::fscanf(input,"%u",&count)!=1||!count||count>4096){g_opticalGpuQa.error=2;return false;}
    bvn::StageRenderer renderer;renderer.start();
    for(unsigned row=0;row<count;++row){
        char name[64],expected[64];unsigned kind,mask;
        if(std::fscanf(input,"%63s%u%u%63s",name,&kind,&mask,expected)!=4||kind>=6){g_opticalGpuQa.error=3;return false;}
        renderer.clearFighterTextures();bvn::StageAsset stage,reference;bvn::FighterAsset blur,optical;
        if(!stage.open((folder+name+".BVN").c_str())||!reference.open((folder+expected).c_str())||
            !blur.open((folder+name+".BVB").c_str(),bvn::FighterAsset::TextureResidency::LowMemory)||
            !optical.open((folder+name+".BVO").c_str(),bvn::FighterAsset::TextureResidency::LowMemory)){g_opticalGpuQa.error=4;return false;}
        const auto fullBytes=stage.residentTextureBytes();stage.useOpticalPalette();
        if(stage.residentTextureBytes()>=fullBytes){g_opticalGpuQa.error=9;return false;}
        for(unsigned cameraCase=0;cameraCase<2;++cameraCase)for(unsigned quality:{0U,3U}){
            const auto& children=stage.optical().children();
            const unsigned selected=mask?std::countr_zero(mask):0;
            const auto& focus=children[selected];const double zoom=cameraCase?2.2:1.;
            const double targetY=focus.y+focus.height*.5;
            double cameraY=(targetY+60-300/zoom)/1.1;
            if(cameraY>600)cameraY=targetY-300/zoom;
            const bvn::CameraView camera{{(focus.x+focus.width*.5-400/zoom)/1.1,cameraY},zoom};
            bvn::SceneEffects view;view.shiftX=cameraCase?5:-3;view.shiftY=cameraCase?-2:4;view.shadeAlpha=cameraCase?179:0;
            renderer.quality(kind?0:quality);renderer.begin();renderer.arena(reference,camera,{},view);copy(referencePixels);renderer.end();
            view.optical.active=true;view.optical.mask=mask;
            if(kind){view.blur.x=bvn::MapBlurStrengths[kind-1];view.optical.cached[kind-1]=mask;}
            const auto tileCount=g_opticalRendererQa.tiles;
            renderer.quality(quality);renderer.begin();renderer.arena(stage,camera,{},view,nullptr,nullptr,nullptr,&blur,&optical);copy(actualPixels);renderer.end();
            const auto* a=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(referencePixels.data())|0x40000000U);
            const auto* b=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(actualPixels.data())|0x40000000U);
            for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x){++g_opticalGpuQa.pixels;
                if(a[y*512+x]!=b[y*512+x]){g_opticalGpuQa.error=5;g_opticalGpuQa.firstCase=g_opticalGpuQa.cases;
                    g_opticalGpuQa.x=x;g_opticalGpuQa.y=y;g_opticalGpuQa.expected=a[y*512+x];g_opticalGpuQa.actual=b[y*512+x];++g_opticalGpuQa.mismatch;return false;}}
            if(g_rendererQa.textureMisses||g_blurRendererQa.missing||g_opticalRendererQa.missing){g_opticalGpuQa.error=6;return false;}
            if(g_opticalRendererQa.tiles==tileCount){g_opticalGpuQa.error=7;return false;}
            ++g_opticalGpuQa.visibleCases;
            if(mask){
                view.optical.mask=0;if(kind)view.optical.cached[kind-1]=0;
                renderer.begin();renderer.arena(stage,camera,{},view,nullptr,nullptr,nullptr,&blur,&optical);copy(referencePixels);renderer.end();
                unsigned changed=0;for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x)changed+=a[y*512+x]!=b[y*512+x];
                if(changed){++g_opticalGpuQa.alphaCases;g_opticalGpuQa.alphaPixels+=changed;}
                if(mask==(1u<<children.size())-1&&!changed){g_opticalGpuQa.error=8;return false;}
            }
            ++g_opticalGpuQa.cases;
        }
    }
    std::fclose(input);return true;
}
}
int main(int argc,char** argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_opticalGpuQa.status=1;const bool pass=run(folder);
    g_opticalGpuQa.status=pass?2:3;sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
