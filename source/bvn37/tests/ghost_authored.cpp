// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_match.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
using namespace bvn;
int main(int argc,char** argv){
 if(argc!=2)return 2;const std::filesystem::path p=argv[1];StageAsset stage;assert(stage.open((p/"JIANCHANG.BVN").c_str()));
 FighterAsset rival;assert(rival.open((p/"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
 for(const char* name:{"HINATA.BVF","TENSA.BVF"})for(unsigned side:{0U,1U}){
  FighterAsset asset;assert(asset.open((p/name).c_str(),FighterAsset::TextureResidency::LowMemory));
  FightMatch match(side?rival:asset,side?asset:rival);const auto floor=stage.geometry();
  match.reset(stage,{450,floor.playerBottom},{500,floor.playerBottom});auto& actor=match.actor(side);actor.setQi(300);
  unsigned ghostTicks=0,shadowTicks=0;bool resumed=false;
  for(unsigned tick=0;tick<850;++tick){
   std::array<FighterControls,2> input{};
   if(tick==2){if(name[0]=='H')input[side]={.vertical=1,.specialPressed=true};else input[side]={.vertical=-1,.superPressed=true};}
   const bool wasGhost=actor.cutin().kind==CutinKind::GhostStep;
   const auto targetPosition=match.actor(1-side).motion().position;
   match.step(floor,input,tick%2==0,Contacts::Round);
   if(actor.cutin().kind==CutinKind::GhostStep){
    ++ghostTicks;
    // Hinata frame 536 explicitly ends its shadow after ghostStep; Tensa
    // frame 1376 ends that blue shadow then starts a white one.
    if(name[0]=='H')assert(!actor.shadowing());
    else assert(actor.shadowing()&&(actor.shadowTint()==ShadowTint{255,255,255}));
    assert(actor.ghostStarts()==0);
    if(wasGhost){assert(match.actor(1-side).motion().position.x==targetPosition.x);assert(match.actor(1-side).motion().position.y==targetPosition.y);}
   }else if(ghostTicks){resumed=true;assert(!actor.blueShadow());}
   shadowTicks+=!match.scene().shadows.empty();
  }
  assert(ghostTicks>0&&resumed&&!actor.shadowing()&&match.scene().shadows.empty());
  assert(name[0]=='H'?shadowTicks==0:shadowTicks>0);
  std::printf("PASS authored %s side=%u ghostTicks=%u shadowTicks=%u resumed without controller timer\n",name,side,ghostTicks,shadowTicks);
 }
}
