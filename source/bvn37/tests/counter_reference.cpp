// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_motion.hpp"
#include <cstdio>
using namespace bvn;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    FighterAsset asset;
    if(!asset.open(argv[1],FighterAsset::TextureResidency::LowMemory))return 3;
    StageGeometry stage{0,4000,800,500,{}};
    const auto prepare=[&](FighterMotion& actor,unsigned faction,unsigned hz,unsigned damage,unsigned flags,double energy,double qi) {
        actor.reset({900,500},1);actor.setFaction(faction);actor.setGameHz(hz);
        FighterStrike hit{};hit.damage=damage;hit.flags=flags;hit.hurtFrames=200;
        if(!actor.receiveStrike({hit,-1,1100}))std::abort();
        actor.setEnergy(energy);actor.setQi(qi);
    };
    for(unsigned faction:{0U,1U,2U})for(unsigned hz:{30U,60U})
    for(double energy:{0.,49.99,50.,99.,100.})for(double qi:{99.99,100.,200.})
    for(unsigned damage:{0U,210U,211U})for(unsigned flags:{0U,2U,16U})for(bool requested:{false,true}) {
        FighterMotion actor(asset);prepare(actor,faction,hz,damage,flags,energy,qi);
        for(unsigned i=0;i<60/hz;++i)actor.step(stage,{.assistHeld=requested},false);
        std::printf("G %u %u %.17g %.17g %u %u %u %u %u %.17g %.17g\n",faction,hz,energy,qi,
            damage,flags,unsigned(requested),actor.counterStarts(),unsigned(actor.counterKind()),actor.energy(),actor.qi());
    }
    for(unsigned faction:{0U,1U,2U})for(unsigned hz:{30U,60U}) {
        FighterMotion actor(asset);prepare(actor,faction,hz,40,0,100,100);
        for(unsigned i=0;i<60/hz;++i)actor.step(stage,{.assistHeld=true},false);
        std::printf("R %u %u\n",faction,hz);
        unsigned tick=0;
        do {
            std::printf("P %u %u %u\n",tick,unsigned(actor.counterPaused()),actor.counterGameFrame());
            if(!actor.counterPaused())break;
            actor.stepCounterPause();++tick;
        }while(tick<100);
        for(unsigned frame=0;frame<=65;++frame) {
            std::printf("A %u %u %u %u %u %u\n",frame,actor.counterHitFrames(),actor.counterSteelFrames(),
                actor.counterReplaceFrames(),unsigned(actor.aiHittable()),unsigned(actor.steel()));
            actor.step(stage,{},true);
        }
    }
}
