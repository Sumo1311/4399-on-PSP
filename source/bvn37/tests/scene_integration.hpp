// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scene_order.hpp"
#include "fight_match.hpp"
#include "musou_battle.hpp"
#include <map>
#include <string>
namespace bvn::scene_order_qa {
struct Integration {
 MusouFighterBank selfBank{},enemyBank{};
 unsigned error=0,bulletRows=0,matches=0,switches=0,clashes=0,births=0,skips=0;std::uint64_t hash=14695981039346656037ull;
 void word(unsigned value){for(unsigned i=0;i<4;++i)hash=(hash^((value>>(8*i))&255))*1099511628211ull;}
#define ORDER_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
 struct Trace {std::vector<unsigned> render;std::vector<std::pair<unsigned,unsigned>> pairs;};
 static SourceScene::Trace trace(Trace& t){return {&t,[](void* p,SourceScene::Key key){static_cast<Trace*>(p)->render.push_back(key);},[](void* p,SourceScene::Key a,SourceScene::Key b){static_cast<Trace*>(p)->pairs.emplace_back(a,b);}};}
 bool bulletCases(const std::string& folder,const FighterAsset& art){
  FILE* f=std::fopen((folder+"BULLETHP.IN").c_str(),"r");ORDER_REQUIRE(f);unsigned count;ORDER_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==8192);
  std::optional<FightProjectile> bullet;
  for(unsigned row=0;row<count;++row){unsigned session,step,fromBullet,hit;double power;int hp;ORDER_REQUIRE(std::fscanf(f,"%u%u%lf%u%d%u",&session,&step,&power,&fromBullet,&hp,&hit)==6);
   if(!step){ObjectSpawn spawn{};spawn.asset=&art;spawn.facing=1;bullet.emplace(art,0,spawn);ORDER_REQUIRE(bullet->maxHealth()==100);}
   bullet->receiveSourceHit(fromBullet?100:power);ORDER_REQUIRE(bullet->health()==hp&&bullet->impacting()==bool(hit));word(unsigned(hp));word(hit);++bulletRows;
  }
  std::fclose(f);return true;
 }
 bool matchCases(const StageAsset& stage,const FighterAsset& rival,const FighterAsset& pain){
  for(unsigned hz:{30u,60u})for(unsigned side:{0u,1u})for(const auto* art:{&rival,&pain}){
   FightMatch game(side?rival:*art,side?*art:rival);const double y=stage.geometry().playerBottom;game.reset(stage,{350,y},{570,y});game.setSourceProfile(hz,30,stage);Trace t;game.sourceScene().trace(trace(t));bool born=false,skipped=false;
   for(unsigned frame=0;frame<hz*10;++frame){
    if(frame==6)game.actor(side).present(FighterCommand::Special);
    const std::vector<unsigned> before(game.sourceScene().displays().begin(),game.sourceScene().displays().end());
    std::map<unsigned,Point> positions;for(auto key:before)if(key&SourceScene::Object){const auto i=game.sourceScene().storage(key);positions[key]=std::visit([](const auto& v){return v.position();},game.objects()[i]);}
    t.render.clear();t.pairs.clear();game.stepMainFrame(stage.geometry(),{},Contacts::Locked);
    ORDER_REQUIRE(t.render.size()>=2&&t.render[0]==0&&t.render[1]==1&&game.sourceScene().stats().rangeFaults==0);
    for(auto key:t.render){word(key);if((key&SourceScene::Object)&&std::ranges::find(before,key)==before.end()){born=true;++births;}}
    for(unsigned i=2;i+1<before.size();++i)if(std::ranges::find(t.render,before[i])!=t.render.end()&&!game.sourceScene().registered(before[i])&&game.sourceScene().registered(before[i+1])&&std::ranges::find(t.render,before[i+1])==t.render.end()){
     const auto next=game.sourceScene().storage(before[i+1]);const auto position=std::visit([](const auto& v){return v.position();},game.objects()[next]);
     ORDER_REQUIRE(position.x==positions[before[i+1]].x&&position.y==positions[before[i+1]].y);skipped=true;++skips;
    }
    game.stepDetached(true);
   }
   ORDER_REQUIRE(born);if(art==&pain){ORDER_REQUIRE(skipped);}
   ++matches;word(hz);word(side);
  }
  return true;
 }
 static FighterMotion& actor(const FighterMotion& value){return const_cast<FighterMotion&>(value);}
 bool enter(MusouBattle& game,const MusouCampaign& campaign,const StageAsset& stage,const FighterAsset& art,unsigned hz,unsigned enemyCount,MusouProgress& progress,std::array<MusouMember,3>& members){
  selfBank={"qa-player",std::span(&art,1),0,1000,0};enemyBank={"qa-enemy",std::span(&art,1),0,1000,0};ORDER_REQUIRE(game.start(campaign,0,stage,{selfBank,selfBank,selfBank},std::span(&enemyBank,1),progress,members,0x12345678));game.setSourceProfile(hz,30);
  bool ready=false;for(unsigned frame=0;frame<2000;++frame){game.step({},false);for(const auto& e:game.enemies()){auto& v=const_cast<MusouOpponent&>(e);v.brain={};v.bossAI.reset();}if(game.enemies().size()==enemyCount&&!game.presentation().starting()&&game.bossCue().kind==MusouBossCue::Kind::None&&game.sourceControl()->running()&&game.sourceControl()->animations().empty()){ready=true;break;}}
  ORDER_REQUIRE(ready);return true;
 }
 bool switchCases(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
  MusouCampaign campaign;ORDER_REQUIRE(campaign.open((folder+"MUSMIN.BVM").c_str()));
  for(unsigned hz:{30u,60u}){
   MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;if(!enter(game,campaign,stage,art,hz,6,progress,members))return false;
   Trace t;game.sourceScene().trace(trace(t));const auto before=std::vector<unsigned>(game.sourceScene().displays().begin(),game.sourceScene().displays().end());ORDER_REQUIRE(before.size()>=7&&before[0]==0);
   bool switched=false;for(unsigned frame=0;frame<12;++frame){t.render.clear();game.step({},true);if(game.leader()==1){switched=true;break;}}
   ORDER_REQUIRE(switched&&!game.sourceScene().registered(0)&&game.sourceScene().registered(1));ORDER_REQUIRE(t.render.size()>=7&&t.render.front()==before[1]&&t.render.back()==1);ORDER_REQUIRE(game.sourceScene().children(0).back()==1);
   for(auto key:t.render){word(key);}
   ++switches;
  }
  return true;
 }
 bool clashCases(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
  MusouCampaign campaign;ORDER_REQUIRE(campaign.open((folder+"MUSLETH.BVM").c_str()));
  for(unsigned hz:{30u,60u}){
   MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;if(!enter(game,campaign,stage,art,hz,1,progress,members))return false;
   auto& player=actor(game.member(game.leader()));auto& enemy=const_cast<MusouOpponent&>(game.enemies().front());const double y=stage.geometry().playerBottom;
   player.reset({350,y},1);enemy.motion.reset({650,y},-1);player.setQi(0);enemy.motion.setQi(0);enemy.brain={};enemy.bossAI.reset();const double hp=player.health(),foeHp=enemy.motion.health();
   player.present(FighterCommand::Special);enemy.motion.present(FighterCommand::Special);bool met=false,impacted=false;
   for(unsigned frame=0;frame<500;++frame){game.step({},false);enemy.brain={};enemy.bossAI.reset();
    bool own=false,other=false;for(const auto& o:game.objects())if(const auto* b=std::get_if<FightProjectile>(&o)){own|=b->owner()<3;other|=b->owner()>=3;impacted|=b->impacting();}met|=own&&other;
    ORDER_REQUIRE(player.health()==hp&&enemy.motion.health()==foeHp&&game.sourceScene().stats().rangeFaults==0);
   }
   ORDER_REQUIRE(met&&impacted&&player.qi()==5&&enemy.motion.qi()==5);++clashes;word(hz);word(unsigned(player.health()));
  }
  return true;
 }
 bool run(const char* root){const std::string folder=root;StageAsset stage;FighterAsset art,pain;ORDER_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));ORDER_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str()));ORDER_REQUIRE(pain.open((folder+"PAIN.BVF").c_str()));return bulletCases(folder,art)&&matchCases(stage,art,pain)&&switchCases(folder,stage,art)&&clashCases(folder,stage,art);}
#undef ORDER_REQUIRE
};
}
