// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scene_integration.hpp"
#include <cmath>
namespace bvn::lifecycle_qa {
struct Checks {
 unsigned error=0,rows=0,alphaRows=0,races=0,fades=0,actors=0,failedRow=0,failedColumn=0,actual=0,expected=0;
 std::uint64_t hash=14695981039346656037ull;
 void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(i*8))&255))*1099511628211ull;}
#define LIFE_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
 static unsigned queueHash(const SourceTimeoutQueue& q){
  unsigned h=2166136261u;const auto add=[&](unsigned v){for(unsigned i=0;i<4;++i)h=(h^((v>>(8*i))&255))*16777619u;};
  add(q.entries().size());for(const auto& e:q.entries()){add(bool(e));if(e){add(e->id);add(e->frames);}}return h;
 }
 struct View {
  bool flat=true;unsigned victories=0,passes=0,returns=0,returnHash=2166136261u;
  void koComplete(){flat=true;}void victory(unsigned actor){victories|=1u<<actor;}
  void finish(bool won){++returns;passes+=won;for(unsigned i=0;i<4;++i)returnHash=(returnHash^((unsigned(won)>>(i*8))&255))*16777619u;}
 };
 bool reference(const std::string& folder){
  FILE* f=std::fopen((folder+"LIFECYCLE.IN").c_str(),"r");LIFE_REQUIRE(f);unsigned count;LIFE_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==23040);
  SourceMusouPresentation ui;SourceMusouResult result;View view;
  for(unsigned row=0;row<count;++row){
   std::array<unsigned,30> v{};for(auto& n:v)LIFE_REQUIRE(std::fscanf(f,"%u",&n)==1);
   if(!v[2]){ui.initialize(v[0]);result={};view={};}
   ui.beginRoot();for(unsigned encoded=v[3];encoded;encoded>>=3){switch(encoded&7){
    case 1:if(result.complete())ui.end(true,v[6]);break;
    case 2:result.defeat();ui.end(false,v[6]);break;
    case 3:result.timeOver();ui.end(false,v[6]);break;
    case 4:view.flat=false;ui.knockout();break;
    case 5:ui.entrance();break;
   }}
   if(!v[4]){if(v[5])ui.renderAnimate(view);if(view.flat)ui.afterMain();}ui.endRoot(view);
   const std::array<unsigned,23> got{ui.frame(),unsigned(ui.kind()),ui.starting(),ui.entranceDone(),ui.ending(),ui.returnRequested(),ui.passRequested(),ui.returnSerial(),ui.endListeners(),ui.koListeners(),ui.completions(),view.victories,ui.takeSignals(),queueHash(ui.timeouts()),ui.timeoutRoots(),ui.stripFrame(),unsigned(result.result()),result.revision(),result.completeAccepted(),result.finished(),view.passes,view.returnHash,view.flat};
   for(unsigned i=0;i<got.size();++i){if(got[i]!=v[7+i]){failedRow=row;failedColumn=7+i;actual=got[i];expected=v[7+i];LIFE_REQUIRE(false);}word(got[i]);}++rows;
  }std::fclose(f);return true;
 }
 bool alphaReference(const std::string& folder,const FighterAsset& art){
  FILE* f=std::fopen((folder+"ALPHA.IN").c_str(),"r");LIFE_REQUIRE(f);unsigned count;LIFE_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==8192);
  FighterMotion actor(art);actor.sourceHz(30);SourceEnemyFade fade;unsigned removed=0;
  for(unsigned row=0;row<count;++row){unsigned scenario,tick,write,animate,active,callbacks;double initial,value,alpha;
   LIFE_REQUIRE(std::fscanf(f,"%u%u%lf%u%lf%u%lf%u%u",&scenario,&tick,&initial,&write,&value,&animate,&alpha,&active,&callbacks)==9);
   if(!tick){actor.reset({400,500},1);actor.displayAlpha(initial);fade={};fade.start(initial);removed=0;}
   if(write){actor.displayAlpha(value);}
   if(animate&&fade.render(actor)){++removed;}
   LIFE_REQUIRE(std::abs(actor.displayAlpha()-alpha)<1e-12&&fade.active()==bool(active)&&removed==callbacks&&actor.opacity()==unsigned(alpha*255+.5));
   word(unsigned(std::llround(actor.displayAlpha()*1000000)));word(active);word(callbacks);++alphaRows;
  }std::fclose(f);return true;
 }
 static FighterMotion& actor(const FighterMotion& value){return const_cast<FighterMotion&>(value);}
 static bool dead(FighterMotion& victim,const StageAsset& stage){victim.die();for(unsigned i=0;i<1200&&!victim.deathComplete();++i)victim.step(stage.geometry(),{},true);return victim.deathComplete();}
 bool scenes(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
  MusouCampaign boss,minions;LIFE_REQUIRE(boss.open((folder+"MUSLETH.BVM").c_str()));LIFE_REQUIRE(minions.open((folder+"MUSMIN.BVM").c_str()));
  for(unsigned hz:{30u,60u}){
   for(unsigned order=0;order<2;++order){
    MusouProgress progress(boss);std::array<MusouMember,3> members{};MusouBattle game;scene_order_qa::Integration fixture;
    LIFE_REQUIRE(fixture.enter(game,boss,stage,art,hz,1,progress,members));
    auto& player=actor(game.member(game.leader()));auto& enemy=const_cast<MusouOpponent&>(game.enemies().front());
    player.reset({350,stage.geometry().playerBottom},1);enemy.motion.reset({1400,stage.geometry().playerBottom},-1);enemy.brain={};enemy.bossAI.reset();
    LIFE_REQUIRE(dead(order?player:enemy.motion,stage));
    LIFE_REQUIRE(game.wave().result()==(order?MusouResult::Defeated:MusouResult::Won));
    LIFE_REQUIRE(dead(order?enemy.motion:player,stage));
    LIFE_REQUIRE(game.wave().result()==(order?MusouResult::Won:MusouResult::Defeated));
    LIFE_REQUIRE(game.presentation().kind()==(order?SourceMusouPresentation::Kind::Win:SourceMusouPresentation::Kind::Lose)&&game.presentation().endListeners()==2);
    bool finished=false,acknowledged=false,firstClosed=false;
    for(unsigned i=0;i<2000;++i){
     game.step({},false);
     if(game.presentation().returnSerial()==1&&!acknowledged){game.acknowledgeReturn();acknowledged=true;}
     firstClosed|=game.leaveReady();
     if(game.presentation().returnSerial()>=2){
      LIFE_REQUIRE(acknowledged&&firstClosed);
      game.acknowledgeReturn();
      LIFE_REQUIRE(!game.leaveReady()&&game.closingTransition().rendering());
      finished=true;break;
     }
    }
    LIFE_REQUIRE(finished&&game.presentation().passRequested()&&game.presentation().endListeners()==0&&game.kills()==1&&game.wave().sourceResult()->revision()==2&&game.sourceScene().stats().rangeFaults==0);
    // Deliberately retain this controlled scene through both global-style
    // timeouts; a normal parent may already have left on the first return.
    word(hz);word(order);word(game.presentation().returnSerial());word(unsigned(game.wave().result()));++races;
   }
   MusouProgress progress(minions);std::array<MusouMember,3> members{};MusouBattle game;scene_order_qa::Integration fixture;
   LIFE_REQUIRE(fixture.enter(game,minions,stage,art,hz,6,progress,members));
   auto& enemy=const_cast<MusouOpponent&>(game.enemies().front());enemy.motion.reset({1400,stage.geometry().playerBottom},-1);enemy.brain={};enemy.bossAI.reset();const unsigned id=enemy.id;
   LIFE_REQUIRE(dead(enemy.motion,stage)&&enemy.sourceFade.active());enemy.motion.displayAlpha(.2);LIFE_REQUIRE(enemy.displayOpacity()==51);game.manualPause();
   for(unsigned i=0;i<50&&enemy.motion.displayAlpha()==.2;++i)game.step({},false);
   LIFE_REQUIRE(std::abs(enemy.motion.displayAlpha()-.15)<1e-12&&enemy.displayOpacity()==38);
   enemy.motion.displayAlpha(0);
   for(unsigned i=0;i<50&&game.sourceScene().registered(id);++i)game.step({},false);
   LIFE_REQUIRE(!game.sourceScene().registered(id)&&game.enemies().size()==5&&game.kills()==1&&game.sourceScene().stats().rangeFaults==0);++fades;word(hz);
  }return true;
 }
 bool alphaActors(const StageAsset& stage,const FighterAsset& art,const FighterAsset& obito){
  for(unsigned hz:{30u,60u}){
   FighterMotion ordinary(art);ordinary.sourceHz(hz);ordinary.reset({350,stage.geometry().playerBottom},1);ordinary.displayAlpha(.23);
   for(unsigned i=0;i<40;++i)ordinary.step(stage.geometry(),{},true);
   LIFE_REQUIRE(ordinary.displayAlpha()==.23&&ordinary.opacity()==59);++actors;
   FighterMotion actor(obito);actor.sourceHz(hz);actor.reset({400,stage.geometry().playerBottom},1);actor.present(FighterCommand::SpecialUp);
   bool visible=false;for(unsigned i=0;i<500;++i){actor.step(stage.geometry(),{},true);if(actor.displayAlpha()<.8){visible=true;break;}}
   LIFE_REQUIRE(visible);actor.displayAlpha(.61);actor.present(FighterCommand::Attack);actor.step(stage.geometry(),{},true);
   LIFE_REQUIRE(std::abs(actor.displayAlpha()-.66)<1e-12);
   for(unsigned i=0;i<30;++i)actor.step(stage.geometry(),{},true);
   LIFE_REQUIRE(actor.displayAlpha()==1&&actor.opacity()==255);++actors;word(hz);
  }return true;
 }
 bool run(const std::string& folder){
  StageAsset stage;FighterAsset art,obito;LIFE_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));LIFE_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str()));LIFE_REQUIRE(obito.open((folder+"OBITO.BVF").c_str()));
  return reference(folder)&&alphaReference(folder,art)&&scenes(folder,stage,art)&&alphaActors(stage,art,obito);
 }
#undef LIFE_REQUIRE
};
}
