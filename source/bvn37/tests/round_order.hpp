// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "versus_round.hpp"
#include <string>
namespace bvn::round_order_qa {
struct Checks {
 unsigned error=0,rows=0,cutins=0,manual=0,expiry=0,hp=0,duals=0,banners=0,hooks=0;
 std::uint64_t hash=14695981039346656037ull;
 void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(i*8))&255))*1099511628211ull;}
#define ROUND_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
 bool enter(VersusRound& game){
  for(unsigned frame=0;frame<1800&&game.phase()!=RoundPhase::Playing;++frame)game.step({});
  ROUND_REQUIRE(game.phase()==RoundPhase::Playing&&game.match().gameClocks()->view().actorPhase==0);return true;
 }
 void step(VersusRound& game,std::array<FighterControls,2> input={}){while(!game.step(input)){};}
 bool reference(const std::string& folder,const StageAsset& stage,const FighterAsset& asset){
  FILE* file=std::fopen((folder+"ROUND.IN").c_str(),"r");ROUND_REQUIRE(file);unsigned count=0;ROUND_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==3072);
  std::optional<VersusRound> game;unsigned mainBase=0,actorBase=0;
  for(unsigned row=0;row<count;++row){
   std::array<int,14> values{};for(auto& v:values)ROUND_REQUIRE(std::fscanf(file,"%d",&v)==1);
   if(!values[3]){
    game.reset();RoundRules rules;rules.sourceQuality=true;rules.gameHz=values[0];rules.shineHz=30;
    game.emplace(stage,std::span(&asset,1),std::span(&asset,1),values[1],std::array<unsigned,2>{},BattleMode::Versus,std::array<AssisterChoice,2>{},TeamFactions{},rules);
    if(!enter(*game))return false;mainBase=game->sourceMainFrames();actorBase=game->match().gameClocks()->view().actorFrames;
   }
   if(values[4]==1)game->manualPause();else if(values[4]==2)game->manualResume();
   step(*game);const auto& match=game->match();
   const std::array<int,9> got{game->seconds(),int(game->roundTimeFrame()),game->displayedSeconds(),int(match.sourceControl()->running()),int(match.sourceActionsEnabled()),int(match.actor(0).allowLoseHp()),int(game->sourceMainFrames()-mainBase),int(match.gameClocks()->view().actorFrames-actorBase),match.gameClocks()->view().actorPhase};
   for(unsigned k=0;k<got.size();++k){ROUND_REQUIRE(got[k]==values[5+k]);word(unsigned(got[k]));}++rows;
  }
  std::fclose(file);return true;
 }
 bool timers(const StageAsset& stage,const FighterAsset& asset){
  for(unsigned hz:{30u,60u})for(bool resume:{false,true}){
   RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;rules.shineHz=30;
   VersusRound game(stage,std::span(&asset,1),std::span(&asset,1),100,{},BattleMode::Versus,{}, {},rules);if(!enter(game))return false;
   auto& actor=const_cast<FightMatch&>(game.match()).actor(0);actor.setQi(300);actor.present(FighterCommand::Super);
   bool saw=false,ended=false;unsigned checked=0;
   for(unsigned frame=0;frame<600;++frame){
    const auto before=game.match().gameClocks()->view();const auto prior=game.roundTimeFrame();const auto seconds=game.seconds();
    step(game);const auto after=game.match().gameClocks()->view();const bool cast=!game.match().sourceControl()->animations().empty();
    const unsigned advanced=after.actorFrames-before.actorFrames;
    ROUND_REQUIRE(advanced<=1);const auto expected=(prior+advanced)%31;
    ROUND_REQUIRE(game.roundTimeFrame()==expected&&game.seconds()==seconds-int(prior+advanced>=31));
    if(cast){++checked;if(!saw){saw=true;if(resume){game.manualPause();game.manualResume();ROUND_REQUIRE(game.match().sourceControl()->running());++manual;}}}
    if(saw&&!cast){ended=true;break;}
   }
   ROUND_REQUIRE(saw&&ended&&checked>3);++cutins;word(hz);word(resume);word(checked);
  }
  return true;
 }
 bool expired(const StageAsset& stage,const FighterAsset& asset){
  for(unsigned hz:{30u,60u}){
   RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;rules.shineHz=30;
   VersusRound game(stage,std::span(&asset,1),std::span(&asset,1),1,{},BattleMode::Versus,{}, {},rules);if(!enter(game))return false;
   auto& actor=const_cast<FightMatch&>(game.match()).actor(0);auto& target=const_cast<FightMatch&>(game.match()).actor(1);
   actor.place({350,stage.geometry().playerBottom});target.place({950,stage.geometry().playerBottom});target.setHealth(700);
   for(unsigned frame=0;frame<100&&game.roundTimeFrame()<30;++frame)step(game,{{{.horizontal=1},{}}});
   ROUND_REQUIRE(game.roundTimeFrame()==30);
   while(game.match().gameClocks()->view().actorPhase<game.match().gameClocks()->view().actorGap)step(game,{{{.horizontal=1},{}}});
   const auto x=actor.motion().position.x;const auto clock=game.match().gameClocks()->view();
   step(game,{{{.horizontal=1,.attackPressed=true},{}}});
   ROUND_REQUIRE(game.phase()==RoundPhase::TimeOver&&game.seconds()==0&&game.winner()==0);
   ROUND_REQUIRE(game.displayedSeconds()==1&&actor.motion().position.x>x&&!actor.allowLoseHp()&&!target.allowLoseHp());
   ROUND_REQUIRE(game.match().gameClocks()->view().actorFrames==clock.actorFrames+1);
   FighterStrike hit{};hit.damage=100;hit.key=0x12345;hit.hurtFrames=6;hit.hitX=5;const double health=actor.health();
   ROUND_REQUIRE(actor.receiveStrike({hit,-1,target.motion().position.x}));ROUND_REQUIRE(actor.health()==health&&actor.wasHitBy(hit.key));
   step(game);ROUND_REQUIRE(game.displayedSeconds()==0);++expiry;word(hz);
  }
  return true;
 }
 bool deaths(const StageAsset& stage,const FighterAsset& asset){
  for(unsigned hz:{30u,60u}){
   RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;rules.shineHz=30;
   VersusRound game(stage,std::span(&asset,1),std::span(&asset,1),100,{},BattleMode::Versus,{}, {},rules);if(!enter(game))return false;
   auto& match=const_cast<FightMatch&>(game.match());const auto y=stage.geometry().playerBottom;
   for(unsigned side=0;side<2;++side){match.actor(side).setHealth(1);match.actor(side).present(FighterCommand::Attack);}
   for(unsigned frame=0;frame<120&&game.phase()==RoundPhase::Playing;++frame){match.actor(0).place({350,y});match.actor(1).place({390,y});step(game);}
   ROUND_REQUIRE(game.phase()==RoundPhase::KnockoutFlash&&game.winner()==0&&game.wins(0)==1&&game.wins(1)==0);
   ROUND_REQUIRE(match.actor(0).health()==1&&match.actor(1).health()==0&&!match.actor(0).allowLoseHp());
   ROUND_REQUIRE(match.actor(0).currentHurtDamage()>0);
   ROUND_REQUIRE(!match.sourceControl()->running());++duals;word(hz);
   const auto held=match.gameClocks()->view();bool resumed=false;unsigned ui=0;
   for(unsigned frame=0;frame<600;++frame){step(game);if(game.phase()==RoundPhase::KnockoutFall||game.phase()==RoundPhase::WinnerDelay){
       ROUND_REQUIRE(match.sourceControl()->running());const auto after=match.gameClocks()->view();ROUND_REQUIRE(after.actorFrames>held.actorFrames||after.actorPhase!=held.actorPhase);resumed=true;break;}
     ROUND_REQUIRE(match.gameClocks()->view().actorFrames==held.actorFrames);ui+=game.bannerFrame().has_value();}
   ROUND_REQUIRE(resumed&&ui>20);++banners;word(ui);
   for(unsigned frame=0;frame<1600&&(game.round()!=2||game.phase()!=RoundPhase::Playing);++frame){
     const auto before=match.gameClocks()->view();step(game);const auto after=match.gameClocks()->view();
     ROUND_REQUIRE(after.displayFrames==before.displayFrames+1);
   }
   ROUND_REQUIRE(game.round()==2&&game.phase()==RoundPhase::Playing&&game.wins(0)==1);
   ROUND_REQUIRE(match.actor(0).allowLoseHp()&&match.actor(1).allowLoseHp()&&match.sourceActionsEnabled());word(game.round());
  }
  return true;
 }
 bool health(const std::string& folder,const StageAsset& stage){
  FILE* file=std::fopen((folder+"TYPED.IN").c_str(),"r");ROUND_REQUIRE(file);unsigned banks=0;ROUND_REQUIRE(std::fscanf(file,"%u",&banks)==1&&banks==78);
  for(unsigned i=0;i<banks;++i){char name[64];ROUND_REQUIRE(std::fscanf(file,"%63s",name)==1);FighterAsset asset;
   ROUND_REQUIRE(asset.open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory));FighterMotion actor(asset);actor.reset({350,stage.geometry().playerBottom},1);
   actor.allowLoseHp(false);const double before=actor.health();FighterStrike hit{};hit.damage=100;hit.key=17;hit.hurtFrames=5;
   actor.receiveStrike({hit,-1,400});ROUND_REQUIRE(actor.health()==before);++hp;word(unsigned(actor.health()));
  }
  std::fclose(file);return true;
 }
 bool hookOrder(const StageAsset& stage,const FighterAsset& asset){
  FightMatch match(asset,asset);match.reset(stage,{350,stage.geometry().playerBottom},{700,stage.geometry().playerBottom});match.setSourceProfile(30,30,stage);
  struct State{FightMatch* match;unsigned trace=0;static void stage(void* p){auto& s=*static_cast<State*>(p);s.trace=s.trace*10+1;s.match->manualResume();}
   static void before(void* p,bool){auto& s=*static_cast<State*>(p);s.trace=s.trace*10+2;}
   static void after(void* p,bool){auto& s=*static_cast<State*>(p);s.trace=s.trace*10+3;}}state{&match};
  match.sourceFrameHooks({&state,State::stage,State::before,State::after});match.manualPause();match.step(stage.geometry(),{},false,Contacts::Locked);
  ROUND_REQUIRE(state.trace==123&&match.gameClocks()->view().actorFrames==1);++hooks;word(state.trace);return true;
 }
 bool run(std::string folder){if(!folder.empty()&&folder.back()!='/')folder+='/';StageAsset stage;FighterAsset asset;
  ROUND_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));ROUND_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
  return reference(folder,stage,asset)&&timers(stage,asset)&&expired(stage,asset)&&deaths(stage,asset)&&health(folder,stage)&&hookOrder(stage,asset);
 }
#undef ROUND_REQUIRE
};
} // namespace bvn::round_order_qa
