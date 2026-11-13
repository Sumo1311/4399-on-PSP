// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include <cmath>
namespace bvn::ghost_qa {
struct Checks {
    unsigned error=0,cases=0,starts=0,ends=0,shadowFrames=0;
    StageGeometry stage{0,4000,2000,1000,{}};
#define GHOST_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
    bool exercise(const FighterAsset& asset,unsigned scenario,unsigned hz,int direction,bool cpu) {
        FighterMotion actor(asset),target(asset);
        const auto kind=scenario==0?GhostKind::Step:scenario==3?GhostKind::Down:GhostKind::Jump;
        actor.reset({1700,scenario>=2?100.:1000.},-direction);actor.startTraining();actor.setGameHz(hz);
        target.reset({1700.+direction*350,1000},-direction);
        actor.setQi(100);actor.setEnergy(100);
        FighterControls input{};
        if(cpu){input.aiControlled=true;input.aiIntents=1U<<unsigned(kind==GhostKind::Step?AiIntent::GhostStep:kind==GhostKind::Jump?AiIntent::GhostJump:AiIntent::GhostJumpDown);}
        else{input.dashPressed=true;input.vertical=kind==GhostKind::Jump?1:-1;}
        for(unsigned tick=0;tick<60/hz&&!actor.ghostActive();++tick){
            actor.step(stage,input,false,{},&target);input.dashPressed=false;
        }
        GHOST_REQUIRE(actor.ghostActive()&&actor.ghostKind()==kind&&actor.ghostStarts()==1);
        GHOST_REQUIRE(actor.qi()==40&&actor.facing()==direction&&actor.ghostFrames()==12);
        // Original walking frame scripts can replace the ghost's blue tint
        // (e.g. bankai Byakuya uses 255,51,153 on entry to MOVE).
        GHOST_REQUIRE(actor.shadowing()&&!actor.aiHittable()&&actor.hurtBody().empty());
        if(kind!=GhostKind::Step)GHOST_REQUIRE(actor.blueShadow());
        GHOST_REQUIRE(actor.energy()==(kind==GhostKind::Jump&&!cpu?0:20));
        GHOST_REQUIRE(actor.overloaded()==(kind==GhostKind::Jump&&!cpu&&scenario==2));
        const auto event=actor.takeTrainingInput();GHOST_REQUIRE(event&&event->ghost==unsigned(kind));
        const auto effect=actor.takeEffects();GHOST_REQUIRE(effect.ghostStepSound==1);
        FightEffects effects;
        unsigned animations=0;
        for(unsigned tick=0;tick<100&&actor.ghostActive();++tick){
            const bool animate=tick%2==0;
            const std::array source{ShadowSource{&actor,&actor.asset(),&actor.pose(),actor.motion().position,actor.facing(),actor.shadowing(),actor.shadowTint()}};
            effects.step(animate,source);
            shadowFrames+=!effects.scene().shadows.empty();
            actor.stepCutin(&target,animate,stage);if(animate){++animations;actor.finishGhostAnimation(&target);}
        }
        GHOST_REQUIRE(!actor.ghostActive()&&!actor.blueShadow()&&actor.cutin().kind==CutinKind::None);
        GHOST_REQUIRE(actor.ghostFrames()==-1&&animations==13&&actor.ghostEnds()==1);
        GHOST_REQUIRE(actor.aiHittable()&&actor.qi()==40);
        if(kind==GhostKind::Jump){
            GHOST_REQUIRE(actor.remainingJumps()==(!cpu&&scenario==2?-1:1));
            GHOST_REQUIRE(std::abs(actor.motion().damping.y+actor.motion().velocity.y/10)<1e-8);
        }
        if(kind==GhostKind::Step)GHOST_REQUIRE((actor.motion().position.x-1700)*direction>90);
        const std::array stopped{ShadowSource{&actor,&actor.asset(),&actor.pose(),actor.motion().position,actor.facing(),false}};
        for(unsigned i=0;i<16;++i)effects.step(true,stopped);
        GHOST_REQUIRE(effects.scene().shadows.empty());
        ++cases;++starts;++ends;return true;
    }
    bool gates(std::span<const FighterAsset> forms) {
        const auto& asset=forms[0];
        for(double qi:{59.99,60.})for(double energy:{0.,79.99,80.}){
            FighterMotion actor(asset);actor.reset({1700,1000},1);actor.setQi(qi);actor.setEnergy(energy);
            actor.step(stage,{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostStep)},false);
            GHOST_REQUIRE(actor.ghostActive()==(qi>=60));
            if(qi>=60){GHOST_REQUIRE(actor.energy()==0&&actor.overloaded()==(energy<80));++starts;}
            ++cases;
        }
        for(const auto command:{FighterCommand::Super,FighterCommand::SuperDown}){
            FighterMotion actor(asset);actor.reset({1700,1000},1);actor.present(command);actor.setQi(300);
            actor.step(stage,{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostStep)},false);
            GHOST_REQUIRE(!actor.ghostActive());++cases;
        }
        {
            FighterMotion actor(forms);actor.reset({1700,1000},1);actor.setQi(300);
            actor.step(stage,{.transformHeld=true},false);
            GHOST_REQUIRE(actor.transforming());
            actor.setQi(300);
            actor.step(stage,{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostStep)},false);
            GHOST_REQUIRE(!actor.ghostActive());++cases;
        }
        FighterMotion actor(asset);actor.reset({1700,1000},1);actor.setQi(300);actor.setEnergy(20);
        auto request=FighterControls{.aiControlled=true,.aiIntents=(1U<<unsigned(AiIntent::GhostStep))|(1U<<unsigned(AiIntent::GhostJump))};
        actor.step(stage,request,false);GHOST_REQUIRE(actor.ghostStarts()==1&&actor.ghostKind()==GhostKind::Step&&actor.overloaded());
        for(unsigned frame=0;actor.ghostActive();++frame){actor.stepCutin(nullptr,true,stage);actor.finishGhostAnimation(nullptr);GHOST_REQUIRE(frame<14);}
        actor.step(stage,request,false);GHOST_REQUIRE(actor.ghostStarts()==1);++cases;++starts;++ends;
        actor.reset({1700,1000},1);actor.setQi(120);actor.setEnergy(100);
        actor.step(stage,request,false);GHOST_REQUIRE(actor.ghostStarts()==2&&actor.ghostKind()==GhostKind::Jump&&actor.qi()==0&&actor.overloaded());
        GHOST_REQUIRE(actor.takeEffects().ghostStepSound==2);++cases;starts+=2;
        // A descending ghost ends at y > playerBottom, not at a platform or the stale contact flag.
        actor.reset({1700,990},1);actor.setQi(60);
        actor.step(stage,{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostJumpDown)},false);
        // Motion classifies the near-bottom point as grounded before controls, so no air ghost is requested.
        GHOST_REQUIRE(!actor.ghostActive());++cases;
        actor.reset({1700,100},1);actor.setQi(60);
        for(unsigned tick=0;tick<24;++tick)actor.step(stage,{},false);
        actor.place({1700,981.8});
        actor.step(stage,{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostJumpDown)},false);
        GHOST_REQUIRE(actor.ghostActive());
        actor.stepCutin(nullptr,true,stage);
        GHOST_REQUIRE(actor.motion().position.y>stage.playerBottom&&!actor.motion().touchingBottom);
        actor.finishGhostAnimation(nullptr);
        GHOST_REQUIRE(!actor.ghostActive()&&actor.ghostFrames()==11&&actor.ghostEnds()==1);++cases;++starts;++ends;
        const std::array platform{Floor{1600,1800,500}};
        auto elevated=stage;elevated.floors=platform;
        actor.reset({1700,400},1);actor.setQi(60);
        actor.step(elevated,{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostJumpDown)},false);
        for(unsigned frame=0;frame<12;++frame){
            actor.stepCutin(nullptr,true,elevated);actor.finishGhostAnimation(nullptr);GHOST_REQUIRE(actor.ghostActive());
        }
        GHOST_REQUIRE(actor.ghostFrames()==0&&actor.motion().position.y>500);
        actor.place({1700,999});actor.stepCutin(nullptr,true,elevated);actor.finishGhostAnimation(nullptr);
        GHOST_REQUIRE(!actor.ghostActive()&&actor.ghostEnds()==2);++cases;++starts;ends+=2;
        actor.reset({1700,1000},1);actor.setQi(300);
        FighterStrike hit{};hit.damage=20;hit.hurtFrames=30;
        GHOST_REQUIRE(actor.receiveStrike({hit,1,1300}));
        actor.step(stage,{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostStep)},false);
        GHOST_REQUIRE(!actor.ghostActive());++cases;
        return true;
    }
#undef GHOST_REQUIRE
};
}
