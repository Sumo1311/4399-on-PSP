// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include "shadow_pixels.hpp"
#include <cstdio>
#include <algorithm>
#include <memory>
namespace bvn::frame_effects_qa {
struct Checks {
 unsigned error=0,banks=0,cases=0,walks=0,puffs=0,tints=0,lifetimes=0;
 StageGeometry stage{0,4000,2000,1000,{}};
#define FX_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
 bool actor(const FighterAsset& asset,unsigned hz,int dir,bool ghost,bool active,ShadowTint tint,std::span<const unsigned> steps){
  FighterMotion a(asset);a.reset({1700,1000},dir);a.setGameHz(hz);a.setQi(100);
  if(ghost){
   FighterControls input{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostStep)};
   for(unsigned i=0;i<2&&!a.ghostActive();++i)a.step(stage,input,false);
   FX_REQUIRE(a.ghostActive());
   if(!active)tint={0,0,255};
  }else a.step(stage,{.horizontal=dir},false);
  FX_REQUIRE(a.shadowing()==(active||ghost));
  if(active||ghost)FX_REQUIRE(a.shadowTint()==tint);
  a.takeEffects();unsigned prev=a.timelineFrame(),observed=0;
  for(unsigned tick=0;tick<(ghost?26U:120U);++tick){
   const bool animate=tick%2==0;
   if(ghost)a.stepCutin(nullptr,animate,stage);
   else a.step(stage,{.horizontal=dir},animate);
   const auto frame=a.timelineFrame();const auto effects=a.takeEffects();
   const unsigned expected=frame!=prev&&std::ranges::find(steps,frame)!=steps.end();
   if(effects.walkSounds!=(ghost?0:expected)||effects.ghostFootSounds!=(ghost?expected:0))
    std::fprintf(stderr,"callback mismatch bank=%u hz=%u dir=%d ghost=%u tick=%u previous=%u frame=%u expected=%u walk=%u puff=%u\n",banks,hz,dir,unsigned(ghost),tick,prev,frame,expected,effects.walkSounds,effects.ghostFootSounds);
   FX_REQUIRE(effects.walkSounds==(ghost?0:expected));
   FX_REQUIRE(effects.ghostFootSounds==(ghost?expected:0));
   FX_REQUIRE(effects.reactions.size()==(ghost?expected:0));
   if(ghost&&expected){
    const auto& puff=effects.reactions[0];
    FX_REQUIRE(puff.cue==ReactionCue::GhostFoot&&puff.facing==dir&&puff.age==0);
   }
   observed+=expected;prev=frame;
   if(ghost&&animate)a.finishGhostAnimation(nullptr);
  }
  if(ghost){FX_REQUIRE(!a.ghostActive()&&!a.shadowing());puffs+=observed;}
  else{walks+=observed;FX_REQUIRE(a.shadowing()==active);}
  ++cases;return true;
 }
 bool colors(){
  const std::array<std::array<int,3>,8> offsets{{{0,0,0},{0,0,255},{-200,-200,-200},{50,0,-200},{255,50,-255},{-255,0,255},{60,-180,-150},{255,51,153}}};
  for(const auto& tint:offsets){
   std::array<std::byte,16> data{std::byte{0},std::byte{3},std::byte{255},std::byte{0},std::byte{250},std::byte{100},std::byte{9},std::byte{1},std::byte{127},std::byte{128},std::byte{129},std::byte{128},std::byte{4},std::byte{200},std::byte{30},std::byte{255}};
   const auto old=data;tintShadowPixels(data,{tint[0],tint[1],tint[2]});
   for(unsigned i=0;i<data.size();++i){const int want=i%4==3?std::to_integer<int>(old[i]):std::clamp(std::to_integer<int>(old[i])+tint[i%4],0,255);FX_REQUIRE(std::to_integer<int>(data[i])==want);}
   ++tints;
  }return true;
 }
 bool lifetime(const FighterAsset& asset){
  FightEffects fx;const auto& pose=asset.frame(MoveClip::Idle,0).pose;
  auto first=std::make_shared<int>(1);std::weak_ptr<int> old=first;
  for(unsigned i=0;i<9;++i){const std::array sources{ShadowSource{first.get(),&asset,&pose,{10.+i,100},1,true,{255,51,153},first}};fx.step(true,sources);}
  FX_REQUIRE(!fx.scene().shadows.empty());const auto before=fx.scene().shadows.front().alpha;
  fx.step(false);FX_REQUIRE(fx.scene().shadows.front().alpha==before);
  first.reset();FX_REQUIRE(!old.expired());
  auto second=std::make_shared<int>(2);
  for(unsigned i=0;i<5;++i){const std::array sources{ShadowSource{second.get(),&asset,&pose,{20.+i,100},-1,true,{-200,-200,-200},second}};fx.step(true,sources);}
  bool pink=false,dark=false;
  for(const auto& stamp:fx.scene().shadows){pink|=stamp.tint==ShadowTint{255,51,153};dark|=stamp.tint==ShadowTint{-200,-200,-200};}
  FX_REQUIRE(pink&&dark);second.reset();
  for(unsigned i=0;i<24;++i)fx.step(true);
  FX_REQUIRE(fx.scene().shadows.empty()&&old.expired());++lifetimes;return true;
 }
 bool run(const std::string& folder,const std::string& fixture){
  FILE* file=std::fopen(fixture.c_str(),"r");FX_REQUIRE(file);char name[32];unsigned count,active;int r,g,b;
  while(std::fscanf(file,"%31s %u %d %d %d %u",name,&active,&r,&g,&b,&count)==6){
   std::array<unsigned,32> steps{};FX_REQUIRE(count<=steps.size());
   for(unsigned i=0;i<count;++i)FX_REQUIRE(std::fscanf(file,"%u",&steps[i])==1);
   FighterAsset asset;FX_REQUIRE(asset.open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory));
   for(unsigned hz:{30U,60U})for(int dir:{-1,1})for(bool ghost:{false,true})
    if(!actor(asset,hz,dir,ghost,active,{r,g,b},{steps.data(),count})){std::fclose(file);return false;}
   if(!banks&&!lifetime(asset)){std::fclose(file);return false;}++banks;
  }
  const bool complete=std::feof(file);std::fclose(file);FX_REQUIRE(complete&&banks==78);return colors();
 }
#undef FX_REQUIRE
};
}
