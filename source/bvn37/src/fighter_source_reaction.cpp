// Copyright (C) 2021-2026, 5DPLAY Game Studio
// Source hurt MovieClip state/pose bridge. Legacy explicit-clock callers keep
// their original flight adapter; actual source-profile actors use this path.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_motion.hpp"
#include "floor_effects.hpp"

namespace bvn {
SourceHurtTimeline::Input FighterMotion::sourceHurtInput()const{
    return {alive(),motion_.airborne,motion_.position.y,motion_.secondaryVelocity.x,motion_.secondaryVelocity.y,facing_};
}
void FighterMotion::SourceHurtView::pose(SourceHurtTimeline::Pose pose){
    using P=SourceHurtTimeline::Pose;
    MoveClip clip=MoveClip::Hurt;
    switch(pose){
    case P::Hurt:actor.flight_=actor.sourceHurt_.flying()?Flight::Start:Flight::None;break;
    case P::Fly:clip=MoveClip::HurtFly;actor.flight_=Flight::Air;break;
    case P::Fall:clip=MoveClip::HurtFall;actor.flight_=Flight::Contact;break;
    case P::Bounce:clip=MoveClip::HurtBounce;actor.flight_=Flight::Bounce;break;
    case P::Down:clip=MoveClip::HurtDown;actor.flight_=Flight::Down;break;
    case P::Recover:clip=MoveClip::HurtRecover;actor.flight_=Flight::Recover;break;
    case P::None:return;
    }
    actor.sourceReactionPose_=true;actor.enter(actor.asset().has(clip)?clip:MoveClip::Hurt);actor.sourceReactionPose_=false;
}
void FighterMotion::SourceHurtView::advance(){actor.advancePose();}
void FighterMotion::SourceHurtView::children(){
    // ExtractFighter.writeHeldCycle phase zero already includes the first
    // renderChildren call made by goFrame. Held parent poses then cycle once
    // on every MC renderAnimate, independent of its main-play flag.
    using P=SourceHurtTimeline::Pose;
    const auto pose=actor.sourceHurt_.pose();
    if(pose==P::Fly||pose==P::Bounce||pose==P::Down)actor.advancePose();
}
void FighterMotion::SourceHurtView::face(int facing){actor.facing_=facing;}
void FighterMotion::SourceHurtView::velocity(double x,double y){actor.motion_.setVelocity({x,y});}
void FighterMotion::SourceHurtView::velocityY(double y){actor.motion_.velocity.y=y;}
void FighterMotion::SourceHurtView::damping(double x,double y){actor.motion_.setDamping({x,y});}
void FighterMotion::SourceHurtView::impact(unsigned type,double power){appendFloorImpact(actor.effects_,FloorImpact(type),actor.motion_.position,power,actor.musouEnemyEffects_);}
void FighterMotion::SourceHurtView::touchFloor(){}
void FighterMotion::SourceHurtView::down(){actor.emitSourceEvent(SourceFighterEvent::HurtDown);}
void FighterMotion::SourceHurtView::dead(){actor.emitSourceEvent(SourceFighterEvent::Dead);}
void FighterMotion::SourceHurtView::recover(){actor.recover();actor.emitSourceEvent(SourceFighterEvent::HurtResume);}

void FighterMotion::emitSourceEvent(SourceFighterEvent event){
    if(!sourcePhysics_)return;
    ++sourceEvents_[unsigned(event)];
    if(sourceHooks_.event)sourceHooks_.event(sourceHooks_.context,sourceHooks_.id,event);
}

void FighterMotion::beginSourceHurt(const StrikeContact& contact){
    clearActions();shadowActive_=false;stopShake();hurtEscaping_=justHurtResume_=false;
    acting_=false;dodging_=crossBody_=guardReady_=guardHit_=false;motion_.applyGravity=true;
    const auto& strike=contact.strike;
    auto& random=sourceHurtRandom_?*sourceHurtRandom_:hurtRandomLocal_;
    const auto velocity=sourceHurtVelocity(strike.hitX,strike.hitY,contact.hasOwner,contact.facing,motion_.airborne,alive(),musouEnemyEffects_,[&]{return sourceRandomUnit(random);});
    const auto x=velocity.x,y=velocity.y;
    hitGap_=4;hittable_=false;hurtDownFrame_=0;
    SourceHurtView view{*this};
    if(strike.launches())sourceHurt_.launch(x,y,sourceHurtInput(),true,view);
    else{
        sourceHurt_.stun(std::max(4,int(strike.hurtFrames)),strike.hitType==11,view);
        motion_.setVelocity({x,y});motion_.setDamping({.1,.5});
    }
    emitSourceEvent(SourceFighterEvent::Hurt);
}
void FighterMotion::animateSourceHurt(){
    // goFrame from an ordinary authored action may retain a prior flight
    // machine. That action's established native animation still advances once.
    if(sourceHurt_.pose()==SourceHurtTimeline::Pose::None)advancePose();
    SourceHurtView view{*this};sourceHurt_.animate(sourceHurtInput(),view);
    hurtFrames_=unsigned(std::max(0,sourceHurt_.hold()));
}
void FighterMotion::die(){
    const auto state=aiState();health_=0;sourceAlive_=false;
    if(!sourcePhysics_)return;
    if(state==AiState::Hurt||state==AiState::Flying||state==AiState::Down||state==AiState::Bounce||state==AiState::Dead)return;
    SourceHurtView view{*this};sourceHurt_.down(state,view);
}
}
