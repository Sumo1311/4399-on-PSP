// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_render_callbacks.hpp"
#include "source_timeout_queue.hpp"
#include "transition_asset.hpp"
#include "versus_round.hpp"
#include <cstdio>
#include <string>
namespace bvn::dispatch_transition_qa {
struct Checks{
 unsigned error=0,rootRows=0,timeoutRows=0,quickRows=0,assets=0,integration=0,pauses=0;
 std::uint64_t hash=14695981039346656037ull;
 void word(unsigned v){for(unsigned i=0;i<4;++i)hash=(hash^((v>>(8*i))&255))*1099511628211ull;}
#define DISPATCH_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
 unsigned queueHash(const SourceTimeoutQueue& q){
  unsigned h=2166136261u;const auto w=[&](unsigned v){for(unsigned i=0;i<4;++i)h=(h^((v>>(8*i))&255))*16777619u;};
  w(q.entries().size());for(const auto& e:q.entries()){w(bool(e));if(e){w(e->id);w(e->frames);w(e->params.size());for(auto p:e->params)w(p);}}return h;
 }
 bool reference(const std::string& folder){
  FILE* file=std::fopen((folder+"DISPATCH.IN").c_str(),"r");DISPATCH_REQUIRE(file);unsigned count=0;DISPATCH_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==14422);
  SourceRenderCallbacks callbacks;SourceTimeoutQueue queue;SourceQuickTransition quick;unsigned adds=0;
  for(unsigned row=0;row<count;++row){
   unsigned type=0;DISPATCH_REQUIRE(std::fscanf(file,"%u",&type)==1);
   if(type==0){
    unsigned scenario,tick,op,expected;std::array<unsigned,3> code;
    DISPATCH_REQUIRE(std::fscanf(file,"%u%u%u%u%u%u%u",&scenario,&tick,&op,&expected,&code[0],&code[1],&code[2])==7);
    if(!tick){callbacks={};for(unsigned id:{0u,1u,2u})callbacks.add(SourceRenderCallbacks::Group::Main,id);if(scenario&1)callbacks.add(SourceRenderCallbacks::Group::Before,3);if(scenario&2)callbacks.add(SourceRenderCallbacks::Group::After,4);}
    const unsigned id=(scenario+tick)%6;
    if(op<3)callbacks.add(SourceRenderCallbacks::Group(op),id);else if(op<6)callbacks.remove(SourceRenderCallbacks::Group(op-3),id);
    unsigned event=0,fired=0;
    if(op!=10)callbacks.render([&](unsigned current){
     event=event*17+current+1;if(fired&(1u<<current))return;fired|=1u<<current;
     const auto mode=(scenario+current)%5,next=(current+1)%6;
     if(mode<2)for(unsigned g=0;g<3;++g)callbacks.remove(SourceRenderCallbacks::Group(g),mode?next:current);
     if(mode==2)callbacks.add(SourceRenderCallbacks::Group::Main,(current+3)%6);
     if(mode==3)callbacks.add(SourceRenderCallbacks::Group::Before,(current+2)%6);
     if(mode==4)callbacks.add(SourceRenderCallbacks::Group::After,next);
    });
    DISPATCH_REQUIRE(event==expected);word(event);
    for(unsigned g=0;g<3;++g){unsigned value=1;for(auto id:callbacks.list(SourceRenderCallbacks::Group(g)))value=value*7+id+1;DISPATCH_REQUIRE(value==code[g]);word(value);}++rootRows;
   }else if(type==1){
    unsigned scenario,hz,tick,op,eventExpected,stateExpected,listened,addExpected,length;
    DISPATCH_REQUIRE(std::fscanf(file,"%u%u%u%u%u%u%u%u%u",&scenario,&hz,&tick,&op,&eventExpected,&stateExpected,&listened,&addExpected,&length)==9);
    if(!tick){queue={};adds=0;}
    if(op<3){queue.frameTimeout(op,(scenario+tick)%5,{int(scenario),-int(tick)});++adds;}
    if(op==3){const int times[]{0,50,1000};queue.timeout(3,times[scenario%3],hz,{int(tick)});++adds;}
    unsigned event=0,fired=0;
    if(queue.listening())queue.render([&](unsigned id,std::span<const int> params){
     int sum=0;for(auto p:params)sum+=p;event=event*17+id+1+sum;
     if(fired&(1u<<id))return;fired|=1u<<id;
     if((scenario&1)&&id<2){queue.frameTimeout(id+2,id,{int(scenario),int(id)});++adds;}
    });
    DISPATCH_REQUIRE(event==eventExpected&&queueHash(queue)==stateExpected&&queue.listening()==bool(listened)&&adds==addExpected&&queue.entries().size()==length);
    word(event);word(stateExpected);word(listened);word(adds);word(length);++timeoutRows;
   }else if(type==2){
    unsigned scenario,tick,ms,stage,completed;int up,down;
    DISPATCH_REQUIRE(std::fscanf(file,"%u%u%u%u%d%d%u",&scenario,&tick,&ms,&stage,&up,&down,&completed)==7);
    if(!tick)quick.begin(0);quick.render(ms);
    DISPATCH_REQUIRE(quick.stage()==stage&&int(std::floor(quick.up()*1e6+.5))==up&&int(std::floor(quick.down()*1e6+.5))==down);
    DISPATCH_REQUIRE(quick.active()==!completed);word(stage);word(unsigned(up));word(unsigned(down));++quickRows;
   }else if(type==3){
    unsigned hz,milliseconds;int frames;DISPATCH_REQUIRE(std::fscanf(file,"%u%u%d",&hz,&milliseconds,&frames)==3);
    SourceTimeoutQueue exact;exact.timeout(0,milliseconds,hz);
    DISPATCH_REQUIRE(exact.entries().size()==1&&exact.entries()[0]->frames==frames&&exact.listening());word(frames);++timeoutRows;
   }else DISPATCH_REQUIRE(false);
  }
  std::fclose(file);return true;
 }
 bool asset(const std::string& folder){
  TransitionAsset art;DISPATCH_REQUIRE(art.open((folder+"TRANS.BVT").c_str()));
  DISPATCH_REQUIRE(art.width()==363&&art.height()==272&&art.stride()==512&&art.rows()==512&&art.residentBytes()<512*1024);
  FILE* file=std::fopen((folder+"TRANS.IN").c_str(),"r");DISPATCH_REQUIRE(file);unsigned count=0;DISPATCH_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==35);
  for(unsigned i=0;i<count;++i){unsigned frame,crc;DISPATCH_REQUIRE(std::fscanf(file,"%u%u",&frame,&crc)==2);DISPATCH_REQUIRE(art.pixels(frame)&&art.decodedCrc()==crc&&art.decodedFrame()==frame);word(frame);word(crc);++assets;}
  DISPATCH_REQUIRE(!art.pixels(999));std::fclose(file);return true;
 }
 bool integrate(const std::string& folder){
  StageAsset stage;FighterAsset asset;DISPATCH_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));DISPATCH_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
  for(unsigned hz:{30u,60u}){
   RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;
   VersusRound game(stage,std::span(&asset,1),std::span(&asset,1),1,{},BattleMode::Versus,{}, {},rules);
   for(unsigned i=0;i<1800&&game.phase()!=RoundPhase::Playing;++i)game.step({});
   DISPATCH_REQUIRE(game.phase()==RoundPhase::Playing&&game.sourceRootFrames()==game.ticks()+1&&!game.sourceTransitionRendering());
   word(game.sourceRootFrames()-game.ticks());++integration;
   // Independent transition advances/removes while UI pause freezes GameCtrl.
   VersusRound paused(stage,std::span(&asset,1),std::span(&asset,1),60,{},BattleMode::Versus,{}, {},rules);
   paused.manualPause();const auto ticks=paused.ticks();const auto clock=paused.match().gameClocks()->view();
   for(unsigned i=0;i<150;++i)paused.pausedDisplayFrame();
   DISPATCH_REQUIRE(paused.ticks()==ticks&&paused.match().gameClocks()->view().displayFrames==clock.displayFrames&&!paused.sourceTransitionRendering()&&paused.sourceStart().step()==0);
   paused.manualResume();for(unsigned i=0;i<1800&&paused.phase()!=RoundPhase::Playing;++i)paused.step({});DISPATCH_REQUIRE(paused.phase()==RoundPhase::Playing);++pauses;
   // TweenLite samples on ENTER_FRAME at stage FPS, including while UI
   // pause stops GameCtrl. The extra PSP presentation must hold that sample.
   VersusRound cadence(stage,std::span(&asset,1),std::span(&asset,1),60,{},BattleMode::Versus,{}, {},rules);
   unsigned displays=0;std::uint64_t now=0;
   const auto display=[&](bool skip){now=std::uint64_t(++displays*1000000./60);cadence.updateUiClock(now);return cadence.step({},skip);};
   for(unsigned i=0;i<200&&cadence.sourceTransitionRendering();++i)display(false);
   do{}while(!display(true));DISPATCH_REQUIRE(cadence.quickTransition().active());
   SourceQuickTransition expected;expected.begin(now*.001);
   for(unsigned i=0;i<20;++i){
    const auto beforeRoot=cadence.sourceRootFrames();const auto beforeUp=cadence.quickTransition().up();
    if(i<8)display(false);
    else{now=std::uint64_t(++displays*1000000./60);cadence.updateUiClock(now);cadence.pausedDisplayFrame();}
    if(cadence.sourceRootFrames()!=beforeRoot)expected.render(now*.001);
    else DISPATCH_REQUIRE(cadence.quickTransition().up()==beforeUp);
    DISPATCH_REQUIRE(cadence.quickTransition().stage()==expected.stage()&&std::abs(cadence.quickTransition().up()-expected.up())<1e-9&&std::abs(cadence.quickTransition().down()-expected.down())<1e-9);
   }
   ++integration;word(displays);word(unsigned(cadence.quickTransition().stage()));
   const auto oldRoot=game.sourceRootFrames();for(unsigned i=0;i<2000&&game.round()==1;++i)game.step({});
   DISPATCH_REQUIRE(game.round()==2&&game.phase()==RoundPhase::Ready&&game.sourceRootFrames()>oldRoot&&game.sourceTransitionVisible());
   for(unsigned i=0;i<500&&game.phase()!=RoundPhase::Playing;++i)game.step({},true);
   DISPATCH_REQUIRE(game.phase()==RoundPhase::Playing);++integration;word(game.sourceRootFrames());
  }
  return true;
 }
 bool run(std::string folder){if(!folder.empty()&&folder.back()!='/')folder+='/';return reference(folder)&&asset(folder)&&integrate(folder);}
#undef DISPATCH_REQUIRE
};
}
