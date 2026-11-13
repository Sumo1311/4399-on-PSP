// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "versus_round.hpp"
#include <cstdio>
#include <string>
namespace bvn::start_end_qa {
struct Checks {
 unsigned error=0,rows=0,timelines=0,starts=0,skips=0,ends=0,pauses=0;
 std::uint64_t hash=14695981039346656037ull;
 void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(8*i))&255))*1099511628211ull;}
#define START_END_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
 struct View{
  unsigned events=0;void event(unsigned n){events=events*23+n;}
  void focus(int side){event(side<0?3:side+1);}void intro(unsigned side){event(4+side);}
  void transitionOut(bool notify){event(notify?7:8);}void quickTransition(){event(9);}
  void startBanner(){event(6);}void fadeIn(){event(10);}void idle(){event(11);}
  void endBanner(){event(12);}void win(){event(13);}void transitionIn(){event(14);}
  void clearBanner(){event(15);}void fadeOut(){event(16);}
 };
 bool reference(const std::string& folder){
  FILE* file=std::fopen((folder+"STARTEND.IN").c_str(),"r");START_END_REQUIRE(file);
  unsigned count=0;START_END_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==67200);
  SourceStartController start;SourceEndController end;View view;bool done=false;
  for(unsigned row=0;row<count;++row){
   std::array<int,16> v{};for(auto& n:v)START_END_REQUIRE(std::fscanf(file,"%d",&n)==1);
   const auto kind=v[0];
   if(!v[4]){
    done=false;start={};end={};view={};
    if(kind==0)start.versus(v[1],v[2],view);else if(kind==1)start.nextRound(v[1],view);else if(kind==2)start.musou(v[1],view);else end.begin(v[1],kind==4);
   }
   view.events=0;
   if(v[6]){if(kind<3)start.transitionComplete();else end.transitionComplete();}
   if(v[7]){if(kind<3)start.uiComplete();else end.uiComplete();}
   bool returned=false;
   if(!done){
    if(v[5]){if(kind<3)start.skip(view);else end.skip();}
    returned=kind<3?start.render(v[8],view):end.render(v[9],view);done|=returned;
   }
   const std::array<int,6> got{int(returned),kind<3?start.step():end.step(),kind<3?start.hold():end.hold(),kind<3?int(start.uiPlaying()):0,kind<3?0:int(end.active()),int(view.events)};
   for(unsigned i=0;i<got.size();++i){if(got[i]!=v[10+i])std::printf("row=%u kind=%d field=%u want=%d got=%d\n",row,kind,i,v[10+i],got[i]);START_END_REQUIRE(got[i]==v[10+i]);word(unsigned(got[i]));}
   ++rows;
  }
  std::fclose(file);return true;
 }
 bool uiReference(const std::string& folder){
  FILE* file=std::fopen((folder+"ROUNDUI.IN").c_str(),"r");START_END_REQUIRE(file);
  unsigned count=0;START_END_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==1440);
  SourceBannerTimeline banner;SourceTransitionTimeline trans;unsigned callbacks=0;
  for(unsigned row=0;row<count;++row){
   std::array<int,8> v{};for(auto& n:v)START_END_REQUIRE(std::fscanf(file,"%d",&n)==1);
   if(!v[3]){banner={};trans={};callbacks=0;
    if(!v[0])switch(v[1]){case 0:banner.start(1);break;case 1:banner.start(5);break;case 2:banner.knockout();break;case 3:banner.timeOver();break;case 4:banner.draw();break;case 5:banner.winner();break;}
    else if(!v[1])trans.in();else trans.out(v[1]==1);
   }
   if(!v[0]){
    const bool complete=banner.renderAnimate();START_END_REQUIRE(banner.frame()&&int(*banner.frame())==v[4]&&int(complete)==v[5]);word(*banner.frame());word(complete);
   }else{
    callbacks+=trans.render(v[2])!=SourceTransitionTimeline::Callback::None;
    START_END_REQUIRE(int(trans.frame())==v[4]&&int(callbacks)==v[5]&&int(trans.rendering())==v[6]&&int(trans.visible())==v[7]);
    word(trans.frame());word(callbacks);word(trans.rendering());word(trans.visible());
   }
   ++timelines;
  }
  std::fclose(file);return true;
 }
 void step(VersusRound& game,bool skip=false){while(!game.step({},skip)){};}
 bool integration(const std::string& folder){
  StageAsset stage;FighterAsset asset;START_END_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
  START_END_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
  for(unsigned hz:{30u,60u})for(bool skip:{false,true}){
   RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;rules.shineHz=30;
   VersusRound game(stage,std::span(&asset,1),std::span(&asset,1),1,{},BattleMode::Versus,{}, {},rules);
   unsigned ready=0;bool firstIntro=false,secondIntro=false;
   for(unsigned i=0;i<1800&&game.phase()!=RoundPhase::Playing;++i){
    step(game,skip);ready+=game.phase()==RoundPhase::Ready;
    firstIntro|=game.match().actor(0).performing(FighterCommand::Intro);
    secondIntro|=game.match().actor(1).performing(FighterCommand::Intro);
    START_END_REQUIRE(game.seconds()==1);
   }
   START_END_REQUIRE(game.phase()==RoundPhase::Playing&&game.match().sourceActionsEnabled());
   if(skip){START_END_REQUIRE(!ready&&!firstIntro&&!secondIntro);++skips;}
   else{START_END_REQUIRE(ready>50&&firstIntro&&secondIntro);++starts;}
   // Actual draw expiry -> TIME OVER -> DRAW GAME -> next-round banner.
   for(unsigned i=0;i<1800&&game.round()==1;++i)step(game);
   START_END_REQUIRE(game.round()==2&&game.phase()==RoundPhase::Ready&&game.sourceStart().kind()==SourceStartController::Kind::NextRound);
   bool sawBanner=false;
   for(unsigned i=0;i<500&&game.phase()!=RoundPhase::Playing;++i){step(game,true);sawBanner|=game.bannerFrame().has_value();}
   START_END_REQUIRE(sawBanner&&game.phase()==RoundPhase::Playing);++ends;
   word(hz);word(skip);word(ready);word(game.ticks());
  }
  for(unsigned hz:{30u,60u}){
   RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;
   VersusRound game(stage,std::span(&asset,1),std::span(&asset,1),60,{},BattleMode::Versus,{}, {},rules);
   game.manualPause(true);START_END_REQUIRE(game.match().sourceControl()->running());
   for(unsigned i=0;i<500&&game.phase()!=RoundPhase::Playing;++i)step(game,true);
   START_END_REQUIRE(game.phase()==RoundPhase::Playing&&!game.match().sourceControl()->running()&&game.takeDeferredPause());
   START_END_REQUIRE(!game.takeDeferredPause());game.manualResume();START_END_REQUIRE(game.match().sourceControl()->running());++pauses;
   VersusRound cancelled(stage,std::span(&asset,1),std::span(&asset,1),60,{},BattleMode::Versus,{}, {},rules);
   cancelled.manualPause(true);cancelled.manualResume();for(unsigned i=0;i<500&&cancelled.phase()!=RoundPhase::Playing;++i)step(cancelled,true);
   START_END_REQUIRE(cancelled.phase()==RoundPhase::Playing&&cancelled.match().sourceControl()->running()&&!cancelled.takeDeferredPause());++pauses;word(hz);
  }
  return true;
 }
 bool run(std::string folder){if(!folder.empty()&&folder.back()!='/')folder+='/';return reference(folder)&&uiReference(folder)&&integration(folder);}
#undef START_END_REQUIRE
};
}
