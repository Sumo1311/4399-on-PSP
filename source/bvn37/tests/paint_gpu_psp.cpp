// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_stage_renderer.hpp"
#include <array>
#include <cstdio>
#include <cmath>
#include <string>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Paint GPU QA",PSP_MODULE_USER,0,66);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(16384);
extern "C" {
struct PaintGpuQA {unsigned status,error,cases,pixels,mismatch,caseRow,quality,zoom,x,y,expected,actual,borderPixels,borderOutside,kinds;}g_paintGpuQa;
extern volatile struct RendererQA {unsigned frames,additive,darken,color,alpha,combatDraws,hardlight,lighten,screen,textureMisses,combatTiles;}g_rendererQa;
extern volatile struct PaintRendererQA {unsigned frames,players,roots,missing,hashLow,hashHigh,sprites,combat,ground,reactions,shadows,blackBack,backgrounds;}g_paintRendererQa;
}
namespace {
using namespace bvn;using P=SourcePaint;
alignas(64) std::array<unsigned,512*272> expectedPixels,actualPixels;
unsigned frames=0;
void capture(std::array<unsigned,512*272>& pixels){
 auto* vram=static_cast<std::byte*>(sceGeEdramGetAddr());auto* drawn=vram+(++frames%2?0:512*272*4);
 sceKernelDcacheWritebackInvalidateRange(pixels.data(),sizeof(pixels));sceGuCopyImage(GU_PSM_8888,0,0,480,272,512,drawn,0,0,512,pixels.data());sceGuTexSync();
}
const volatile unsigned* uncached(const std::array<unsigned,512*272>& v){return reinterpret_cast<volatile const unsigned*>(reinterpret_cast<std::uintptr_t>(v.data())|0x40000000u);}
void cueReference(const UiAsset& art,const GroundCueView& cue,CameraView camera,Point shift,bool smooth){
 char key[24];std::snprintf(key,sizeof(key),"ground-%u-%u",unsigned(cue.cue),cue.frame);const auto* sprite=art.find(key);if(!sprite)return;
 const bool rush=cue.cue>=GroundCue::Rush;const int ox=rush?112:64,oy=rush?64:(cue.cue>=GroundCue::AssistBleach?128:24);
 float x=float((cue.origin.x-camera.origin.x-ox)*StageTextureScale*camera.zoom),y=float((cue.origin.y-camera.origin.y-oy)*StageTextureScale*camera.zoom);
 const float width=float(sprite->width*camera.zoom),height=float(sprite->height*camera.zoom);x+=shift.x*StageTextureScale;y+=shift.y*StageTextureScale;
 constexpr float view=float(800*StageTextureScale),pillar=(480-view)/2;if(x>=view||x+width<=0||y>=272||y+height<=0)return;
 sceGuTexMode(GU_PSM_4444,0,0,GU_FALSE);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);sceGuTexFilter(smooth?GU_LINEAR:GU_NEAREST,smooth?GU_LINEAR:GU_NEAREST);
 sceGuTexImage(0,sprite->stride,sprite->rows,sprite->stride,art.pixels(*sprite));
 struct Vertex{float u,v;unsigned color;float x,y,z;};auto* vertices=static_cast<Vertex*>(sceGuGetMemory(2*sizeof(Vertex)));
 vertices[0]={cue.facing<0?float(sprite->width):0,0,0xffffffff,x+pillar,y,0};vertices[1]={cue.facing<0?0:float(sprite->width),float(sprite->height),0xffffffff,x+width+pillar,y+height,0};
 sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,nullptr,vertices);
}
bool run(const std::string& folder){
 StageRenderer renderer;renderer.start();renderer.begin();renderer.beginUi();renderer.box(-100,-100,1000,800,0xff217bc1);capture(actualPixels);renderer.end();
 constexpr double view=800*StageTextureScale,pillar=(480-view)/2;const unsigned left=unsigned(std::ceil(pillar-.5)),right=unsigned(std::ceil(pillar+view-.5));
 const auto* border=uncached(actualPixels);
 for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x){++g_paintGpuQa.borderPixels;if(x<left||x>=right){++g_paintGpuQa.borderOutside;if((border[y*512+x]&0x00ffffff)!=0){g_paintGpuQa.error=1;g_paintGpuQa.x=x;g_paintGpuQa.y=y;g_paintGpuQa.expected=0;g_paintGpuQa.actual=border[y*512+x]&0x00ffffff;++g_paintGpuQa.mismatch;return false;}}}
 FILE* input=std::fopen((folder+"PAINTGPU.IN").c_str(),"r");if(!input){g_paintGpuQa.error=2;return false;}unsigned count;if(std::fscanf(input,"%u",&count)!=1||count!=32){g_paintGpuQa.error=3;return false;}
 StageAsset empty;FighterAsset ichigo,naruto,reaction,combat;UiAsset common;
 if(!ichigo.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)||!naruto.open((folder+"NARUTO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)||!reaction.open((folder+"REACTION.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)||!combat.open((folder+"COMBAT.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)||!common.open((folder+"UI.BVU").c_str())){g_paintGpuQa.error=4;return false;}
 std::vector<FighterView> fighters;fighters.reserve(16);
 for(unsigned i=0;i<16;++i){const auto& art=i%2?naruto:ichigo;const auto clip=art.command(FighterCommand::Attack);fighters.push_back({art,art.frame(clip,(i*3)%art.clip(clip).count).pose,{390.+(i%3)*10,435.+(i%2)*4},i%2?-1:1,nullptr,128+(i%4)*32,{},P::key(P::Kind::Sprite,i)});}
 std::array<GroundCueView,4> ground{};std::array<ReactionCueView,4> reactions{};std::array<ShadowStamp,4> shadows{};
 for(unsigned i=0;i<4;++i){ground[i]={GroundCue(i),{392.+i*5,438.},i+1,i%2?-1:1};reactions[i]={ReactionCue(i),{400.,420.},i+1,i%2?-1:1};const auto clip=ichigo.command(FighterCommand::Attack);shadows[i]={&ichigo,&ichigo.frame(clip,i%ichigo.clip(clip).count).pose,{397.+i*3,435.},i%2?-1:1,.4+i*.1,i,{0,0,255}};}
 std::array<CombatEffect,8> styles{};std::array<CombatEffectView,8> effects{},backgrounds{};
 const auto* data=combatEffect("hit_end");if(!data){g_paintGpuQa.error=5;return false;}const auto art=std::ranges::find(CombatArtData,data->className,&CombatArt::className);if(art==std::end(CombatArtData)){g_paintGpuQa.error=6;return false;}
 for(unsigned i=0;i<8;++i){styles[i]=*data;styles[i].blend=EffectBlend(i%5);effects[i].effect=&styles[i];effects[i].art=art;effects[i].origin={400.,415.};effects[i].facing=i%2?-1:1;effects[i].rotation=0;effects[i].displayFrame=i%std::min(4u,art->frames);effects[i].paintId=i;backgrounds[i]=effects[i];backgrounds[i].background=true;}
 for(unsigned row=0;row<count;++row){
  unsigned scenario,tick;if(std::fscanf(input,"%u%u",&scenario,&tick)!=2){g_paintGpuQa.error=7;return false;}
  std::array<std::vector<P::Key>,2> order;
  for(auto& list:order){unsigned length;if(std::fscanf(input,"%u",&length)!=1||length>128){g_paintGpuQa.error=8;return false;}for(unsigned i=0;i<length;++i){unsigned long long key;if(std::fscanf(input,"%llu",&key)!=1){g_paintGpuQa.error=9;return false;}list.push_back(key);g_paintGpuQa.kinds|=1u<<unsigned(P::kind(key));}}
  P paint;for(unsigned i=0;i<3;++i)paint.remove(P::key(P::Kind::Base,i));for(auto key:order[0])paint.add(P::Layer::Players,key);for(auto key:order[1])paint.add(P::Layer::Stage,key);
  ground[3].cue=GroundCue(3+row%4);reactions[3].cue=row%2?ReactionCue::ExplodeBurst:ReactionCue::GhostFoot;
  for(unsigned quality:{0u,3u})for(unsigned zoom=0;zoom<3;++zoom)for(unsigned offset=0;offset<2;++offset){
   g_paintGpuQa.caseRow=scenario*128+tick;g_paintGpuQa.quality=quality;g_paintGpuQa.zoom=zoom;
   const double scale=zoom==0?1:zoom==1?2.5:3.5;const CameraView camera{{400-400/scale,430-300/scale},scale};const Point shift{offset?5.:-3.,offset?-2.:4.};
   SceneEffects scene;scene.hideMap=true;scene.shiftX=shift.x;scene.shiftY=shift.y;scene.backdropColor=0x1c2834;scene.ground=ground;scene.reactions=reactions;scene.shadows=shadows;scene.combat=effects;scene.backgrounds=backgrounds;scene.paint=&paint;
   renderer.quality(quality);renderer.begin();renderer.arena(empty,camera,fighters,scene,&common,&reaction,&combat);capture(actualPixels);renderer.end();
   renderer.begin();renderer.rootOffset(shift);
   const auto single=[&](P::Key key,bool background){
    const auto id=P::id(key);SceneEffects item;item.hideMap=true;item.shiftX=shift.x;item.shiftY=shift.y;
    switch(P::kind(key)){
    case P::Kind::Sprite:renderer.arena(empty,camera,std::span(fighters).subspan(id,1),item);break;
    case P::Kind::Ground:cueReference(common,ground[id],camera,shift,quality==3);break;
    case P::Kind::Reaction:item.reactions=std::span(reactions).subspan(id,1);renderer.arena(empty,camera,{},item,nullptr,&reaction);break;
    case P::Kind::Shadow:item.shadows=std::span(shadows).subspan(id,1);renderer.arena(empty,camera,{},item);break;
    case P::Kind::Combat:if(background)item.backgrounds=std::span(backgrounds).subspan(id,1);else item.combat=std::span(effects).subspan(id,1);renderer.arena(empty,camera,{},item,nullptr,nullptr,&combat);break;
    case P::Kind::BlackBack:renderer.box(0,0,800,600,0xff000000|scene.backdropColor);break;
    default:break;
    }
   };
   for(auto key:order[1]){if(P::kind(key)==P::Kind::Base){if(P::id(key)==1)for(auto child:order[0])single(child,false);}else single(key,true);}
   capture(expectedPixels);renderer.end();const auto* a=uncached(expectedPixels);const auto* b=uncached(actualPixels);
   for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x){++g_paintGpuQa.pixels;if(a[y*512+x]!=b[y*512+x]){g_paintGpuQa.error=10;g_paintGpuQa.x=x;g_paintGpuQa.y=y;g_paintGpuQa.expected=a[y*512+x];g_paintGpuQa.actual=b[y*512+x];++g_paintGpuQa.mismatch;return false;}}
   if(g_rendererQa.textureMisses||g_paintRendererQa.missing){g_paintGpuQa.error=11;return false;}++g_paintGpuQa.cases;
  }
 }
 std::fclose(input);return true;
}
}
int main(int argc,char** argv){std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);scePowerSetClockFrequency(333,333,166);g_paintGpuQa.status=1;const bool pass=run(folder);g_paintGpuQa.status=pass?2:3;sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();}
