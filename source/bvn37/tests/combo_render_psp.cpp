// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include "source_combo.hpp"
#include <array>
#include <string>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Combo GPU QA",PSP_MODULE_USER,0,62);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct ComboGpuQA{unsigned status,error,cases,pixels,mismatch,layout,number,phase,linear,x,y,expected,actual;}g_comboGpuQa;}
namespace {
constexpr std::array<const char*,11> Keys{"combo-label","combo-hits_num0","combo-hits_num1","combo-hits_num2","combo-hits_num3","combo-hits_num4","combo-hits_num5","combo-hits_num6","combo-hits_num7","combo-hits_num8","combo-hits_num9"};
alignas(64) std::array<unsigned,512*272> referencePixels,nativePixels;
alignas(64) std::array<std::array<unsigned,64*64>,11> expanded;
unsigned frames=0;
struct Quad{const char* key;float x,y,w,h;};
void capture(std::array<unsigned,512*272>& pixels){
    auto* vram=static_cast<std::byte*>(sceGeEdramGetAddr());auto* drawn=vram+(++frames%2?0:512*272*4);
    sceKernelDcacheWritebackInvalidateRange(pixels.data(),sizeof(pixels));
    sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,drawn,0,0,512,pixels.data());sceGuTexSync();
}
bool run(const std::string& folder){
    bvn::UiAsset art;if(!art.open((folder+"COMBO.BVU").c_str())){g_comboGpuQa.error=1;return false;}
    for(unsigned n=0;n<Keys.size();++n){const auto* sprite=art.find(Keys[n]);if(!sprite||unsigned(sprite->stride)*sprite->rows>64*64){g_comboGpuQa.error=2;return false;}
        const auto* raw=static_cast<const unsigned short*>(art.pixels(*sprite));
        for(unsigned i=0;i<unsigned(sprite->stride)*sprite->rows;++i){const unsigned v=raw[i];expanded[n][i]=((v&15)*17)|(((v>>4)&15)*17<<8)|(((v>>8)&15)*17<<16)|(((v>>12)&15)*17<<24);}}
    sceKernelDcacheWritebackRange(expanded.data(),sizeof(expanded));bvn::StageRenderer renderer;renderer.start();
    for(unsigned layout=0;layout<3;++layout)for(unsigned value:{2u,10u,11u,101u,999u,10000u,4294967295u})for(unsigned linear=0;linear<2;++linear){
        bvn::SourceHitsDisplay ui;const bool musou=layout==2;const unsigned side=layout==1;ui.show(value,layout==0);
        for(unsigned step=0;step<26;++step){
            if(step==11)ui.show(value,layout==0);if(step==20)ui.hide();
            std::array<Quad,12> quads{};unsigned count=0;
            bvn::sourceComboDraw(ui,musou,side,[&](const char* key,double x,double y,double w,double h){quads[count++]={key,float(x),float(y),float(w),float(h)};});
            renderer.quality(linear?3:1);renderer.begin();renderer.beginUi();renderer.box(0,0,800,600,0xff437cb9);
            for(unsigned q=0;q<count;++q){const auto& quad=quads[q];const auto* sprite=art.find(quad.key);unsigned index=0;for(;index<Keys.size();++index)if(std::string(Keys[index])==quad.key)break;
                if(!sprite||index==Keys.size()){g_comboGpuQa.error=3;return false;}
                struct Vertex{float u,v;unsigned color;float x,y,z;};constexpr float viewWidth=float(800*bvn::StageTextureScale),pillar=(480-viewWidth)/2;
                sceGuTexMode(GU_PSM_8888,0,0,GU_FALSE);sceGuTexImage(0,sprite->stride,sprite->rows,sprite->stride,expanded[index].data());
                sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(linear?GU_LINEAR:GU_NEAREST,linear?GU_LINEAR:GU_NEAREST);sceGuTexFlush();
                const float x=quad.x*bvn::StageTextureScale,y=quad.y*bvn::StageTextureScale,w=quad.w*bvn::StageTextureScale,h=quad.h*bvn::StageTextureScale;
                if(x>=viewWidth||x+w<=0||y>=272||y+h<=0)continue;
                auto* vertices=static_cast<Vertex*>(sceGuGetMemory(2*sizeof(Vertex)));
                vertices[0]={0,0,0xffffffff,x+pillar,y,0};vertices[1]={float(sprite->width),float(sprite->height),0xffffffff,(x+w)+pillar,y+h,0};
                sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,nullptr,vertices);
            }
            capture(referencePixels);renderer.end();
            renderer.begin();renderer.beginUi();renderer.box(0,0,800,600,0xff437cb9);if(!linear)sceGuTexFilter(GU_NEAREST,GU_NEAREST);for(unsigned q=0;q<count;++q){const auto& quad=quads[q];renderer.image(art,quad.key,quad.x,quad.y,quad.w,quad.h);}capture(nativePixels);renderer.end();
            const auto* a=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(referencePixels.data())|0x40000000u);
            const auto* b=reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(nativePixels.data())|0x40000000u);
            for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x){++g_comboGpuQa.pixels;if(a[y*512+x]!=b[y*512+x]){g_comboGpuQa.error=4;++g_comboGpuQa.mismatch;g_comboGpuQa.layout=layout;g_comboGpuQa.number=value;g_comboGpuQa.phase=ui.frame();g_comboGpuQa.linear=linear;g_comboGpuQa.x=x;g_comboGpuQa.y=y;g_comboGpuQa.expected=a[y*512+x];g_comboGpuQa.actual=b[y*512+x];return false;}}
            ++g_comboGpuQa.cases;ui.render();
        }
    }
    return true;
}
}
int main(int argc,char** argv){std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_comboGpuQa.status=1;const bool pass=run(folder);g_comboGpuQa.status=pass?2:3;sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();}
