// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include <array>
#include <string>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Transition GPU QA",PSP_MODULE_USER,0,57);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {
struct TransitionGpuQA{unsigned status,error,cases,pixels,mismatch,frame,x,y,expected,actual,visibleCases,alphaBytes;}g_transitionGpuQa;
extern volatile struct TransitionRendererQA{unsigned draws,frame,decodeFailures;}g_transitionRendererQa;
}
namespace {
alignas(64) std::array<unsigned,512*272> expectedPixels,actualPixels;
alignas(64) std::array<unsigned,512*512> colorTexture;
unsigned frames=0;
void copy(std::array<unsigned,512*272>& pixels){
 void* vram=static_cast<std::byte*>(sceGeEdramGetAddr());void* drawn=static_cast<std::byte*>(vram)+(++frames%2?0:512*272*4);
 sceKernelDcacheWritebackInvalidateRange(pixels.data(),sizeof(pixels));
 sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,drawn,0,0,512,pixels.data());sceGuTexSync();
}
void reference(const bvn::TransitionAsset& asset){
 struct Vertex{float u,v;unsigned color;float x,y,z;};
 constexpr float width=800.f*272/600,pillar=(480-width)/2;
 sceGuTexMode(GU_PSM_8888,0,0,GU_FALSE);sceGuTexImage(0,asset.stride(),asset.rows(),asset.stride(),colorTexture.data());
 sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFlush();
 auto* v=static_cast<Vertex*>(sceGuGetMemory(sizeof(Vertex)*2));
 v[0]={0,0,0xffffffff,pillar,0,0};v[1]={float(asset.width()),float(asset.height()),0xffffffff,pillar+width,272,0};
 sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,nullptr,v);
}
bool run(const std::string& folder){
 bvn::TransitionAsset asset;if(!asset.open((folder+"TRANS.BVT").c_str())){g_transitionGpuQa.error=1;return false;}
 g_transitionGpuQa.alphaBytes=asset.residentBytes();bvn::StageRenderer renderer;renderer.start();
 for(unsigned frame=0;frame<=60;++frame){if(frame&&!(frame>=9&&frame<=25)&&!(frame>=44&&frame<=60))continue;
  const auto* raw=static_cast<const unsigned char*>(asset.pixels(frame));if(!raw){g_transitionGpuQa.error=2;return false;}
  for(unsigned i=0;i<colorTexture.size();++i)colorTexture[i]=unsigned(raw[i])<<24;
  sceKernelDcacheWritebackRange(colorTexture.data(),sizeof(colorTexture));
  for(unsigned color:{0xff194f8au,0xffe9ac43u,0xff000fffu})for(unsigned offset=0;offset<2;++offset){
   renderer.begin();renderer.beginUi();renderer.box(0,0,800,600,color);reference(asset);copy(expectedPixels);renderer.end();
   renderer.begin();renderer.beginUi();renderer.box(0,0,800,600,color);if(offset)renderer.rootOffset({100,-75});
   if(!renderer.transition(asset,frame)){g_transitionGpuQa.error=3;return false;}copy(actualPixels);renderer.end();
   const auto* a=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(expectedPixels.data())|0x40000000U);
   const auto* b=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(actualPixels.data())|0x40000000U);
   for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x){++g_transitionGpuQa.pixels;if(a[y*512+x]!=b[y*512+x]){
    g_transitionGpuQa.error=4;++g_transitionGpuQa.mismatch;g_transitionGpuQa.frame=frame;g_transitionGpuQa.x=x;g_transitionGpuQa.y=y;g_transitionGpuQa.expected=a[y*512+x];g_transitionGpuQa.actual=b[y*512+x];return false;}}
   if(g_transitionRendererQa.frame!=frame||g_transitionRendererQa.decodeFailures){g_transitionGpuQa.error=5;return false;}
   ++g_transitionGpuQa.visibleCases;++g_transitionGpuQa.cases;
  }
 }
 return true;
}
}
int main(int argc,char** argv){
 std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_transitionGpuQa.status=1;const bool pass=run(folder);g_transitionGpuQa.status=pass?2:3;
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
