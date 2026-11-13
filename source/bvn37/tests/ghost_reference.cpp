// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_motion.hpp"
#include <cstdio>
using namespace bvn;
int main(int argc,char** argv){
 if(argc!=2)return 2;FighterAsset asset;if(!asset.open(argv[1],FighterAsset::TextureResidency::LowMemory))return 3;
 StageGeometry stage{0,4000,2000,1000,{}};
 const auto input=[](unsigned kind){return FighterControls{.aiControlled=true,.aiIntents=1U<<unsigned(kind==1?AiIntent::GhostStep:kind==2?AiIntent::GhostJump:AiIntent::GhostJumpDown)};};
 for(unsigned kind:{1U,2U,3U})for(double qi:{0.,59.99,60.,120.})for(double energy:{0.,79.99,80.,100.})for(bool overloaded:{false,true}){
  FighterMotion actor(asset);actor.reset({1700,1000},1);
  if(overloaded){actor.setQi(300);actor.setEnergy(0);actor.step(stage,input(1),false);
   for(unsigned frame=0;frame<13;++frame){actor.stepCutin(nullptr,true,stage);actor.finishGhostAnimation(nullptr);}}
  actor.reset({1700,kind==3?100.:1000.},1,true);actor.setQi(qi);actor.setEnergy(energy);
  actor.step(stage,input(kind),false);
  std::printf("G %u %.17g %.17g %u %u %u %.17g %.17g %u\n",kind,qi,energy,unsigned(overloaded),actor.ghostStarts(),
   unsigned(actor.ghostActive()),actor.qi(),actor.energy(),unsigned(actor.overloaded()));
 }
 for(unsigned kind:{1U,2U,3U})for(unsigned hz:{30U,60U})for(unsigned low:{0U,1U}){
  FighterMotion actor(asset);actor.reset({1700,kind==3?(low?970.:100.):1000.},1);actor.setQi(60);actor.setGameHz(hz);
  for(unsigned tick=0;tick<60/hz;++tick)actor.step(stage,input(kind),false);
  std::printf("R %u %u %u\n",kind,hz,low);
  for(unsigned frame=0;frame<13&&actor.ghostActive();++frame){
   actor.stepCutin(nullptr,true,stage);const auto before=actor.motion();actor.finishGhostAnimation(nullptr);const auto& after=actor.motion();
   std::printf("F %u %.17g %.17g %.17g %.17g %.17g %u %u %d %u %.17g %.17g %.17g %.17g\n",frame,
    before.position.y,before.velocity.x,before.velocity.y,before.damping.x,before.damping.y,unsigned(before.airborne),
    unsigned(actor.ghostActive()),actor.ghostFrames(),actor.ghostEnds(),after.velocity.x,after.velocity.y,after.damping.x,after.damping.y);
  }
 }
 FightEffects effects;
 for(unsigned frame=0;frame<80;++frame){
  const bool active=frame<12||(frame>=14&&frame<22)||(frame>=30&&frame<35)||(frame>=55&&frame<65),animate=frame%5!=2;
  const std::array source{ShadowSource{&asset,&asset,&asset.frame(MoveClip::Walk,0).pose,{double(frame*3),1000},1,active}};
  effects.step(animate,source);const auto shadows=effects.scene().shadows;
  std::printf("S %u %u %u %zu",frame,unsigned(active),unsigned(animate),shadows.size());
  for(const auto& shadow:shadows)std::printf(" %.17g %.17g",shadow.position.x,shadow.alpha);std::putchar('\n');
 }
}
