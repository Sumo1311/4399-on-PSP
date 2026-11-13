// Copyright (C) 2021-2026, 5DPLAY Game Studio
// Adapted from QuickTransUI's two 0.1-second TweenLite calls.
// Pinned TweenLite 12.1.5 default Ease(type=1,power=1): 1-(1-p)^2.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
namespace bvn {
class SourceQuickTransition {
public:
    void begin(double milliseconds){stage_=1;start_=milliseconds;up_=-300;down_=600;}
    void render(double milliseconds){
        if(!stage_)return;
        const auto progress=std::clamp((milliseconds-start_)/100.,0.,1.);
        const auto ratio=1-(1-progress)*(1-progress);
        if(stage_==1){up_=-300+300*ratio;down_=600-300*ratio;}
        else{up_=-300*ratio;down_=300+300*ratio;}
        if(progress>=1){if(stage_==1){stage_=2;start_=milliseconds;}else stage_=0;}
    }
    bool active()const{return stage_!=0;}unsigned stage()const{return stage_;}
    double up()const{return up_;}double down()const{return down_;}
private:unsigned stage_=0;double start_=0,up_=-300,down_=600;
};
}
