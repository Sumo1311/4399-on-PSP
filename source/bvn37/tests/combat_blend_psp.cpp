// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Combat Blend QA",PSP_MODULE_USER,0,43);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct CombatBlendQA {unsigned status,error,frames,cases,pixels,maxError,firstFrame,mode,alpha,x,expected,actual,quality,parity;}g_combatBlendQa;}
namespace {
constexpr float Scale=bvn::StageTextureScale,Pillar=(480-800*Scale)/2;
alignas(64) std::array<unsigned,512*272> readback;
std::array<char,65536> fileBuffer;
unsigned ch(unsigned rgb,unsigned c){return rgb>>(16-8*c)&255;}
unsigned abgr(unsigned rgb,unsigned a=255){return a<<24|(rgb&255)<<16|(rgb&0xff00)|rgb>>16;}
unsigned rgb(unsigned pixel){return (pixel&255)<<16|(pixel&0xff00)|(pixel>>16&255);}
unsigned source(unsigned x){return x<<16|(x*73&255)<<8|(255-x);}
unsigned background(unsigned x,unsigned frame){return ((x*137+frame*23)&255)<<16|((x*19+frame*13)&255)<<8|((x*73+frame*11)&255);}
unsigned over(unsigned bg,unsigned color,unsigned a){unsigned value=0;for(unsigned c=0;c<3;++c)value|=((ch(color,c)*a+ch(bg,c)*(255-a)+127)/255)<<(16-8*c);return value;}
unsigned ideal(unsigned bg,unsigned color,unsigned alpha,unsigned mode){
    unsigned value=0;
    for(unsigned c=0;c<3;++c){
        const double b=ch(bg,c),s=ch(color,c),a=alpha/255.;double out=0;
        if(mode==0)out=b*(1-a)+s*a;
        if(mode==1)out=std::min(255.,b+s*a);
        if(mode==2)out=b*(1-a)+std::max(b,s)*a;
        if(mode==3)out=b*(1-a)+(s<128?2*s*b/255:255-2*(255-s)*(255-b)/255)*a;
        if(mode==4)out=b+(255-b)*s*a/255;
        value|=unsigned(std::clamp(std::lround(out),0L,255L))<<(16-8*c);
    }
    return value;
}
void box(bvn::StageRenderer& r,float x,float y,float w,float h,unsigned color){r.box((x-Pillar)/Scale,y/Scale,w/Scale,h/Scale,color);}
bool run(const std::string& folder){
    bvn::FighterAsset asset;if(!asset.open((folder+"BLEND.BVF").c_str(),bvn::FighterAsset::TextureResidency::LowMemory)){g_combatBlendQa.error=1;return false;}
    FILE* out=std::fopen((folder+"BLEND.OUT").c_str(),"wb");if(!out){g_combatBlendQa.error=2;return false;}
    std::setvbuf(out,fileBuffer.data(),_IOFBF,fileBuffer.size());
    std::fwrite("BVNCBL02",1,8,out);const unsigned empty[4]{};std::fwrite(empty,4,4,out);
    bvn::StageRenderer renderer;renderer.start();
    for(unsigned alpha=0;alpha<256;++alpha)for(unsigned quality:{0U,3U})for(unsigned parity:{0U,1U}){
        const unsigned frame=g_combatBlendQa.frames+1;renderer.quality(quality);renderer.begin();
        const int shiftX=(int(alpha%3)-1)*3,shiftY=(int(alpha%5)-2)*2;
        renderer.rootOffset({shiftX/double(Scale),shiftY/double(Scale)});
        for(unsigned x=0;x<256;++x)box(renderer,64+x,0,1,272,abgr(background(x,frame)));
        for(unsigned mode=0;mode<5;++mode){
            const unsigned y=12+mode*45;
            renderer.effectPose(asset,asset.frame(bvn::MoveClip(9),alpha).pose,{64-Pillar,double(y)},1,1,0,bvn::EffectBlend(mode));
            box(renderer,64,y+1,256,1,abgr(0x21d89b,99));
        }
        void* drawn=static_cast<std::byte*>(sceGeEdramGetAddr())+(frame%2?0:512*272*4);
        sceKernelDcacheWritebackInvalidateRange(readback.data(),sizeof(readback));
        sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,drawn,0,0,512,readback.data());sceGuTexSync();renderer.end();
        const auto* pixels=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(readback.data())|0x40000000U);
        const int header[]{int(frame),int(alpha),int(quality),int(parity),shiftX,shiftY};std::fwrite(header,4,6,out);
        for(unsigned mode=0;mode<5;++mode){
            const unsigned data[]{mode,512};std::fwrite(data,4,2,out);
            for(unsigned overlay:{0U,1U})for(unsigned x=0;x<256;++x){
                const unsigned bg=background(x,frame),src=source(x),actual=rgb(pixels[(12+mode*45+overlay+shiftY)*512+64+x+shiftX]);
                unsigned expected=ideal(bg,src,alpha,mode);if(overlay)expected=over(expected,0x21d89b,99);
                unsigned error=0;for(unsigned c=0;c<3;++c)error=std::max(error,unsigned(std::abs(int(ch(actual,c))-int(ch(expected,c)))));
                g_combatBlendQa.maxError=std::max(g_combatBlendQa.maxError,error);++g_combatBlendQa.pixels;
                const unsigned sample[]{abgr(src,alpha),bg,actual,overlay};std::fwrite(sample,4,4,out);
                const unsigned tolerance=mode==3?2:mode==4?1:0;
                if(error>tolerance){g_combatBlendQa.error=3;g_combatBlendQa.firstFrame=frame;g_combatBlendQa.mode=mode;
                    g_combatBlendQa.alpha=alpha;g_combatBlendQa.x=x;g_combatBlendQa.expected=expected;g_combatBlendQa.actual=actual;
                    g_combatBlendQa.quality=quality;g_combatBlendQa.parity=parity;std::fclose(out);return false;}
            }
            ++g_combatBlendQa.cases;
        }
        for(unsigned x:{0U,479U}){const unsigned pixel=rgb(pixels[100*512+x]);std::fwrite(&pixel,4,1,out);++g_combatBlendQa.pixels;if(pixel){g_combatBlendQa.error=4;std::fclose(out);return false;}}
        ++g_combatBlendQa.frames;
    }
    std::fseek(out,8,SEEK_SET);const unsigned counts[]{g_combatBlendQa.frames,g_combatBlendQa.cases,g_combatBlendQa.pixels,g_combatBlendQa.maxError};std::fwrite(counts,4,4,out);
    return std::fclose(out)==0;
}
}
int main(int argc,char**argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_combatBlendQa.status=1;const bool pass=run(folder);g_combatBlendQa.status=pass?2:3;
    sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
