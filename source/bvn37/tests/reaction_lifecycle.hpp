// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_hurt_timeline.hpp"
#include "fighter_motion.hpp"
#include "source_hud.hpp"
#include "roster_asset.hpp"
#include "versus_round.hpp"
#include "musou_battle.hpp"
#include <cstdio>
#include <cmath>
#include <string>
namespace bvn::reaction_lifecycle_qa {
struct Checks {
 unsigned error=0,referenceRows=0,actors=0,banks=0,forced=0,versus=0,musou=0,pauses=0;std::uint64_t hash=14695981039346656037ull;
 unsigned failedRow=0,failedColumn=0;double actual=0,expected=0;
 void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(8*i))&255))*1099511628211ull;}
 void number(double x){word(unsigned(std::llround(x*1000000)));}
#define REACT_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
 struct View {
  SourceHurtTimeline* timeline=nullptr;unsigned cycle=1,main=0,child=0,impacts=0,touches=0,resumes=0;int facing=1,type=-1;double vx=0,vy=0,dx=0,dy=0,power=0;
  void pose(SourceHurtTimeline::Pose){main=0;child=1%cycle;}
  void advance(){++main;}void children(){child=(child+1)%cycle;}
  void face(int v){facing=v;}void velocity(double x,double y){vx=x;vy=y;}void velocityY(double y){vy=y;}void damping(double x,double y){dx=x;dy=y;}
  void impact(unsigned t,double p){++impacts;type=t;power=p;}void touchFloor(){++touches;}void down(){}void dead(){}
  void recover(){++resumes;timeline->externalFrame(AiState::Normal,true);main=0;child=1%cycle;}
 };
 bool reference(const std::string& folder){
  FILE* file=std::fopen((folder+"REACT.IN").c_str(),"r");REACT_REQUIRE(file);unsigned count=0;REACT_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==19200);
  SourceHurtTimeline t;View view;bool alive=true;
  for(unsigned row=0;row<count;++row){
   std::array<double,46> v{};for(auto& x:v)REACT_REQUIRE(std::fscanf(file,"%lf",&x)==1);
   if(v[2]==0){t.reset();view={&t,2+unsigned(v[1])%11};alive=true;}
   const auto scenario=unsigned(v[1]),tick=unsigned(v[2]),op=unsigned(v[3]);
   SourceHurtTimeline::Input input{alive,bool(v[9]),v[10],v[11],v[12],view.facing};
   if(op==1){
    if(tick==0&&scenario%4==2)alive=input.alive=false;
    double y=v[8];if(input.air&&y<=0)y-=3;else if(!input.air&&y<0)y-=6;
    if(v[4])t.launch(v[7]*v[13],y,input,true,view);
    else{t.stun(std::max(4,int(std::round(v[6]*.001*30))+3),v[5]==11,view);view.velocity(v[7]*v[13],y);view.damping(.1,.5);}
   }
   if(op==3){alive=false;const auto s=t.state();if(s!=AiState::Hurt&&s!=AiState::Flying&&s!=AiState::Down&&s!=AiState::Bounce&&s!=AiState::Dead)t.down(s,view);}
   if(op==4)view.recover();
   input.alive=alive;input.facing=view.facing;if(v[14])t.animate(input,view);
   const bool pose=t.pose()!=SourceHurtTimeline::Pose::None;
   const std::array<double,30> got{double(t.state()),double(t.pose()),double(t.fly()),t.active()?double(t.age()):0,double(t.hold()),double(t.flyFrames()),double(t.downFrames()),pose?double(t.playing()):0,pose?double(t.playFrames()):0,double(t.delay()),t.delay()==SourceHurtTimeline::Delay::None?0:double(t.delayFrames()),double(t.heavy()),t.minY(),t.hitY(),double(t.hurting()),double(t.flying()),view.vx,view.vy,view.dx,view.dy,double(view.facing),double(t.downEvents()),double(t.deadEvents()),double(view.impacts),double(view.type),view.power,pose?double(view.main):0,pose?double(view.child):0,double(view.touches),double(view.resumes)};
   for(unsigned k=0;k<got.size();++k){if(std::abs(got[k]-v[k+16])>1e-9){failedRow=row;failedColumn=k;actual=got[k];expected=v[k+16];error=__LINE__;std::fclose(file);return false;}number(got[k]);}
   ++referenceRows;
  }
  std::fclose(file);return true;
 }
 bool actorCases(const StageAsset& stage,std::span<const FighterAsset> forms,unsigned initial=0){
  const auto& art=forms[initial];
  for(unsigned hz:{30u,60u})for(unsigned kind=0;kind<8;++kind){
   FighterMotion actor(forms,initial);actor.reset({450,stage.geometry().playerBottom},-1);actor.sourceHz(hz);actor.setRoundStats(1000,1);actor.setHealth(kind%2?1000:1);
   if(kind>=4){auto& motion=const_cast<Motion&>(actor.motion());motion.position.y-=60;motion.airborne=true;motion.secondaryVelocity={2,-1};}
   FighterStrike strike{};strike.key=700+kind;strike.damage=10;strike.hurtFrames=4;strike.hurtTimeMs=0;strike.hitX=6;strike.hitY=kind%4==3?8:0;strike.flags=kind%4>=2?1:0;strike.hitType=kind%4==1?11:1;
   REACT_REQUIRE(actor.receiveStrike({strike,1,400}));REACT_REQUIRE(actor.aiState()==(strike.launches()?AiState::Flying:AiState::Hurt));
   REACT_REQUIRE(!actor.deathComplete());SourceHpBar bar;bar.render(1,AiState::Normal,hz);bar.render(actor.health()/1000,actor.aiState(),hz);
   if((actor.health()<=0))REACT_REQUIRE(strike.launches()?(bar.red()<1&&bar.delay()==0):(bar.red()==1&&bar.delay()==100));
   bool down=false,dead=false,flyChild=false,recovered=false;unsigned previousPhase=actor.phase();
   for(unsigned tick=0;tick<500;++tick){
    // Actual native physics and collision path, with explicit actor-animation
    // calls at 30 Hz, separately from the supplied-input source-method trace.
    const bool animate=hz==30||tick%2==0;const auto before=actor.aiState();
    actor.step(stage.geometry(),{},animate);
    if(actor.sourceHurt().pose()==SourceHurtTimeline::Pose::Fly&&actor.phase()!=previousPhase)flyChild=true;
    previousPhase=actor.phase();
    if(actor.aiState()==AiState::Down&&!down){down=true;REACT_REQUIRE(!actor.deathComplete());}
    if(actor.deathComplete()){REACT_REQUIRE((actor.health()<=0)&&down&&before==AiState::Down);dead=true;break;}
    if(actor.standing()&&!actor.sourceHurt().active()&&!(actor.health()<=0)){recovered=true;break;}
   }
   if(!((actor.health()<=0)?dead:recovered))std::printf("actor hz=%u kind=%u hp=%.2f state=%u pose=%u fly=%u active=%u age=%u hold=%d phase=%u frame=%u y=%.3f air=%u\n",hz,kind,actor.health(),unsigned(actor.aiState()),unsigned(actor.sourceHurt().pose()),unsigned(actor.sourceHurt().fly()),actor.sourceHurt().active(),actor.sourceHurt().age(),actor.sourceHurt().hold(),actor.phase(),actor.timelineFrame(),actor.motion().position.y,actor.motion().airborne);
   REACT_REQUIRE((actor.health()<=0)?dead:recovered);
   if(strike.launches()&&kind>=4&&art.clip(MoveClip::HurtFly).count>1)REACT_REQUIRE(flyChild);
   word(hz);word(kind);word(actor.aiState()==AiState::Dead);++actors;
  }
  return true;
 }
 bool forceCases(const StageAsset& stage,std::span<const FighterAsset> forms,unsigned initial=0){
  for(unsigned hz:{30u,60u})for(bool attack:{false,true}){
   FighterMotion actor(forms,initial);actor.reset({450,stage.geometry().playerBottom},1);actor.sourceHz(hz);
   if(attack)actor.present(FighterCommand::Attack);const auto state=actor.aiState();actor.die();
   REACT_REQUIRE(actor.health()==0&&actor.aiState()==state&&!actor.deathComplete());
   unsigned calls=0;for(unsigned tick=0;tick<20&&!actor.deathComplete();++tick){
    const bool animate=hz==30||tick%2==0;actor.step(stage.geometry(),{},animate);if(animate)++calls;
    if(calls<3)REACT_REQUIRE(actor.aiState()==state);
    if(calls==3)REACT_REQUIRE(actor.aiState()==AiState::Down&&!actor.deathComplete());
   }
   REACT_REQUIRE(actor.deathComplete()&&calls==4);++forced;word(hz);word(unsigned(state));
  }
  return true;
 }
 bool bankCases(const std::string& folder,const StageAsset& stage){
  FILE* list=std::fopen((folder+"TYPED.IN").c_str(),"r");REACT_REQUIRE(list);unsigned count=0;REACT_REQUIRE(std::fscanf(list,"%u",&count)==1&&count==78);
  RosterAsset roster;REACT_REQUIRE(roster.open((folder+"ROSTER.BVR").c_str()));
  for(unsigned i=0;i<count;++i){char name[64]{};REACT_REQUIRE(std::fscanf(list,"%63s",name)==1);
   std::vector<FighterAsset> forms;unsigned initial=0;
   for(unsigned j=0;j<roster.size();++j)if(std::string(roster.fighter(j).file)==name){
    const auto ids=roster.forms(j);forms.resize(ids.size());initial=roster.fighter(j).form;
    for(unsigned k=0;k<ids.size();++k){if(ids[k]==MissingFighter)continue;if(!forms[k].open((folder+roster.fighter(ids[k]).file).c_str(),FighterAsset::TextureResidency::LowMemory)){std::printf("missing bank %s family %s\n",roster.fighter(ids[k]).file,name);error=__LINE__;return false;}}break;
   }
   if(forms.empty()){forms.resize(1);REACT_REQUIRE(forms[0].open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory));}
   if(!actorCases(stage,forms,initial)||!forceCases(stage,forms,initial)){std::printf("reaction bank %s\n",name);std::fclose(list);return false;}++banks;
  }
  std::fclose(list);return true;
 }
 bool sessionCases(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
  MusouCampaign campaign;REACT_REQUIRE(campaign.open((folder+"MUSLETH.BVM").c_str()));
  const MusouFighterBank bank{"qa-player",std::span(&art,1),0,1000,0},foe{"qa-enemy",std::span(&art,1),0,1000,0};
  const auto floor=stage.geometry().playerBottom;
  for(unsigned hz:{30u,60u})for(bool self:{false,true}){
   MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;
   REACT_REQUIRE(game.start(campaign,0,stage,{bank,bank,bank},std::span(&foe,1),progress,members,0x12345678));game.setSourceProfile(hz,30);
   for(unsigned tick=0;tick<1200;++tick){game.step({},false);for(const auto& e:game.enemies())const_cast<MusouOpponent&>(e).bossAI.reset();
    if(!game.presentation().starting()&&game.enemies().size()==1&&game.bossCue().kind==MusouBossCue::Kind::None)break;}
   REACT_REQUIRE(game.enemies().size()==1&&game.bossCue().kind==MusouBossCue::Kind::None);
   auto& player=const_cast<FighterMotion&>(game.member(0));auto& enemy=const_cast<MusouOpponent&>(game.enemies().front());
   player.reset({350,floor},1);enemy.motion.reset({1100,floor},-1);enemy.brain={};
   FighterMotion& victim=self?player:enemy.motion;victim.setHealth(1);
   FighterStrike hit{};hit.key=900;hit.damage=10;hit.hurtFrames=4;hit.hitType=1;REACT_REQUIRE(victim.receiveStrike({hit,self?-1:1,500}));
   REACT_REQUIRE(victim.aiState()==AiState::Hurt&&game.wave().result()==MusouResult::Playing&&game.kills()==0);
   const auto money=progress.money();bool down=false,complete=false;
   for(unsigned tick=0;tick<2500;++tick){
    const auto previous=self?game.member(0).aiState():(game.enemies().empty()?AiState::Dead:game.enemies().front().motion.aiState());
    game.step({},false);for(const auto& e:game.enemies())const_cast<MusouOpponent&>(e).bossAI.reset();
    const auto now=self?game.member(0).aiState():(game.enemies().empty()?AiState::Dead:game.enemies().front().motion.aiState());
    if(now==AiState::Down&&!down){
     down=true;REACT_REQUIRE(game.wave().result()==MusouResult::Playing&&game.kills()==0&&progress.money()==money);
     game.manualPause();const auto age=self?game.member(0).sourceHurt().age():game.enemies().front().motion.sourceHurt().age();
     for(unsigned display=0;display<15;++display)game.pausedDisplayFrame();
     REACT_REQUIRE((self?game.member(0).sourceHurt().age():game.enemies().front().motion.sourceHurt().age())==age);
     REACT_REQUIRE(game.wave().result()==MusouResult::Playing&&game.kills()==0&&progress.money()==money);game.manualResume();++pauses;
    }
    if(game.wave().result()!=MusouResult::Playing){
     REACT_REQUIRE(down&&previous==AiState::Down&&now==AiState::Dead);
     REACT_REQUIRE(self?(game.wave().result()==MusouResult::Defeated&&game.kills()==0):(game.wave().result()==MusouResult::Won&&game.kills()==1&&progress.money()>money));complete=true;break;
    }
   }
   REACT_REQUIRE(complete);++musou;word(hz);word(self);word(progress.money());
  }
  for(unsigned hz:{30u,60u}){
   RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;rules.maxHealth=5000;
   VersusRound game(stage,std::span(&art,1),std::span(&art,1),60,{},BattleMode::Versus,{}, {},rules);
   for(unsigned d=0;d<200&&game.sourceTransitionRendering();++d)game.step({});do{}while(!game.step({},true));
   for(unsigned d=0;d<1800&&game.phase()!=RoundPhase::Playing;++d)game.step({});REACT_REQUIRE(game.phase()==RoundPhase::Playing);
   auto& a=const_cast<FighterMotion&>(game.match().actor(0));auto& b=const_cast<FighterMotion&>(game.match().actor(1));
   a.reset({350,floor},1);b.reset({390,floor},-1);b.setHealth(1);a.present(FighterCommand::Attack);
   for(unsigned d=0;d<300&&b.health()>0;++d)game.step({});
   REACT_REQUIRE(b.health()==0&&b.aiState()==AiState::Hurt&&!b.deathComplete());
   bool down=false,delay=false;
   for(unsigned d=0;d<2500;++d){const bool wasDead=b.deathComplete();game.step({});
    if(b.aiState()==AiState::Down){down=true;REACT_REQUIRE(game.phase()!=RoundPhase::WinnerDelay);}
    if(game.phase()==RoundPhase::WinnerDelay){REACT_REQUIRE(down&&wasDead&&b.deathComplete());delay=true;break;}
   }
   REACT_REQUIRE(delay);++versus;word(hz);
  }
  return true;
 }
 bool run(const std::string& folder){
  if(!reference(folder))return false;
  StageAsset stage;FighterAsset art;REACT_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));REACT_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str()));
  return bankCases(folder,stage)&&sessionCases(folder,stage,art);
 }
#undef REACT_REQUIRE
};
}
