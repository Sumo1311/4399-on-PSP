// Copyright (C) 2021-2026, 5DPLAY Game Studio
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_motion.hpp"
namespace bvn {
bool FighterMotion::SourceHitView::touchingSide()const{return actor.motion_.touchingSide;}
double FighterMotion::SourceHitView::x()const{return actor.motion_.position.x;}
double FighterMotion::SourceHitView::ownerX()const{return contact.originX;}
void FighterMotion::SourceHitView::reversePush(double x){
    if(contact.recoil.apply)contact.recoil.apply(contact.recoil.context,x);
    else if(attacker&&contact.ownerKind==StrikeOwnerKind::Fighter)attacker->sourceRecoil(x);
}
bool FighterMotion::SourceHitView::alive()const{return actor.sourceAlive_;}
void FighterMotion::SourceHitView::alive(bool value){actor.sourceAlive_=value;}
bool FighterMotion::SourceHitView::checkDie()const{return !actor.training_&&actor.health_<=0;}
void FighterMotion::SourceHitView::dieEvent(){++actor.sourceDieEvents_;if(actor.sourceHooks_.die)actor.sourceHooks_.die(actor.sourceHooks_.context,actor.sourceHooks_.id);}
void FighterMotion::SourceHitView::hurtHit(){actor.hurtStrike_=contact.strike.key;}
void FighterMotion::SourceHitView::guardHit(){actor.guardStrike_=contact.strike.key;}
void FighterMotion::SourceHitView::loseHp(double value){if(actor.allowLoseHp_)actor.health_=std::max(0.,actor.health_-std::max(0.,value*actor.defenseRate()-actor.flatDefense_));}
void FighterMotion::SourceHitView::energy(double value){actor.useEnergy(value);}
double FighterMotion::SourceHitView::energy()const{return actor.energy_;}
double FighterMotion::SourceHitView::maxEnergy()const{return actor.maxEnergy_;}
bool FighterMotion::SourceHitView::hittable()const{return actor.hittable_;}
int FighterMotion::SourceHitView::state()const{return int(actor.aiState());}
void FighterMotion::SourceHitView::qi(double value){actor.addQi(value);}
void FighterMotion::SourceHitView::cacheHit(){actor.rememberHurtHit(contact);}
void FighterMotion::SourceHitView::hittable(bool value){actor.hittable_=value;}
void FighterMotion::SourceHitView::gap(unsigned value){actor.hitGap_=value;}
void FighterMotion::SourceHitView::gravity(bool value){actor.motion_.applyGravity=value;}
void FighterMotion::SourceHitView::overload(bool value){actor.overloaded_=value;}
void FighterMotion::SourceHitView::guard(bool value){actor.guardReady_=value;}
void FighterMotion::SourceHitView::guardHitting(bool value){actor.guardHit_=value;}
void FighterMotion::SourceHitView::guardHold(unsigned value){actor.guardHold_=value;}
unsigned FighterMotion::SourceHitView::hz()const{return actor.gameHz_;}
void FighterMotion::SourceHitView::velocity(double x,double y){actor.motion_.setVelocity({x,y});}
void FighterMotion::SourceHitView::damping(double x,double y){actor.motion_.setDamping({x,y});}
void FighterMotion::SourceHitView::secondary(double x,double y,double dx,double dy){actor.motion_.setSecondaryVelocity({x,y},{dx,dy});}
void FighterMotion::SourceHitView::effect(SourceHitResult kind){
    ContactEffectKind mapped=ContactEffectKind::Hurt;
    if(kind==SourceHitResult::Guard)mapped=ContactEffectKind::Guard;
    else if(kind==SourceHitResult::GuardBreak)mapped=ContactEffectKind::GuardBreak;
    else if(kind==SourceHitResult::Steel)mapped=ContactEffectKind::Steel;
    const auto before=actor.effects_.contactEffects.size();actor.contactEffect(contact,mapped);
    if(kind==SourceHitResult::Hurt&&actor.effects_.contactEffects.size()!=before)actor.effects_.slowTimeMs=std::max(actor.effects_.slowTimeMs,unsigned(contact.strike.slowTimeMs));
    actor.flushSourceEffects();
}
void FighterMotion::SourceHitView::hurtAnimate(){actor.beginSourceHurt(contact);actor.flushSourceEffects();}
void FighterMotion::SourceHitView::breakAnimate(unsigned hold){
    actor.guardReady_=actor.guardHit_=false;actor.acting_=false;actor.clearActions();
    SourceHurtView view{actor};actor.sourceHurt_.stun(hold,false,view);
}
void FighterMotion::SourceHitView::action(){const bool defenseHitting=actor.guardHit_;actor.target_=attacker;actor.startAction(actor.hurtClip_);actor.target_=nullptr;actor.guardHit_=defenseHitting;actor.flushSourceEffects();}
void FighterMotion::flushSourceEffects(){if(sourcePhysics_&&sourceHooks_.effects&&!effects_.empty())sourceHooks_.effects(sourceHooks_.context,takeEffects());}
bool FighterMotion::receiveSourceStrike(const StrikeContact& contact,FighterMotion* attacker){
    SourceHitResult result=SourceHitResult::Ignored;
    const auto& strike=contact.strike;
    const bool weak=contact.hasOwner&&attacker&&attacker->sourceNonboss()&&!strike.launches()&&!SourceHitControl::heavy(strike.hitType);
    const bool behind=strike.checksFacing()&&contact.hasOwner&&contact.facing==facing_&&(motion_.position.x-contact.originX)*facing_>5;
    const SourceHitInput hit{double(contact.damage()),strike.hitX,strike.hitY,strike.hurtTimeMs,int(strike.hitType),contact.facing,
        strike.launches(),strike.breaksGuard(),strike.bisha(),weak,contact.hasOwner,contact.ownerKind!=StrikeOwnerKind::Bullet,contact.reversePush,
        steel_,superSteel_,overloaded_,guardReady_,behind,strike.targetGravity(),contact.hasHitRect,hurtClip_!=MoveClip::Missing};
    SourceHitView view{*this,contact,attacker};result=SourceHitControl::receiveMain(hit,contact.power(),view);
    return result==SourceHitResult::Weak||result==SourceHitResult::Hurt||result==SourceHitResult::GuardBreak;
}
}
