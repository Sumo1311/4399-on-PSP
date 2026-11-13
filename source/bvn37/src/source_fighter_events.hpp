// Adapted from FighterEvent and EffectCtrl.renderRemoveEnemy.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
namespace bvn {
enum class SourceFighterEvent : unsigned { Idle,Hurt,HurtResume,HurtDown,Dead,Defense,Count };
class SourceEnemyFade {
public:
    void start(double alpha){active_=true;alpha_=std::clamp(alpha,0.,1.);}
    // The source tests alpha before subtracting. A zero-alpha display stays
    // registered until its following effect-animation call.
    bool render(){
        if(!active_)return false;
        if(alpha_>0){alpha_=std::clamp(alpha_-.05,0.,1.);return false;}
        active_=false;return true;
    }
    bool active()const{return active_;}
    template<class Fighter> bool render(Fighter& fighter){
        if(!active_)return false;
        alpha_=fighter.displayAlpha();
        if(alpha_>0){fighter.displayAlpha(alpha_-.05);alpha_=fighter.displayAlpha();return false;}
        active_=false;return true;
    }
    double alpha()const{return alpha_;}
private:
    double alpha_=1;
    bool active_=false;
};
}
