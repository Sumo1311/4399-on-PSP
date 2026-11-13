// Verify the real FighterView tint/cache path using GE-to-RAM pixels.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Actor Tint QA",PSP_MODULE_USER,0,44);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C"{struct TintQA{unsigned status,error,frames,cases,pixels,maxError,firstFrame,tint,x,expected,actual;}g_tintQa;}
namespace {
alignas(64)std::array<unsigned,512*272> readback;
constexpr bvn::ShadowTint Tints[]{{0,0,0},{-255,-255,-255},{0,0,255},{50,-75,255},{150,150,150},{150,150,0}};
constexpr double Scale=bvn::StageTextureScale,Pillar=(480-800*Scale)/2;
unsigned rgb(unsigned v){return (v&255)<<16|(v&0xff00)|(v>>16&255);}
bool run(const std::string& folder){
    bvn::FighterAsset asset;bvn::StageAsset stage;
    if(!asset.open((folder+"BLEND.BVF").c_str(),bvn::FighterAsset::TextureResidency::LowMemory)||
       !stage.open((folder+"JIANCHANG.BVN").c_str())){g_tintQa.error=1;return false;}
    FILE* out=std::fopen((folder+"TINT.OUT").c_str(),"wb");if(!out){g_tintQa.error=2;return false;}
    std::fwrite("BVNTINT1",1,8,out);const unsigned empty[4]{};std::fwrite(empty,4,4,out);
    bvn::StageRenderer renderer;renderer.start();
    bvn::SceneEffects scene;scene.hideMap=true;
    for(unsigned alpha:{0U,1U,64U,127U,128U,192U,254U,255U})for(unsigned quality:{0U,3U})for(unsigned parity:{0U,1U}){
        renderer.quality(quality);renderer.begin();std::vector<bvn::FighterView> actors;
        for(unsigned i=0;i<6;++i)actors.push_back({asset,asset.frame(bvn::MoveClip(9),alpha).pose,
            {(64-Pillar)/Scale,(12+i*37)/Scale},1,nullptr,255,Tints[i]});
        renderer.arena(stage,{{0,0},1},actors,scene);
        const unsigned frame=g_tintQa.frames+1;
        void* drawn=static_cast<std::byte*>(sceGeEdramGetAddr())+(frame%2?0:512*272*4);
        sceKernelDcacheWritebackInvalidateRange(readback.data(),sizeof(readback));
        sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,drawn,0,0,512,readback.data());sceGuTexSync();renderer.end();
        const auto* pixels=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(readback.data())|0x40000000U);
        const unsigned header[]{frame,alpha,quality,parity};std::fwrite(header,4,4,out);
        for(unsigned tint=0;tint<6;++tint){
            const int offset[]{Tints[tint].red,Tints[tint].green,Tints[tint].blue};
            for(unsigned row=0;row<2;++row)for(unsigned x=0;x<256;++x){
                const unsigned actual=rgb(pixels[(12+tint*37+row)*512+64+x]);
                const int source[]{int(x),int(x*73&255),int(255-x)};unsigned expected=0;
                for(unsigned c=0;c<3;++c)expected|=unsigned((std::clamp(source[c]+offset[c],0,255)*alpha+127)/255)<<(16-8*c);
                std::fwrite(&actual,4,1,out);++g_tintQa.pixels;
                if(actual!=expected){g_tintQa.error=3;g_tintQa.firstFrame=frame;g_tintQa.tint=tint;
                    g_tintQa.x=x;g_tintQa.expected=expected;g_tintQa.actual=actual;std::fclose(out);return false;}
            }
            ++g_tintQa.cases;
        }
        ++g_tintQa.frames;
    }
    std::fseek(out,8,SEEK_SET);const unsigned counts[]{g_tintQa.frames,g_tintQa.cases,g_tintQa.pixels,g_tintQa.maxError};std::fwrite(counts,4,4,out);
    return std::fclose(out)==0;
}
}
int main(int argc,char**argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_tintQa.status=1;const bool pass=run(folder);g_tintQa.status=pass?2:3;
    sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
