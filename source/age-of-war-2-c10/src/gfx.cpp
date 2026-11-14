#include "gfx.hpp"
#include "boot_trace.hpp"
#include "runtime_memory.hpp"
#include "texture_bank.hpp"
#include "render_clip.hpp"
#include <pspgu.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <set>
namespace aow {
namespace {
// GU writes commands/vertices through an uncached alias. Keep both ends on
// exclusive CPU cache lines: a cached neighbour's writeback must never
// replace the first commands (including the framebuffer address).
constexpr unsigned GeCacheLine=64;
alignas(GeCacheLine) unsigned list[262144];
static_assert(sizeof(list)%GeCacheLine==0,"GE list must occupy whole cache lines");
TextureBank bank;bool started=false,letterbox=true,textured=false,nearest=false;
int boundTexture=-1;
void* drawBuffer=nullptr;
unsigned submitted=0,listPeak=0;
unsigned long long presentedTime=0;unsigned presentedVcount=0;
struct Vertex {float u,v;uint32_t color;float x,y,z;};
void startList(){sceGuStart(GU_SEND,list);sceGuDrawBufferList(GU_PSM_8888,drawBuffer,512);boundTexture=-1;}
void submit(){unsigned bytes=sceGuFinish();listPeak=std::max(listPeak,bytes);sceGuSendList(GU_TAIL,list,nullptr);sceGuSync(GU_SYNC_SEND,GU_SYNC_WAIT);++submitted;}
void flush(){submit();startList();}
void reserve(unsigned bytes){if(unsigned(sceGuCheckList())+bytes+512>sizeof(list))flush();}
void enableTexture(){if(!textured){sceGuEnable(GU_TEXTURE_2D);textured=true;}}
void filter(bool sharp){if(sharp!=nearest){sceGuTexFilter(sharp?GU_NEAREST:GU_LINEAR,sharp?GU_NEAREST:GU_LINEAR);nearest=sharp;}}
}
void Gfx::init(){
 bootTrace("gfx-bank-open");bank.open("data/art/textures.bin");bootTrace("gfx-bank-opened");
 std::set<int> uiPages;
 for(int i=0;i<ART_ASSET_COUNT;i++){std::string key=artAssets[i].key;bool ui=key.rfind("hud/",0)==0||key.rfind("skin/",0)==0;bool world=key=="backgrounds"||key.rfind("bases_fla.",0)==0;if(!ui&&!(largeMemory()&&world))continue;for(unsigned f=0;f<artAssets[i].count;f++){const auto& frame=artFrames[artAssets[i].first+f];for(unsigned j=0;j<frame.count;j++)uiPages.insert(artTiles[frame.first+j].page);}}
 for(int i=0;i<GLYPH_COUNT;i++){const auto& f=artFrames[glyphs[i].frame];for(unsigned j=0;j<f.count;j++)uiPages.insert(artTiles[f.first+j].page);}
 for(int i=0;i<COMPACT_GLYPH_COUNT;i++){const auto& f=artFrames[compactGlyphs[i].frame];for(unsigned j=0;j<f.count;j++)uiPages.insert(artTiles[f.first+j].page);}
 for(int i=0;i<LARGE_GLYPH_COUNT;i++){const auto& f=artFrames[largeGlyphs[i].frame];for(unsigned j=0;j<f.count;j++)uiPages.insert(artTiles[f.first+j].page);}
 bootTrace("gfx-pin-start");for(int page:uiPages)bank.pin(page);bootTrace("gfx-pin-ready");
 // Retire the CRT's cached BSS alias before any uncached GU writes. Runtime
 // submission must not write back a stale cached copy over a finished list.
 sceKernelDcacheWritebackInvalidateRange(list,sizeof(list));
 bootTrace("gfx-gu-start");sceGuInit();guSwapBuffersBehaviour(PSP_DISPLAY_SETBUF_NEXTHSYNC);sceGuStart(GU_DIRECT,list);
 sceGuDrawBuffer(GU_PSM_8888,(void*)0,512);sceGuDispBuffer(480,272,(void*)(512*272*4),512);
 sceGuOffset(2048-240,2048-136);sceGuViewport(2048,2048,480,272);
 sceGuScissor(0,0,480,272);sceGuEnable(GU_SCISSOR_TEST);sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);
 sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
 sceGuTexMode(GU_PSM_8888,0,0,GU_TRUE);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
 sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
 bootTrace("gfx-gu-submit");sceGuFinish();sceGuSync(0,0);bootTrace("gfx-gu-ready");sceDisplayWaitVblankStart();sceGuDisplay(GU_TRUE);
}
void Gfx::shutdown(){sceGuTerm();bank.close();}
void Gfx::begin(){bank.beginFrame();startList();started=true;
 // Build geometry completely before handing the list to GE. Reassert each
 // frame's fixed 2D state instead of depending on the preceding page/pass.
 sceGuEnable(GU_SCISSOR_TEST);sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_DITHER);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);sceGuDisable(GU_LIGHTING);sceGuDisable(GU_TEXTURE_2D);textured=false;
 sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
 sceGuTexMode(GU_PSM_8888,0,0,GU_TRUE);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);nearest=false;sceGuTexWrap(GU_CLAMP,GU_CLAMP);
 sceGuScissor(0,0,480,272);sceGuClearColor(0xff000000);sceGuClear(GU_COLOR_BUFFER_BIT);clip(true);}
void Gfx::end(){if(!started)return;submit();int priority=sceKernelGetThreadCurrentPriority();sceKernelChangeThreadPriority(0,0x10);sceDisplayWaitVblankStart();drawBuffer=sceGuSwapBuffers();presentedTime=sceKernelGetSystemTimeWide();presentedVcount=sceDisplayGetVcount();sceKernelChangeThreadPriority(0,priority);started=false;}
void Gfx::clip(bool enabled){letterbox=enabled;sceGuScissor(enabled?36:0,enabled?74:0,enabled?444:480,enabled?249:272);}
void Gfx::rect(float x,float y,float w,float h,uint32_t color){
 float right=x+w,bottom=y+h,l=0,t=0,r=1,b=1;
 if(!render::rectangle(x,y,right,bottom,l,t,r,b,{letterbox?36.f:0.f,letterbox?74.f:0.f,letterbox?444.f:480.f,letterbox?249.f:272.f}))return;
 if(textured){sceGuDisable(GU_TEXTURE_2D);textured=false;}
 reserve(sizeof(Vertex)*2);auto* v=(Vertex*)sceGuGetMemory(sizeof(Vertex)*2);v[0]={0,0,color,x,y,0};v[1]={0,0,color,right,bottom,0};
 sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,nullptr,v);
}
void Gfx::border(float x,float y,float w,float h,uint32_t c){rect(x,y,w,1,c);rect(x,y+h-1,w,1,c);rect(x,y,1,h,c);rect(x+w-1,y,1,h,c);}
const ArtAsset* Gfx::asset(const std::string& key){static const auto lookup=[] {std::unordered_map<std::string,const ArtAsset*> table;for(int i=0;i<ART_ASSET_COUNT;i++)table.emplace(artAssets[i].key,artAssets+i);return table;}();auto it=lookup.find(key);return it==lookup.end()?nullptr:it->second;}
void Gfx::frame(int f,float x,float y,float sx,float sy,float rotation,uint32_t color,bool sharp){
 if(f<0)return;const auto& frame=artFrames[f];if(sharp){x=std::round(x);y=std::round(y);}filter(sharp);
 float cs=1,sn=0;if(rotation){float r=rotation*.017453292519943295f;cs=std::cos(r);sn=std::sin(r);}
 for(unsigned i=0;i<frame.count;i++){
  const auto& t=artTiles[frame.first+i];float left=t.ox*sx,top=t.oy*sy,right=(t.ox+t.w)*sx,bottom=(t.oy+t.h)*sy;
  if(!rotation&&(x+std::max(left,right)<(letterbox?36:0)||x+std::min(left,right)>(letterbox?444:480)||y+std::max(top,bottom)<(letterbox?74:0)||y+std::min(top,bottom)>(letterbox?249:272)))continue;
  auto* tex=bank.get(t.page,flush);if(!tex)continue;enableTexture();const float uvScale=artTextures[t.page].uvScale;
  if(boundTexture!=t.page){const auto& info=artTextures[t.page];sceGuTexImage(0,info.width,info.height,info.width,tex);boundTexture=t.page;}
  // Axis-aligned images use sprites in 32px strips. Only rotation needs
  // triangles. sceGuTexImage already flushes the GE texture cache.
  if(!rotation){
   float x0=x+std::min(left,right),x1=x+std::max(left,right),y0=y+std::min(top,bottom),y1=y+std::max(top,bottom);
   if(x1<=x0||y1<=y0)continue;float u0=(sx<0?t.x+t.w:t.x)*uvScale,u1=(sx<0?t.x:t.x+t.w)*uvScale,v0=(sy<0?t.y+t.h:t.y)*uvScale,v1=(sy<0?t.y:t.y+t.h)*uvScale;
   if(!render::rectangle(x0,y0,x1,y1,u0,v0,u1,v1,{letterbox?36.f:0.f,letterbox?74.f:0.f,letterbox?444.f:480.f,letterbox?249.f:272.f}))continue;
   int strips=std::max(1,int(std::ceil((x1-x0)/32)));reserve(sizeof(Vertex)*2*strips);auto* v=(Vertex*)sceGuGetMemory(sizeof(Vertex)*2*strips);
   for(int j=0;j<strips;j++){float a=std::min(32.f*j,x1-x0),b=std::min(32.f*(j+1),x1-x0);v[j*2]={u0+(u1-u0)*a/(x1-x0),v0,color,x0+a,y0,0};v[j*2+1]={u0+(u1-u0)*b/(x1-x0),v1,color,x0+b,y1,0};}
   sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2*strips,nullptr,v);
  }else{
   float xx[4]={left,right,right,left},yy[4]={top,top,bottom,bottom};float u[4]={t.x*uvScale,(t.x+t.w)*uvScale,(t.x+t.w)*uvScale,t.x*uvScale},vv[4]={t.y*uvScale,t.y*uvScale,(t.y+t.h)*uvScale,(t.y+t.h)*uvScale};
   render::Point input[4],clipped[12];for(int j=0;j<4;j++)input[j]={x+xx[j]*cs-yy[j]*sn,y+xx[j]*sn+yy[j]*cs,u[j],vv[j]};
   int n=render::polygon(input,4,clipped,{letterbox?36.f:0.f,letterbox?74.f:0.f,letterbox?444.f:480.f,letterbox?249.f:272.f});if(!n)continue;
   reserve(sizeof(Vertex)*n);auto* v=(Vertex*)sceGuGetMemory(sizeof(Vertex)*n);for(int j=0;j<n;j++)v[j]={clipped[j].u,clipped[j].v,color,clipped[j].x,clipped[j].y,0};
   sceGuDrawArray(GU_TRIANGLE_FAN,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,n,nullptr,v);
  }
 }
}
void Gfx::sprite(const std::string& key,int index,float x,float y,float sx,float sy,float r,uint32_t c){
 const auto* a=asset(key);if(!a||!a->count)return;index=std::max(0,std::min(index,int(a->count)-1));
 frame(a->first+index,x,y,sx/a->rasterScale,sy/a->rasterScale,r,c,key.rfind("btn",0)==0||key.rfind("hud/",0)==0||key.rfind("skin/button",0)==0||key.rfind("skin/pad",0)==0||key=="hud_base");
 if(a->count>12&&key!="backgrounds")for(int ahead:{3,9}){const auto& f=artFrames[a->first+std::min(index+ahead,int(a->count)-1)];if(f.count)bank.prefetch(artTiles[f.first].page);}
}
void Gfx::text(const std::string& str,float x,float y,uint32_t color,float scale,float width){
 // Select a separately hinted native font; never stretch or fractionally
 // shrink a tiny glyph atlas. Compact counters and larger menu captions
 // retain their own layout metrics at exactly one texel per device pixel.
 const Glyph* table=glyphs;int count=GLYPH_COUNT;float lineHeight=15;
 if(scale<=.8f){table=compactGlyphs;count=COMPACT_GLYPH_COUNT;lineHeight=12;}
 else if(scale>=1.2f){table=largeGlyphs;count=LARGE_GLYPH_COUNT;lineHeight=20;}
 float start=std::round(x);x=start;y=std::round(y);
 for(size_t i=0;i<str.size();){
  uint32_t c=(unsigned char)str[i++];if(c>=0xc0){int n=c<0xe0?1:c<0xf0?2:3;c&=n==1?31:n==2?15:7;while(n--&&i<str.size())c=(c<<6)|((unsigned char)str[i++]&63);}
  if(c=='\n'){x=start;y+=lineHeight;continue;}
  const Glyph* g=std::lower_bound(table,table+count,c,[](const Glyph& g,uint32_t c){return g.code<c;});
  if(g==table+count||g->code!=c){g=std::lower_bound(glyphs,glyphs+GLYPH_COUNT,c,[](const Glyph& g,uint32_t c){return g.code<c;});if(g==glyphs+GLYPH_COUNT||g->code!=c)continue;}
  if(x-start+g->advance>width){x=start;y+=lineHeight;}frame(g->frame,x,y,1,1,0,color,true);x+=g->advance;
 }
}
unsigned long long Gfx::presentTime(){return presentedTime;}
unsigned Gfx::presentVcount(){return presentedVcount;}
int Gfx::cachedBytes(){return bank.residentBytes();}int Gfx::misses(){return bank.stats().decodes;}
unsigned Gfx::diskReads(){return bank.stats().reads;}unsigned Gfx::diskBytes(){return bank.stats().readBytes;}unsigned Gfx::prefetches(){return bank.stats().prefetches;}
unsigned Gfx::pinnedBytes(){return bank.pinnedBytes();}unsigned Gfx::submissions(){return submitted;}unsigned Gfx::listPeakBytes(){return listPeak;}
}
