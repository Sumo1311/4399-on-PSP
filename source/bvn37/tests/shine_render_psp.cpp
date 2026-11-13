// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>

PSP_MODULE_INFO("BVN Shine GPU QA", PSP_MODULE_USER, 0, 42);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);
extern "C" {
struct RenderProbeQA {
    unsigned status,error,cases,pixels,maxError,darkCases,addCases,multiCases;
    unsigned firstCase,expected,actual,x,y,buffer,color,alpha;
} g_renderProbeQa;
}

namespace {
constexpr float Scale=bvn::StageTextureScale;
constexpr float Pillar=(480-800*Scale)/2;
constexpr std::array<unsigned,7> Alphas{0,1,40,51,76,128,255};
std::array<char,65536> outputBuffer;
alignas(64) std::array<unsigned,512*272> readback;
unsigned channel(unsigned rgb,unsigned n){return (rgb>>(16-8*n))&255;}
unsigned abgr(unsigned rgb,unsigned alpha=255){return (alpha<<24)|((rgb&255)<<16)|(rgb&0xff00)|(rgb>>16);}
unsigned fromPixel(unsigned pixel){return ((pixel&255)<<16)|(pixel&0xff00)|((pixel>>16)&255);}
unsigned background(unsigned i,unsigned frame){return (((i+frame*7)&255)<<16)|(((i*3+frame*13)&255)<<8)|((i*73+frame*11)&255);}
unsigned blend(unsigned bg,unsigned color,unsigned alpha,bool add){
    unsigned out=0;
    for(unsigned n=0;n<3;++n){
        const unsigned b=channel(bg,n),s=channel(color,n);
        const unsigned v=add?std::min(255U,b+(s*alpha+127)/255)
            :(std::min(b,s)*alpha+b*(255-alpha)+127)/255;
        out|=v<<(16-8*n);
    }
    return out;
}
unsigned over(unsigned bg,unsigned color,unsigned alpha){
    unsigned out=0;for(unsigned n=0;n<3;++n)out|=((channel(color,n)*alpha+channel(bg,n)*(255-alpha)+127)/255)<<(16-8*n);return out;
}
void box(bvn::StageRenderer& renderer,float x,float y,float width,float height,unsigned color){
    renderer.box((x-Pillar)/Scale,y/Scale,width/Scale,height/Scale,color);
}
struct Case {unsigned id,quality,count,colors[4],alphas[4],pixels;};
struct Pixel {unsigned background,actual,overlay;};
static_assert(sizeof(Case)==48&&sizeof(Pixel)==12);

bool run(const std::string& folder){
    FILE* input=std::fopen((folder+"SHINE.IN").c_str(),"r");if(!input){g_renderProbeQa.error=1;return false;}
    unsigned count;if(std::fscanf(input,"%u",&count)!=1||!count||count>256){std::fclose(input);g_renderProbeQa.error=2;return false;}
    std::vector<unsigned> colors(count);for(auto& color:colors)if(std::fscanf(input,"%x",&color)!=1||color>0xffffff){std::fclose(input);g_renderProbeQa.error=3;return false;}
    std::fclose(input);FILE* output=std::fopen((folder+"SHINE.OUT").c_str(),"wb");if(!output){g_renderProbeQa.error=4;return false;}
    std::setvbuf(output,outputBuffer.data(),_IOFBF,outputBuffer.size());
    std::fwrite("BVNSHN01",1,8,output);const unsigned empty[2]{};std::fwrite(empty,4,2,output);
    bvn::StageRenderer renderer;renderer.start();
    for(unsigned c=0;c<colors.size();++c)for(unsigned alpha:Alphas)for(unsigned layers:{1U,2U,4U}){
        const unsigned frame=g_renderProbeQa.cases+1,quality=frame%2?0:3;
        renderer.quality(quality);renderer.begin();
        for(unsigned i=0;i<256;++i)box(renderer,64+i,0,1,272,abgr(background(i,frame)));
        std::array<bvn::ShineView,4> views{};Case record{frame,quality,layers,{},{},771};
        for(unsigned k=0;k<layers;++k){
            views[k]={colors[(c+17*k)%colors.size()],(alpha+71*k)&255};record.colors[k]=views[k].color;record.alphas[k]=views[k].alpha;
        }
        bvn::SceneEffects scene;scene.shines=std::span(views).first(layers);renderer.shine(scene);
        // A translucent primitive after the effect detects leaked MIN/ADD state.
        box(renderer,120,112,80,16,abgr(0x21d89b,99));
        // An explicit GE copy forces buffered emulators to download the final
        // framebuffer. Direct CPU VRAM loads can observe an earlier copy.
        void* drawn=static_cast<std::byte*>(sceGeEdramGetAddr())+(frame%2?0:512*272*4);
        sceKernelDcacheWritebackInvalidateRange(readback.data(),sizeof(readback));
        sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,drawn,0,0,512,readback.data());
        sceGuTexSync();renderer.end();
        void* address=nullptr;int stride=0,format=0;
        if(sceDisplayGetFrameBuf(&address,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)<0||!address||stride!=512||format!=PSP_DISPLAY_PIXEL_FORMAT_8888){std::fclose(output);g_renderProbeQa.error=5;return false;}
        if(address!=drawn){std::fclose(output);g_renderProbeQa.error=9;return false;}
        const auto* pixels=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(readback.data())|0x40000000U);
        std::fwrite(&record,sizeof(record),1,output);
        const auto check=[&](unsigned x,unsigned y,unsigned bg,bool overlay){
            unsigned expected=bg;
            for(unsigned k=0;k<layers;++k)expected=blend(expected,views[k].color,views[k].alpha,views[k].color>0x7fffff);
            if(overlay)expected=over(expected,0x21d89b,99);
            const unsigned actual=fromPixel(pixels[y*512+x]);
            unsigned error=0;for(unsigned n=0;n<3;++n)error=std::max(error,unsigned(std::abs(int(channel(actual,n))-int(channel(expected,n)))));
            g_renderProbeQa.maxError=std::max(g_renderProbeQa.maxError,error);++g_renderProbeQa.pixels;
            const Pixel sample{bg,actual,unsigned(overlay)};std::fwrite(&sample,sizeof(sample),1,output);
            const unsigned tolerance=layers+unsigned(overlay);
            if(error>tolerance){g_renderProbeQa.firstCase=frame;g_renderProbeQa.expected=expected;g_renderProbeQa.actual=actual;g_renderProbeQa.x=x;g_renderProbeQa.y=y;g_renderProbeQa.buffer=reinterpret_cast<std::uintptr_t>(address);g_renderProbeQa.color=colors[c];g_renderProbeQa.alpha=alpha;return false;}
            if(layers==1&&!overlay){
                for(unsigned n=0;n<3;++n){const auto value=channel(actual,n),before=channel(bg,n);if((colors[c]<=0x7fffff&&value>before)||(colors[c]>0x7fffff&&value<before))return false;}
            }
            return true;
        };
        for(unsigned y:{16U,256U,270U})for(unsigned i=0;i<256;++i)if(!check(64+i,y,background(i,frame),false)){std::fclose(output);g_renderProbeQa.error=6;return false;}
        if(!check(150,120,background(86,frame),true)){std::fclose(output);g_renderProbeQa.error=7;return false;}
        // Neither side pillar belongs to the Flash view.
        for(unsigned x:{0U,479U}){
            const Pixel sample{0,fromPixel(pixels[100*512+x]),0};std::fwrite(&sample,sizeof(sample),1,output);++g_renderProbeQa.pixels;
            if(sample.actual){std::fclose(output);g_renderProbeQa.error=8;return false;}
        }
        ++g_renderProbeQa.cases;
        if(colors[c]<=0x7fffff)++g_renderProbeQa.darkCases;else ++g_renderProbeQa.addCases;
        if(layers>1)++g_renderProbeQa.multiCases;
    }
    std::fseek(output,8,SEEK_SET);const unsigned counts[]{g_renderProbeQa.cases,g_renderProbeQa.pixels};std::fwrite(counts,4,2,output);
    return std::fclose(output)==0;
}
}
int main(int argc,char** argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_renderProbeQa.status=1;const bool pass=run(folder);g_renderProbeQa.status=pass?2:3;sceKernelDcacheWritebackAll();
    for(;;)sceDisplayWaitVblankStart();
}
