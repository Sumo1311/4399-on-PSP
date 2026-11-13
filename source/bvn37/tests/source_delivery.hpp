// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "stage_motion.hpp"
#include "fight_projectile.hpp"
#include "versus_round.hpp"
#include "musou_battle.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

namespace bvn::source_delivery_qa {
struct Checks {
 unsigned error=0,motions=0,bullets=0,calls=0,inputs=0,rounds=0,freezes=0,musou=0,badField=0;
 double expected=0,actual=0;
 std::uint64_t hash=14695981039346656037ull;
 void word(unsigned v){for(unsigned i=0;i<4;++i)hash=(hash^((v>>(8*i))&255))*1099511628211ull;}
 void number(double v){word(unsigned(static_cast<long long>(std::floor(v*1e6+.5))));}
 bool near(double value,double wanted,unsigned field){
  if(!std::isfinite(value)||!std::isfinite(wanted)||std::abs(value-wanted)>1e-8){
   actual=value;expected=wanted;badField=field;
   std::fprintf(stderr,"mismatch motion=%u bullet=%u field=%u actual=%.17g expected=%.17g\n",motions,bullets,field,value,wanted);return false;
  }
  number(value);return true;
 }
#define SOURCE_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
 bool motion(const std::string& folder){
  FILE* file=std::fopen((folder+"MOTION.IN").c_str(),"r");SOURCE_REQUIRE(file);
  unsigned count=0;SOURCE_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==6912);
  const std::array<Floor,2> floors{{{120,280,300},{260,380,300}}};
  const StageGeometry stage{0,1200,640,600,floors};Motion m;
  for(unsigned i=0;i<count;++i){
   std::array<double,32> v{};for(auto& n:v)SOURCE_REQUIRE(std::fscanf(file,"%lf",&n)==1);
   const unsigned hz=unsigned(v[0]),flags=unsigned(v[5]);
   if(!v[3]){m={};m.sourceHz(hz);}
   m.speed=m.defaultSpeed()/v[4];
   if(flags&1)m.position={v[6],v[7]};if(flags&2)m.setVelocity({v[8],v[9]});
   if(flags&4)m.setDamping({v[10],v[11]});if(flags&8)m.setSecondaryVelocity({v[12],v[13]},{v[14],v[15]});
   m.applyGravity=v[16];m.allowCrossSide=v[17];m.allowCrossBottom=v[18];
   m.step(stage,v[19]?std::optional(CameraLimits{120,510}):std::nullopt);
   const std::array<double,12> got{m.position.x,m.position.y,m.velocity.x,m.velocity.y,
    m.secondaryVelocity.x,m.secondaryVelocity.y,m.gravity(),double(m.airborne),double(m.touchingSide),double(m.touchingBottom),m.damping.x,m.damping.y};
   for(unsigned k=0;k<got.size();++k)SOURCE_REQUIRE(near(got[k],v[20+k],k));
   ++motions;
  }
  std::fclose(file);return true;
 }
 bool bullet(const std::string& folder){
  FILE* file=std::fopen((folder+"BULLET.IN").c_str(),"r");SOURCE_REQUIRE(file);
  unsigned names=0;SOURCE_REQUIRE(std::fscanf(file,"%u",&names)==1&&names==5);
  std::array<std::string,5> bankNames;for(auto& name:bankNames){char text[64];SOURCE_REQUIRE(std::fscanf(file,"%63s",text)==1);name=text;}
  unsigned count=0;SOURCE_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==6912);
  FighterAsset asset;unsigned current=999;std::optional<FightProjectile> shot;
  const StageGeometry stage{-12000,12000,600,560,{}};
  for(unsigned i=0;i<count;++i){
   std::array<double,14> v{};for(auto& n:v)SOURCE_REQUIRE(std::fscanf(file,"%lf",&n)==1);
   if(!v[4]){
    const unsigned bank=unsigned(v[5]);SOURCE_REQUIRE(bank<bankNames.size());shot.reset();
    if(current!=bank){asset=FighterAsset{};SOURCE_REQUIRE(asset.open((folder+bankNames[bank]).c_str(),FighterAsset::TextureResidency::LowMemory));current=bank;}
    ObjectSpawn spawn{.op=FighterOp::Fire,.id=std::uint16_t(v[6]),.origin={500,-4000},.facing=int(v[2]),.asset=&asset};
    shot.emplace(asset,0,spawn);shot->sourceHz(unsigned(v[0]));
   }
   SOURCE_REQUIRE(shot);if(shot->active())shot->step(stage,false,1/v[7]);
   const auto p=shot->position(),velocity=shot->velocity();
   const std::array<double,6> got{p.x,p.y,velocity.x,velocity.y,double(shot->active()),double(shot->phase())};
   for(unsigned k=0;k<got.size();++k)SOURCE_REQUIRE(near(got[k],v[8+k],12+k));
   ++bullets;
  }
  std::fclose(file);return true;
 }
 bool game(const std::string& folder){
  StageAsset stage;FighterAsset asset;
  SOURCE_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
  SOURCE_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
  for(unsigned hz:{30u,60u}){
   RoundRules rules{};rules.sourceQuality=true;rules.gameHz=hz;rules.shineHz=30;
   VersusRound idle(stage,std::span(&asset,1),std::span(&asset,1),-1,{},BattleMode::Training,{}, {},rules);
   unsigned delivered=0;
   for(unsigned display=0;display<120;++display)delivered+=idle.step({});
   SOURCE_REQUIRE(delivered==2*hz&&idle.ticks()==2*hz);
   const auto before=idle.match().gameClocks()->view();SOURCE_REQUIRE(before.actorFrames==60&&before.effectFrames==60);
   for(unsigned display=0;display<3;++display)idle.pausedDisplayFrame();
   const auto paused=idle.match().gameClocks()->view();SOURCE_REQUIRE(paused.displayFrames==before.displayFrames&&paused.actorFrames==before.actorFrames&&paused.effectFrames==before.effectFrames);
   SOURCE_REQUIRE(idle.step({}));calls+=delivered+1;word(hz);word(delivered);
   for(const auto key:{HumanKey::Attack,HumanKey::Jump}){
    VersusRound tap(stage,std::span(&asset,1),std::span(&asset,1),-1,{},BattleMode::Training,{}, {},rules);
    const unsigned bit=1U<<unsigned(key);std::array<FighterControls,2> controls{};
    controls[0]=HumanInput::controlsForFrame(bit,bit,bit,false);
    const bool first=tap.step(controls);controls[0]=HumanInput::controlsForFrame(0,0,0,false);tap.step(controls);
    SOURCE_REQUIRE(first==(hz==60));SOURCE_REQUIRE(tap.match().actor(0).timelineFrame()!=1);
    ++inputs;word(unsigned(key));
   }
   VersusRound timed(stage,std::span(&asset,1),std::span(&asset,1),1,{},BattleMode::Versus,{}, {},rules);
   for(unsigned display=0;display<800&&timed.phase()!=RoundPhase::Playing;++display)timed.step({},true);
   SOURCE_REQUIRE(timed.phase()==RoundPhase::Playing);
   unsigned elapsed=0;for(;elapsed<100&&timed.phase()==RoundPhase::Playing;++elapsed)timed.step({});
   SOURCE_REQUIRE(timed.phase()==RoundPhase::TimeOver&&timed.seconds()==0&&timed.winner()==-1);
   SOURCE_REQUIRE(elapsed>=60&&elapsed<=64);++rounds;word(elapsed);
   FightMatch hit(asset,asset);const double floor=stage.geometry().playerBottom;
   hit.reset(stage,{350,floor},{500,floor});hit.setSourceProfile(hz,30,stage);
   FighterStrike strike{};strike.key=17;strike.hitType=3;strike.damage=1;strike.hurtFrames=6;strike.slowTimeMs=500;
   SOURCE_REQUIRE(hit.actor(1).receiveStrike({strike,1,350,1,-1,{},true,{480,floor-80,520,floor}}));
   // The receipt is injected outside a MainLogic call. Its source contact
   // effect now starts immediately; the next root decrements its freeze.
   const unsigned hold=400*hz/1000;SOURCE_REQUIRE(hit.hitFreezeFrames()==hold&&hit.scene().blur.x==6);
   const auto clock=hit.gameClocks()->view();
   for(unsigned step=1;step<hold;++step){
    hit.step(stage.geometry(),{},false,Contacts::Locked);
    SOURCE_REQUIRE(hit.gameClocks()->view().actorFrames==clock.actorFrames);
   }
   hit.step(stage.geometry(),{},false,Contacts::Locked);SOURCE_REQUIRE(!hit.hitFreezeFrames());
   SOURCE_REQUIRE(hit.gameClocks()->view().effectFrames>clock.effectFrames);
   for(unsigned step=0;step<hz;++step)hit.step(stage.geometry(),{},false,Contacts::Locked);
   SOURCE_REQUIRE(hit.gameClocks()->clocks().slowRate()==0&&hit.scene().blur.slowRate==0);
   ++freezes;word(hold);
  }
  return true;
 }
 bool run(std::string folder){
  if(!folder.empty()&&folder.back()!='/')folder+='/';
  return motion(folder)&&bullet(folder)&&game(folder)&&musouDeath(folder);
 }
 bool musouDeath(const std::string& folder){
  StageAsset stage;FighterAsset asset;MusouCampaign campaign;
  SOURCE_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
  SOURCE_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
  SOURCE_REQUIRE(campaign.open((folder+"MUSLETH.BVM").c_str()));
  const MusouFighterBank bank{"qa-player",std::span(&asset,1),0,1000,0};
  const MusouFighterBank foe{"qa-enemy",std::span(&asset,1),0,1000,0};
  for(unsigned hz:{30u,60u}){
   MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle run;
   SOURCE_REQUIRE(run.start(campaign,0,stage,{bank,bank,bank},std::span(&foe,1),progress,members,0x12345678));
   run.setSourceProfile(hz,30);
   for(unsigned display=0;display<600;++display){
    run.step({},false);
    if(run.enemies().size()==1&&run.bossCue().kind==MusouBossCue::Kind::None)break;
   }
   SOURCE_REQUIRE(run.enemies().size()==1&&run.bossCue().kind==MusouBossCue::Kind::None);
   auto& player=const_cast<FighterMotion&>(run.member(run.leader()));
   auto& enemy=const_cast<MusouOpponent&>(run.enemies().front());
   enemy.bossAI.reset();const double floor=stage.geometry().playerBottom;
   player.reset({1000,floor},1);player.setHealth(1);
   enemy.motion.reset({1040,floor},-1);enemy.motion.present(FighterCommand::Attack);
   for(unsigned display=0;display<240&&player.health()>0;++display){
    player.place({1000,floor});enemy.motion.place({1040,floor});run.step({},false);
   }
   SOURCE_REQUIRE(player.health()==0&&run.gameClocks()->clocks().slowRate()==2);
   bool moved=false;
   for(unsigned display=0;display<120&&run.wave().result()==MusouResult::Playing;++display){
    if(!run.hitFreeze()&&std::abs(player.motion().velocity.x)>.01){
     const auto before=player.motion();
     for(unsigned wait=0;wait<2;++wait)if(run.step({},false))break;
     const double delta=player.motion().position.x-before.position.x;
     SOURCE_REQUIRE(near(delta,(before.velocity.x+before.secondaryVelocity.x)*(double(AnimationHz)/hz)/2,18));
     moved=true;break;
    }
    run.step({},false);
   }
   SOURCE_REQUIRE(moved);word(hz);++musou;
  }
  return true;
 }
#undef SOURCE_REQUIRE
};
} // namespace bvn::source_delivery_qa

