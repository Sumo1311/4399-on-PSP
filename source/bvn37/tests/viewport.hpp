// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_camera.hpp"
#include "fight_match.hpp"
#include "scene_integration.hpp"
#include <cstdio>
#include <string>
#include <vector>
namespace bvn::viewport_qa {
struct Checks {
 unsigned error=0,rows=0,faults=0,forced=0,scenes=0,failedRow=0,failedColumn=0;
 double actual=0,expected=0;
 std::uint64_t hash=14695981039346656037ull;
 void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(i*8))&255))*1099511628211ull;}
#define VIEW_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
 bool reference(const std::string& folder){
  FILE* f=std::fopen((folder+"CAMERA.IN").c_str(),"r");VIEW_REQUIRE(f);unsigned count;VIEW_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==16384);
  SourceCamera camera;std::array<Point,4> actors{};std::vector<unsigned> focus,base;
  const auto points=[&](){std::vector<Point> result;for(auto id:focus)result.push_back(actors[id]);return result;};
  for(unsigned row=0;row<count;++row){
   std::array<double,40> v{};for(auto& n:v)VIEW_REQUIRE(std::fscanf(f,"%lf",&n)==1);
   const unsigned hz=v[0],scenario=v[1],tick=v[2],op=v[6],target=v[7],amount=v[8];const double value=v[9];
   if(!tick){camera.initialize(v[3],v[4],v[5],hz);base={0};if(!(scenario%2))base.push_back(1);focus=base;camera.focus(focus.size());}
   for(unsigned i=0;i<4;++i)actors[i]={v[10+i*2],v[11+i*2]};
   bool valid=true;
   switch(op){
    case 1:focus={target};camera.focus(1);camera.setZoom(3.5);camera.tween=int(2.5/(30./hz));break;
    case 2:focus=base;camera.focus(focus.size());if(base.size()<2)camera.setZoom(2);camera.tween=int(2.5/(30./hz));break;
    case 3:base.clear();for(unsigned i=0;i<1+amount%3;++i)base.push_back(i);focus=base;camera.focus(focus.size());camera.setZoom(2);camera.tween=int(2.5/(30./hz));break;
    case 4:camera.setZoom(value);break;
    case 5:camera.setX(value*100-200);break;
    case 6:camera.setY(value*100-200);break;
    case 7:camera.setStageBounds({(double(amount)-1)*10,-1000.+target*50,v[3]+(value-2)*100,600.+amount*40});break;
    case 8:camera.tween=int((value-2)*2);break;
    case 9:camera.focusX=amount&1;camera.focusY=amount&2;camera.autoZoom=target&1;camera.offsetX=(value-2)*25;camera.offsetY=v[4]-v[5]+(value-2)*10;break;
    case 10:valid=camera.updateNow(points());forced+=valid;break;
    case 11:camera.tween=int(2.5*value);break;
    case 12:focus.clear();camera.move(actors[target]);break;
    case 13:focus.clear();camera.focus(0);break;
    case 14:focus.clear();for(unsigned i=0;i<amount;++i)focus.push_back(i);camera.focus(focus.size());break;
   }
   if(valid)valid=camera.render(points());
   faults+=!valid;
   if(unsigned(!valid)!=unsigned(v[18])){failedRow=row;failedColumn=18;actual=!valid;expected=v[18];VIEW_REQUIRE(false);}
   const auto got=camera.snapshot();
   for(unsigned i=0;i<got.size();++i){
    if(!(got[i]==v[19+i]||std::abs(got[i]-v[19+i])<1e-10||(std::isnan(got[i])&&std::isnan(v[19+i])))){failedRow=row;failedColumn=19+i;actual=got[i];expected=v[19+i];VIEW_REQUIRE(false);}
    word(std::isfinite(got[i])?unsigned(std::llround(got[i]*1000000)):0x7fc00000);
   }
   word(!valid);++rows;
  }
  std::fclose(f);VIEW_REQUIRE(faults==5560&&forced==728);return true;
 }
 struct SceneState {
  FightMatch* game=nullptr;std::array<Point,2> points{};CameraView view{};unsigned selected=0;
  static void stage(void* value){auto& state=*static_cast<SceneState*>(value);for(unsigned side=0;side<2;++side)state.points[side]=state.game->actor(side).motion().position;state.view=state.game->camera();state.selected=state.game->sourceControl()->animations().size();}
 };
 bool matches(const StageAsset& stage,const FighterAsset& art){
  for(unsigned hz:{30u,60u})for(unsigned last:{0u,1u})for(auto command:{FighterCommand::SuperUp,FighterCommand::SuperDown,FighterCommand::Transform}){
   FightMatch game(art,art);const double y=stage.geometry().playerBottom;game.reset(stage,{350,y},{950,y});game.setSourceProfile(hz,30,stage);
   if(last==0){game.sourceScene().removeStage(0);game.sourceScene().addStage(0);}
   for(unsigned i=0;i<2;++i){game.actor(i).setHealth(1000000);game.actor(i).setQi(300);game.actor(i).present(command);}
   SceneState state{&game};game.sourceFrameHooks({&state,SceneState::stage});
   bool selected=false,resumed=false;unsigned selectedRows=0;
   for(unsigned tick=0;tick<hz*15;++tick){
    const auto before=game.camera();game.stepMainFrame(stage.geometry(),{},Contacts::Locked);
    const bool single=state.selected&&command!=FighterCommand::SuperUp;
    const auto a=single?state.points[last]:state.points[0],b=single?a:state.points[1];
    const double dx=std::abs(a.x-b.x),dy=std::abs(a.y-b.y);
    double zoom=single?3.5:std::fmin(800/dx*.8,600/dy*.8);if(!single){if(zoom<1)zoom=1;if(zoom>2.5)zoom=2.5;}
    const double inv=1/zoom;
    double x=(a.x<b.x?a.x:b.x)+dx*.5-800*.5*inv;
    double yy=(a.y<b.y?a.y:b.y)+dy*.5-(600*.5*inv+stage.geometry().bottom-y);
    if(x<0){x=0;}if(x>stage.cameraWidth()-800*inv){x=stage.cameraWidth()-800*inv;}
    if(yy<-1000*zoom){yy=-1000*zoom;}if(yy>600-600*inv){yy=600-600*inv;}
    const double tween=int(2.5/(30./hz));
    const std::array<double,3> want{before.origin.x+(x-before.origin.x)/tween,before.origin.y+(yy-before.origin.y)/tween,before.zoom+(zoom-before.zoom)/tween};
    const std::array<double,3> got{state.view.origin.x,state.view.origin.y,state.view.zoom};
    for(unsigned i=0;i<3;++i){if(std::abs(got[i]-want[i])>1e-10){failedRow=tick;failedColumn=i;actual=got[i];expected=want[i];VIEW_REQUIRE(false);}word(unsigned(std::llround(got[i]*1000000)));}
    if(state.selected==2)selected=true;
    selectedRows+=bool(state.selected);
    if(selected&&!state.selected){resumed=true;break;}
    // This fixture supplies the base Ichigo asset only. It checks the
    // transformation focus while selected, not a completed form transition.
    if(command==FighterCommand::Transform&&selectedRows>=30)break;
   }
   VIEW_REQUIRE(selected&&(resumed||command==FighterCommand::Transform)&&selectedRows>=10&&game.sourceScene().stats().rangeFaults==0);word(hz);word(last);word(unsigned(command));++scenes;
  }
  return true;
 }
 bool musou(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
  MusouCampaign campaign;VIEW_REQUIRE(campaign.open((folder+"MUSLETH.BVM").c_str()));
  for(unsigned hz:{30u,60u})for(unsigned last:{0u,1u}){
   MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;scene_order_qa::Integration fixture;
   VIEW_REQUIRE(fixture.enter(game,campaign,stage,art,hz,1,progress,members));
   auto& player=const_cast<FighterMotion&>(game.member(game.leader()));auto& boss=const_cast<MusouOpponent&>(game.enemies().front());
   const double y=stage.geometry().playerBottom;player.reset({350,y},1);boss.motion.reset({950,y},-1);boss.brain={};boss.bossAI.reset();
   for(auto* actor:{&player,&boss.motion}){actor->setHealth(1000000);actor->setGaugeLimits(300,100);actor->setQi(300);}
   // Use the actual scene update and effect dispatch, reversing registration
   // order while preserving the normal actor timeline methods.
   if(!last){game.sourceScene().removeStage(game.leader());game.sourceScene().addStage(game.leader());}
   player.present(FighterCommand::SuperDown);boss.motion.present(FighterCommand::SuperDown);
   for(unsigned tick=0;tick<120&&game.sourceControl()->animations().size()!=2;++tick)game.step({},false);
   VIEW_REQUIRE(game.sourceControl()->animations().size()==2);
   unsigned checked=0;
   for(unsigned tick=0;tick<40&&checked<12;++tick){
    const auto before=game.camera();const auto roots=game.ticks();game.step({},false);if(game.ticks()==roots)continue;
    VIEW_REQUIRE(game.sourceControl()->animations().size()==2);
    const auto focus=last?boss.motion.motion().position:player.motion().position;const double inv=1/3.5,tween=int(2.5/(30./hz));
    const std::array<double,3> want{before.origin.x+(focus.x-800*.5*inv-before.origin.x)/tween,before.origin.y+(focus.y-(600*.5*inv+stage.geometry().bottom-y)-before.origin.y)/tween,before.zoom+(3.5-before.zoom)/tween};
    const auto view=game.camera();const std::array<double,3> got{view.origin.x,view.origin.y,view.zoom};
    for(unsigned i=0;i<3;++i){if(std::abs(got[i]-want[i])>1e-10){failedRow=tick;failedColumn=i;actual=got[i];expected=want[i];VIEW_REQUIRE(false);}word(unsigned(std::llround(got[i]*1000000)));}
    ++checked;
   }
   VIEW_REQUIRE(checked==12&&game.sourceScene().stats().rangeFaults==0);word(hz);word(last);++scenes;
  }
  return true;
 }
 bool run(const std::string& folder){StageAsset stage;FighterAsset art;VIEW_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));VIEW_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str()));return reference(folder)&&matches(stage,art)&&musou(folder,stage,art);}
#undef VIEW_REQUIRE
};
}
