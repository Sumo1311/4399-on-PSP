// Copyright (C) 2021-2026, 5DPLAY Game Studio
// Adapted from FighterMcHurtCtrl. Sprite/gauge/effect operations are supplied
// by the owning actor so the same branch order runs on host and PSP.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace bvn {
struct SourceHitInput {
    double damage=0,x=0,y=0;unsigned hurtMillis=0;
    int hitType=0,direction=1;
    bool launch=false,breaksGuard=false,bisha=false,weak=false,owner=true,ownerBase=true,ownerReverse=true;
    bool steel=false,superSteel=false,overload=false,guard=false,behind=false,gravity=true,rect=true,hurtAction=false;
    bool catchHit()const{return hitType==11&&breaksGuard;}
    double directedX()const{return x*(owner?direction:1);}
};
enum class SourceHitResult : unsigned {Ignored,Weak,Hurt,Guard,GuardBreak,Steel};
class SourceHitControl {
public:
    template<class View> static SourceHitResult receiveMain(const SourceHitInput& hit,double power,View& view){
        if(!view.hittable())return SourceHitResult::Ignored;
        const auto result=receive(hit,view);view.qi(std::min(20.,power*.08));
        if(view.state()==21||view.state()==22)view.cacheHit();
        return result;
    }
    static bool heavy(int type){return type==3||type==5||type==6||type==7||type==8||type==9;}
    template<class View> static SourceHitResult receive(const SourceHitInput& hit,View& view){
        if(hit.hurtAction){view.action();return SourceHitResult::Ignored;}
        if(view.touchingSide()&&hit.owner&&hit.ownerBase&&hit.ownerReverse&&std::abs(view.x()-view.ownerX())<100)
            view.reversePush(std::clamp(-hit.x*hit.direction*1.4,-20.,20.));
        if(hit.steel)return view.alive()?steel(hit,view):SourceHitResult::Ignored;
        if(hit.guard&&!hit.catchHit()&&!hit.behind)return guard(hit,view);
        return hurt(hit,view);
    }
    template<class View> static SourceHitResult hurt(const SourceHitInput& hit,View& view){
        if(hit.rect)view.effect(SourceHitResult::Hurt);
        view.hurtHit();view.loseHp(hit.damage);view.hittable(false);view.gap(4);
        if(view.alive()&&view.checkDie()){view.alive(false);view.dieEvent();}
        const bool reaction=!view.alive()||!hit.weak;
        if(reaction)view.hurtAnimate();
        if(view.alive()&&!hit.launch)view.gravity(hit.gravity);
        return reaction?SourceHitResult::Hurt:SourceHitResult::Weak;
    }
private:
    template<class View> static SourceHitResult steel(const SourceHitInput& hit,View& view){
        if(!hit.superSteel&&(hit.overload||hit.bisha||hit.catchHit()))return hurt(hit,view);
        view.hurtHit();view.loseHp(hit.damage*(hit.superSteel?.3:.65));
        if(view.alive()&&view.checkDie()){
            // Unlike doHurt, doSteelHurt dispatches DIE before clearing isAlive.
            view.dieEvent();view.alive(false);return hurt(hit,view);
        }
        view.gap(hit.launch?10:4);view.energy(hit.damage*(hit.superSteel?.2:hit.breaksGuard?1:.4));view.hittable(false);
        if(!hit.superSteel){const auto x=hit.directedX(),rate=hit.breaksGuard?2.:1.;view.secondary(x*rate,hit.y*rate,std::abs(x*.1),std::abs(hit.y*.1));}
        if(hit.rect)view.effect(SourceHitResult::Steel);
        return SourceHitResult::Steel;
    }
    template<class View> static SourceHitResult guard(const SourceHitInput& hit,View& view){
        view.loseHp(hit.damage*.05);
        if(view.alive()&&view.checkDie()){view.dieEvent();view.alive(false);return hurt(hit,view);}
        view.guardHit();
        const int cost=hit.breaksGuard?int(view.maxEnergy()*.9):std::min(50,int(hit.damage*.2));
        const bool breaks=view.energy()<cost;view.energy(cost);
        if(breaks){
            view.loseHp(hit.damage*.1);view.gap(hit.launch?10:4);view.hittable(false);view.overload(false);view.guard(false);
            view.breakAnimate(42);view.velocity(std::clamp(hit.x,5.,10.)*(hit.owner?hit.direction:1),0);view.damping(.1,0);
            if(hit.rect){view.effect(SourceHitResult::Guard);view.effect(SourceHitResult::GuardBreak);}
            return SourceHitResult::GuardBreak;
        }
        view.gap(hit.launch?8:4);view.hittable(false);view.guardHitting(true);
        view.guardHold(hit.launch?10:std::clamp(int(hit.hurtMillis*.001*view.hz()*.2),5,10));
        view.velocity(hit.directedX(),0);view.damping(1,0);
        if(hit.rect)view.effect(SourceHitResult::Guard);
        return SourceHitResult::Guard;
    }
};
inline double sourceRandomUnit(std::uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return double(state)/4294967296.;}
struct SourceHitVelocity {double x,y;};
template<class Random> SourceHitVelocity sourceHurtVelocity(double x,double y,bool owner,int direction,bool air,bool alive,bool minion,Random&& random){
    if(minion&&!alive){if(y>0)y+=random()*3;else y-=3+random()*3;x+=2+random()*3;}
    if(owner)x*=direction;
    if(air&&y<=0)y-=3;else if(!air&&y<0)y-=6;
    return {x,y};
}
}
