// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scene_integration.hpp"
#include <cstdio>
namespace bvn::paint_qa {
struct Checks {
 unsigned error=0,rows=0,faults=0,changes=0,matches=0,switches=0,layers=0,failedRow=0,failedColumn=0;
 std::uint64_t actual=0,expected=0,hash=14695981039346656037ull;
 void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(i*8))&255))*1099511628211ull;}
#define PAINT_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
 bool reference(const std::string& folder){
  FILE* f=std::fopen((folder+"PAINT.IN").c_str(),"r");PAINT_REQUIRE(f);unsigned count;PAINT_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==8192);SourceScene scene;
  using P=SourcePaint;
  for(unsigned row=0;row<count;++row){
   std::array<unsigned,7> v{};for(auto& n:v)PAINT_REQUIRE(std::fscanf(f,"%u",&n)==1);
   if(!v[1]){scene.reset();}auto& paint=scene.paint();const auto before=std::vector<P::Key>(paint.players().begin(),paint.players().end());const unsigned faultsBefore=paint.stats().faults;
   const auto sprite=P::key(P::Kind::Sprite,v[3]),other=P::key(P::Kind::Sprite,v[4]),combat=P::key(P::Kind::Combat,v[3]%8);
   switch(v[2]){
    case 0:scene.addStage(v[3]);break;
    case 1:scene.addStage(v[3],SourceScene::Missing,v[5]);break;
    case 2:scene.removeStage(v[3]);break;
    case 3:paint.hit(sprite,other);break;
    case 4:paint.add(P::Layer::Players,combat);break;
    case 5:paint.add(P::Layer::Stage,combat,0);break;
    case 6:paint.remove(combat);break;
    case 7:paint.add(P::Layer::Stage,P::key(P::Kind::BlackBack),0);break;
    case 8:paint.remove(P::key(P::Kind::BlackBack));break;
    case 9:paint.add(P::Layer::Players,P::key(P::Kind::Shadow,v[3]%4),0);break;
    case 10:paint.setIndex(combat,v[5]);break;
    case 11:paint.swap(combat,other);break;
    case 12:paint.setIndex(sprite,0);break;
    case 13:paint.swap(sprite,other);break;
    case 14:paint.add(P::Layer::Players,P::key(P::Kind::Ground,v[3]%4));break;
    case 15:paint.add(P::Layer::Players,P::key(P::Kind::Reaction,v[3]%4));break;
   }
   const bool fault=paint.stats().faults!=faultsBefore;PAINT_REQUIRE(fault==bool(v[6]));faults+=fault;word(fault);
   changes+=!std::ranges::equal(before,paint.players());
   const std::vector<P::Key> logical(scene.displays().begin(),scene.displays().end());
   unsigned column=0;
   for(auto got:{std::span<const P::Key>(logical),paint.players(),paint.stage()}){
    unsigned length;PAINT_REQUIRE(std::fscanf(f,"%u",&length)==1);if(got.size()!=length){failedRow=row;failedColumn=column;actual=got.size();expected=length;PAINT_REQUIRE(false);}word(length);++column;
    for(auto key:got){unsigned long long value;PAINT_REQUIRE(std::fscanf(f,"%llu",&value)==1);if(key!=value){failedRow=row;failedColumn=column;actual=key;expected=value;PAINT_REQUIRE(false);}word(unsigned(key));word(unsigned(key>>32));++column;}
   }
   ++rows;
  }
  std::fclose(f);PAINT_REQUIRE(faults==1514&&changes==3432);return true;
 }
 bool hitCases(const StageAsset& stage,const FighterAsset& art){
  using P=SourcePaint;
  for(unsigned hz:{30u,60u})for(unsigned first:{0u,1u}){
   FightMatch game(art,art);game.reset(stage,{400,stage.geometry().playerBottom},{440,stage.geometry().playerBottom});game.setSourceProfile(hz,30,stage);
   for(unsigned turn=0;turn<2;++turn){
    for(unsigned i=0;i<2;++i)game.actor(i).reset({400.+i*40,stage.geometry().playerBottom},i?-1:1);
    const unsigned side=turn?1-first:first;game.actor(side).present(FighterCommand::Attack);const double hp=game.actor(1-side).health();bool hit=false;
    for(unsigned frame=0;frame<hz*4;++frame){game.stepMainFrame(stage.geometry(),{},Contacts::Round);if(game.actor(1-side).health()<hp){hit=true;break;}}
    PAINT_REQUIRE(hit&&game.sourceScene().displays().size()==2&&game.sourceScene().displays()[0]==0&&game.sourceScene().displays()[1]==1);
    const auto& paint=game.sourceScene().paint();PAINT_REQUIRE(paint.index(P::key(P::Kind::Sprite,side))>paint.index(P::key(P::Kind::Sprite,1-side))&&paint.stats().faults==0);
    word(unsigned(paint.hash()));word(unsigned(paint.hash()>>32));
   }
   ++matches;
  }
  return true;
 }
 bool switchCases(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
  MusouCampaign campaign;PAINT_REQUIRE(campaign.open((folder+"MUSMIN.BVM").c_str()));
  for(unsigned hz:{30u,60u}){
   MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;scene_order_qa::Integration fixture;
   PAINT_REQUIRE(fixture.enter(game,campaign,stage,art,hz,6,progress,members));const auto previous=game.leader();
   for(unsigned frame=0;frame<12&&game.leader()==previous;++frame)game.step({},true);
   PAINT_REQUIRE(game.leader()!=previous);const auto& paint=game.sourceScene().paint();const auto lead=SourcePaint::key(SourcePaint::Kind::Sprite,game.leader());
   PAINT_REQUIRE(paint.index(SourcePaint::key(SourcePaint::Kind::Sprite,previous))==SourcePaint::End);
   for(const auto& enemy:game.enemies())PAINT_REQUIRE(paint.index(lead)>paint.index(SourcePaint::key(SourcePaint::Kind::Sprite,enemy.id)));
   PAINT_REQUIRE(paint.stats().faults==0);word(unsigned(paint.hash()));word(unsigned(paint.hash()>>32));++switches;
  }
  return true;
 }
 bool coffinCases(const StageAsset& stage,const FighterAsset& art,const FighterAsset& aizen){
  using P=SourcePaint;
  for(unsigned hz:{30u,60u})for(unsigned side:{0u,1u}){
   FightMatch game(side?art:aizen,side?aizen:art);const double y=stage.geometry().playerBottom;game.reset(stage,{400,y},{540,y});game.setSourceProfile(hz,30,stage);
   game.actor(side).setQi(300);game.actor(side).present(FighterCommand::SuperDown);
   bool behind=false,swapped=false,removed=false;unsigned boxKey=SourceScene::Missing;
   for(unsigned frame=0;frame<hz*30;++frame){
    game.stepMainFrame(stage.geometry(),{},Contacts::Round);
    const auto& scene=game.sourceScene();const auto& paint=scene.paint();
    for(auto key:scene.displays())if(key&SourceScene::Object){const auto index=scene.storage(key);if(index>=game.objects().size())continue;
     const auto* actor=std::get_if<FightAttacker>(&game.objects()[index]);if(!actor||&actor->definition()!=&aizen.attacker(8))continue;
     boxKey=key;const auto self=P::key(P::Kind::Sprite,key);
     if(actor->behindFighters()&&!actor->swapsTargetLayer()){PAINT_REQUIRE(paint.index(self)==0);behind=true;}
     if(actor->swapsTargetLayer()){PAINT_REQUIRE(paint.index(P::key(P::Kind::Sprite,1-side))<paint.index(self));swapped=true;}
    }
    PAINT_REQUIRE(paint.stats().faults==0);
    if(swapped&&boxKey!=SourceScene::Missing&&!scene.registered(boxKey)){removed=true;break;}
   }
   PAINT_REQUIRE(behind&&swapped&&removed);
   const auto& paint=game.sourceScene().paint();PAINT_REQUIRE(paint.index(P::key(P::Kind::Sprite,1-side))<paint.index(P::key(P::Kind::Sprite,side)));
   word(unsigned(paint.hash()));word(unsigned(paint.hash()>>32));++layers;
  }
  return true;
 }
 bool run(const std::string& folder){StageAsset stage;FighterAsset art,aizen;PAINT_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));PAINT_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str()));PAINT_REQUIRE(aizen.open((folder+"AIZEN.BVF").c_str()));return reference(folder)&&hitCases(stage,art)&&switchCases(folder,stage,art)&&coffinCases(stage,art,aizen);}
#undef PAINT_REQUIRE
};
}
