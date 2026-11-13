// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scene_integration.hpp"
#include <cmath>
namespace bvn::emission_qa {
using Call=std::array<int,4>;
struct Context{
 FighterMotion* actor=nullptr;
 struct Node{std::uint32_t key;bool referenced=false;};
 std::vector<Node> nodes;std::vector<Call> calls;unsigned bullets=0,unexpected=0;
 void record(int code,int value=0,bool point=false){auto p=point?actor->motion().position:Point{};calls.push_back({code,value,int(p.x),int(p.y)});}
 void effects(FighterEffects effect){
  for(const auto& r:effect.controllerRequests){if(r.op==SourceControlOp::Bisha)record(12);else if(r.op==SourceControlOp::EndBisha)record(13);else ++unexpected;}
  if(effect.slowTimeMs)record(9,effect.slowTimeMs);
  if(effect.shakeY||effect.shakeRequested)record(1,int(effect.shakeY));
  if(effect.shakeLoopY||effect.shakeLoopX){if(effect.startShake)record(10,int(effect.shakeLoopX.value_or(0)*256+effect.shakeLoopY.value_or(0)));else record(11);}
  if(effect.ground)record(2,effect.ground->sound,true);
  for(const auto& s:effect.shines)record(3,s.color);
  for(unsigned i=0;i<effect.walkSounds;++i)record(14);
  if(effect.reaction||!effect.reactions.empty()||!effect.contactEffects.empty()||effect.counterSounds||effect.ghostStepSound||effect.ghostFootSounds)++unexpected;
 }
 void spawn(const ObjectSpawn& s){
  if(s.op==FighterOp::Fire){record(17,s.id,true);++bullets;return;}
  const auto key=s.asset->attacker(s.id).key;
  if(s.op==FighterOp::Summon){record(4,s.id,true);nodes.push_back({key});return;}
  auto found=std::ranges::find(nodes,key,&Node::key);
  if(s.op==FighterOp::RememberAttacker){record(15,s.id);for(auto& n:nodes)n.referenced=false;if(found!=nodes.end())found->referenced=true;return;}
  record(s.op==FighterOp::ControlRemembered?16:5,s.id);
  if(s.op==FighterOp::ControlRemembered)found=std::ranges::find_if(nodes,[&](const auto& n){return n.key==key&&n.referenced;});
  if(found!=nodes.end()&&s.jumpFrame==FinishClip)nodes.erase(found);
 }
 bool query(std::uint32_t key){const bool found=std::ranges::find(nodes,key,&Node::key)!=nodes.end();record(6,found);return found;}
 void buff(const BuffEffectRequest& r){record(7,int(std::lround(r.buff->value*1000)),true);record(8,int(std::lround(r.buff->value*1000)),true);}
 FighterMotion::SourceHooks hooks(){return {this,0,nullptr,[](void* p,FighterEffects&& e){static_cast<Context*>(p)->effects(std::move(e));},nullptr,nullptr,
  [](void* p,unsigned,const ObjectSpawn& s,const FighterMotion*){static_cast<Context*>(p)->spawn(s);},
  [](void* p,unsigned,std::uint32_t key){return static_cast<Context*>(p)->query(key);},
  [](void* p,const BuffEffectRequest& r){static_cast<Context*>(p)->buff(r);}};}
};
struct Checks{
 unsigned error=0,rows=0,calls=0,queries=0,peak=0,ordinary=0,musou=0,assists=0,lookups=0,switches=0,roots=0,failedRow=0,failedColumn=0;std::uint64_t actual=0,expected=0,hash=14695981039346656037ull;
 void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(8*i))&255))*1099511628211ull;}
#define EMIT_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
 bool reference(const std::string& folder){
  FighterAsset art;EMIT_REQUIRE(art.open((folder+"EMITTER.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));FILE* f=std::fopen((folder+"EMIT.IN").c_str(),"r");EMIT_REQUIRE(f);unsigned n;EMIT_REQUIRE(std::fscanf(f,"%u",&n)==1&&n==768);
  for(unsigned row=0;row<n;++row){unsigned context,cmd,hz,attackers,bullets,count;int facing,x,y;EMIT_REQUIRE(std::fscanf(f,"%u%u%u%d%d%d%u%u%u",&context,&cmd,&hz,&facing,&x,&y,&attackers,&bullets,&count)==9);std::vector<Call> want(count);for(auto& c:want)for(auto& v:c)EMIT_REQUIRE(std::fscanf(f,"%d",&v)==1);
   FighterMotion actor(art);actor.sourceHz(hz);actor.orderedSourceControl();actor.reset({double(x),double(y)},facing);actor.takeEffects();actor.takeSpawns();actor.takeBuffEffects();Context got;got.actor=&actor;actor.sourceHooks(got.hooks());actor.present(FighterCommand(cmd));
   if(got.calls!=want){failedRow=row;failedColumn=0;actual=got.calls.size();expected=want.size();for(unsigned i=0;i<std::min(got.calls.size(),want.size());++i)if(got.calls[i]!=want[i]){failedColumn=i+1;actual=got.calls[i][0];expected=want[i][0];break;}EMIT_REQUIRE(false);}
   EMIT_REQUIRE(!got.unexpected&&got.nodes.size()==attackers&&got.bullets==bullets&&actor.takeEffects().empty()&&actor.takeSpawns().empty()&&actor.takeBuffEffects().empty());
   for(const auto& c:got.calls){for(int v:c)word(unsigned(v));queries+=c[0]==6;}calls+=got.calls.size();peak=std::max(peak,unsigned(got.nodes.size()));word(got.nodes.size());word(got.bullets);++rows;
  }
  std::fclose(f);EMIT_REQUIRE(peak==12);return true;
 }
 bool ordered(const SourcePaint& p,const SourceScene& scene,const std::vector<SourcePaint::Key>& before){
  using P=SourcePaint;P::Key object=P::None,ground=P::None;
  for(auto k:p.players()){
   if(P::kind(k)==P::Kind::Sprite&&(P::id(k)&SourceScene::Object))object=k;
   if(P::kind(k)==P::Kind::Ground&&std::ranges::find(before,k)==before.end())ground=k;
  }
  if(object==P::None)return false;
  EMIT_REQUIRE(ground!=P::None&&p.index(ground)<p.index(object)&&p.stats().faults==0&&scene.stats().rangeFaults==0);word(unsigned(p.hash()));word(unsigned(p.hash()>>32));return true;
 }
 bool scenes(const std::string& folder){
  StageAsset stage;FighterAsset hinata,enemy;EMIT_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));EMIT_REQUIRE(hinata.open((folder+"HINATA.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)&&enemy.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
  for(unsigned hz:{30u,60u})for(unsigned side:{0u,1u}){
   FightMatch game(side?enemy:hinata,side?hinata:enemy);const auto y=stage.geometry().playerBottom;game.reset(stage,{350,y},{650,y});game.setSourceProfile(hz,30,stage);game.actor(side).present(FighterCommand::Special);bool seen=false;
   for(unsigned f=0;f<hz*3;++f){const auto& p=game.sourceScene().paint();const std::vector<SourcePaint::Key> before(p.players().begin(),p.players().end());game.stepMainFrame(stage.geometry(),{},Contacts::Locked);if(ordered(p,game.sourceScene(),before)){seen=true;break;}EMIT_REQUIRE(!error);}
   EMIT_REQUIRE(seen);++ordinary;
  }
  MusouCampaign campaign;EMIT_REQUIRE(campaign.open((folder+"MUSMIN.BVM").c_str()));
  for(unsigned hz:{30u,60u}){
   MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;scene_order_qa::Integration fixture;EMIT_REQUIRE(fixture.enter(game,campaign,stage,hinata,hz,6,progress,members));const_cast<FighterMotion&>(game.member(game.leader())).present(FighterCommand::Special);bool seen=false;
   for(unsigned f=0;f<hz*3;++f){const auto& p=game.sourceScene().paint();const std::vector<SourcePaint::Key> before(p.players().begin(),p.players().end());game.step({},false);if(ordered(p,game.sourceScene(),before)){seen=true;break;}EMIT_REQUIRE(!error);}
   EMIT_REQUIRE(seen);++musou;
  }
  return true;
 }
 bool assister(const std::string& folder){
  FighterAsset art,ownerArt;StageAsset stage;EMIT_REQUIRE(art.open((folder+"AYLS.BVS").c_str(),FighterAsset::TextureResidency::LowMemory)&&ownerArt.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)&&stage.open((folder+"JIANCHANG.BVN").c_str()));
  for(unsigned hz:{30u,60u})for(int facing:{1,-1}){
   FighterMotion owner(ownerArt),target(ownerArt);owner.sourceHz(hz);owner.reset({350,stage.geometry().playerBottom},facing);target.reset({550,stage.geometry().playerBottom},-facing);Context ctx;ctx.actor=&owner;owner.sourceHooks(ctx.hooks());ObjectSpawn spawn{FighterOp::Summon,0,owner.motion().position,facing,&art};FightAttacker actor(art,0,spawn,target.motion().position);actor.sourceHz(hz);
   actor.sourceEmissionHook({&ctx,[](void* p,FighterEffects&& e){static_cast<Context*>(p)->effects(std::move(e));},[](void* p,unsigned,const ObjectSpawn& s,const FighterMotion*){static_cast<Context*>(p)->spawn(s);},nullptr});
   bool found=false;for(unsigned frame=0;frame<30;++frame){ctx.calls.clear();std::vector<ObjectSpawn> shots;const auto effects=actor.step(stage.geometry(),true,{},target,1,shots,&owner,&owner);EMIT_REQUIRE(effects.empty()&&shots.empty()&&!ctx.unexpected);unsigned created=0;for(const auto& c:ctx.calls)created+=c[0]==4;if(created){EMIT_REQUIRE(ctx.calls.front()[0]==3&&created==3);for(const auto& c:ctx.calls)for(int v:c)word(unsigned(v));found=true;break;}}
   EMIT_REQUIRE(found);++assists;
  }
  return true;
 }
 bool lookup(const std::string& folder){
  FighterAsset art,peerArt,hinata;StageAsset stage;MusouCampaign campaign;
  EMIT_REQUIRE(art.open((folder+"TEAMMATE.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)&&peerArt.open((folder+"TEAMMATE.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)&&hinata.open((folder+"HINATA.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)&&stage.open((folder+"JIANCHANG.BVN").c_str())&&campaign.open((folder+"MUSMIN.BVM").c_str()));
  FILE* input=std::fopen((folder+"TEAM.IN").c_str(),"r");EMIT_REQUIRE(input);unsigned n=0;EMIT_REQUIRE(std::fscanf(input,"%u",&n)==1&&n==4);
  for(unsigned row=0;row<n;++row){
   unsigned hz,team;std::array<int,9> expectedOwners{};EMIT_REQUIRE(std::fscanf(input,"%u%u",&hz,&team)==2);for(auto& v:expectedOwners)EMIT_REQUIRE(std::fscanf(input,"%d",&v)==1);
   MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;scene_order_qa::Integration fixture;EMIT_REQUIRE(fixture.enter(game,campaign,stage,hinata,hz,6,progress,members));
   auto& p0=const_cast<FighterMotion&>(game.member(0));auto& p1=const_cast<FighterMotion&>(game.member(1));auto e=game.enemies().begin();auto& e0=const_cast<MusouOpponent&>(*e++);auto& e1=const_cast<MusouOpponent&>(*e);
   const double y=stage.geometry().playerBottom;
   p0=FighterMotion(art);p0.reset({350,y},1);p1=FighterMotion(peerArt);p1.reset({350,y},1);
   e0.motion=FighterMotion(art);e0.motion.reset({1500,y},-1);e1.motion=FighterMotion(peerArt);e1.motion.reset({1500,y},-1);e0.brain={};e0.bossAI.reset();e1.brain={};e1.bossAI.reset();game.setSourceProfile(hz,30);
   for(unsigned f=0;f<2000&&game.presentation().starting();++f){game.step({},false);for(const auto& enemy:game.enemies()){auto& value=const_cast<MusouOpponent&>(enemy);value.brain={};value.bossAI.reset();}}EMIT_REQUIRE(!game.presentation().starting());
   FighterMotion& a=team?e0.motion:p0;FighterMotion& b=team?e1.motion:p1;FighterMotion& other=team?p0:e0.motion;
   const unsigned aid=team?e0.id:0,bid=team?e1.id:1,oid=team?0:e0.id;
   const auto relative=[&](unsigned id){return id==aid?0:id==bid?1:id==oid?2:-2;};
   const auto find=[&](unsigned id,unsigned key)->FightAttacker*{for(const auto& o:game.objects())if(const auto* v=std::get_if<FightAttacker>(&o);v&&v->active()&&v->owner()==id&&v->key()==key&&!v->assister())return const_cast<FightAttacker*>(v);return nullptr;};
   const auto saved=[&](unsigned id){for(const auto& o:game.objects())if(const auto* v=std::get_if<FightAttacker>(&o);v&&v->active()&&v->referenced(0,id))return relative(v->owner());return -1;};
   std::array<int,9> got{};const auto shine0=game.scene().shines.size();b.present(FighterCommand(1));got[0]=game.scene().shines.size()==shine0?-1:-2;
   a.present(FighterCommand(0));auto* first=find(aid,art.attacker(0).key);EMIT_REQUIRE(first);
   if(!team){for(unsigned f=0;f<6&&game.leader()!=1;++f)game.step({},true);EMIT_REQUIRE(game.leader()==1&&first->active()&&!game.sourceScene().registered(0)&&game.sourceScene().registered(1));++switches;}
   const auto shine1=game.scene().shines.size();b.present(FighterCommand(1));got[1]=game.scene().shines.size()==shine1+1?relative(first->owner()):-2;
   a.present(FighterCommand(3));got[2]=saved(aid);b.present(FighterCommand(3));got[3]=saved(bid);
   b.present(FighterCommand(5));auto* second=find(bid,art.attacker(1).key);EMIT_REQUIRE(second);b.present(FighterCommand(7));got[4]=saved(bid);got[5]=saved(aid);
   EMIT_REQUIRE(first->referenced(0,aid)&&!first->referenced(0,bid)&&second->referenced(0,bid));
   // Prime a nonzero pose, then use the caller's saved controller through the actual event hook.
   EMIT_REQUIRE(first->asset().clip(first->definition().clip).count>2);
   first->applyControl({.op=FighterOp::ControlAttacker,.id=0,.asset=&peerArt,.jumpFrame=2,.playImmediately=true});EMIT_REQUIRE(first->phase()==2);a.present(FighterCommand(4));std::vector<ObjectSpawn> shots;
   first->step(stage.geometry(),false,{},other,1,shots,&a,&a);EMIT_REQUIRE(first->phase()==0&&shots.empty());
   b.present(FighterCommand(2));EMIT_REQUIRE(!first->active());const auto shine2=game.scene().shines.size();b.present(FighterCommand(1));got[6]=game.scene().shines.size()==shine2?-1:-2;
   other.present(FighterCommand(0));auto* enemy=find(oid,art.attacker(0).key);EMIT_REQUIRE(enemy);const auto shine3=game.scene().shines.size();b.present(FighterCommand(1));got[7]=game.scene().shines.size()==shine3?-1:-2;
   const auto shine4=game.scene().shines.size();other.present(FighterCommand(1));got[8]=game.scene().shines.size()==shine4+1?relative(enemy->owner()):-2;
   if(got!=expectedOwners){failedRow=row;for(unsigned c=0;c<got.size();++c)if(got[c]!=expectedOwners[c]){failedColumn=c;actual=got[c];expected=expectedOwners[c];break;}EMIT_REQUIRE(false);}
   EMIT_REQUIRE(game.sourceScene().stats().rangeFaults==0&&game.sourceScene().paint().stats().faults==0);word(hz);word(team);for(auto v:got)word(v);++lookups;
  }
  std::fclose(input);return true;
 }
 bool rootAssist(const std::string& folder){
  FighterAsset art,assist,rival;StageAsset stage;EMIT_REQUIRE(art.open((folder+"TEAMMATE.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)&&assist.open((folder+"ASSROOT.BVS").c_str(),FighterAsset::TextureResidency::LowMemory)&&rival.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)&&stage.open((folder+"JIANCHANG.BVN").c_str()));EMIT_REQUIRE(assist.assister()&&assist.attacker(0).key==art.attacker(0).key);
  for(unsigned hz:{30u,60u}){
   FightMatch game(std::span(&art,1),std::span(&rival,1),{},{AssisterChoice{&assist,0},AssisterChoice{}});const double y=stage.geometry().playerBottom;game.reset(stage,{350,y},{1000,y});game.setSourceProfile(hz,30,stage);
   std::array<FighterControls,2> controls{};controls[0].assistHeld=true;game.stepMainFrame(stage.geometry(),controls,Contacts::Training);
   FightAttacker* root=nullptr;for(const auto& o:game.objects())if(const auto* v=std::get_if<FightAttacker>(&o);v&&v->assister())root=const_cast<FightAttacker*>(v);EMIT_REQUIRE(root&&root->active());
   auto& actor=game.actor(0);const auto before=game.scene().shines.size();actor.present(FighterCommand(1));EMIT_REQUIRE(game.scene().shines.size()==before);actor.present(FighterCommand(3));EMIT_REQUIRE(!root->referenced(0,0));actor.present(FighterCommand(2));EMIT_REQUIRE(root->active());
   actor.present(FighterCommand(0));FightAttacker* child=nullptr;for(const auto& o:game.objects())if(const auto* v=std::get_if<FightAttacker>(&o);v&&!v->assister()&&v->key()==art.attacker(0).key)child=const_cast<FightAttacker*>(v);EMIT_REQUIRE(child&&child->active());
   actor.present(FighterCommand(1));EMIT_REQUIRE(game.scene().shines.size()==before+1);actor.present(FighterCommand(3));EMIT_REQUIRE(!root->referenced(0,0)&&child->referenced(0,0));actor.present(FighterCommand(2));EMIT_REQUIRE(root->active()&&!child->active()&&game.sourceScene().stats().rangeFaults==0&&game.sourceScene().paint().stats().faults==0);word(hz);word(root->active());word(child->active());++roots;
  }
  return true;
 }
 bool run(const std::string& folder){return reference(folder)&&scenes(folder)&&assister(folder)&&lookup(folder)&&rootAssist(folder);}
#undef EMIT_REQUIRE
};
}
