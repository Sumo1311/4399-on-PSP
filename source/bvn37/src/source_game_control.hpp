// Adapted from GameCtrl and EffectBlackBackHandler, 5DPLAY Game Studio.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "stage_motion.hpp"
#include <algorithm>
#include <memory>
#include <vector>

namespace bvn {
using SourceControlTarget = std::shared_ptr<const void>;
enum class SourceControlOp { Bisha, SuperBisha, WanKai, EndBisha, EndWanKai, Ghost, EndGhost, Replace, Explode };
struct SourceControlRequest { SourceControlOp op; SourceControlTarget target; Point origin{}; };
struct SourceControlChange {
    bool mapChanged=false, mapVisible=true, cameraFocus=false, cameraResume=false;
};
struct SourceCounterState {
    unsigned replaceFrame=0,replaceHold=0,explodeFrame=0,explodeLength=0;
    Point replaceOrigin{},explodeOrigin{};
    SourceControlTarget replaceTarget{},explodeTarget{};
};
struct SourceCounterStep {
    bool replaceSmoke=false,explodeBurst=false;
    Point replaceOrigin{},explodeOrigin{};
    SourceControlChange change{};
};

// GameCtrl owns one running flag. A resume from one handler can override a
// pause from another handler; the original does not count independent reasons.
class SourceGameControl {
public:
    void initialize(unsigned hz){*this=SourceGameControl{};gameHz_=hz;}
    void pause(){running_=false;}
    void resume(){running_=true;}
    bool running()const{return running_;}
    bool renderHit()const{return renderHit_;}
    const std::vector<SourceControlTarget>& animations()const{return animations_;}
    const std::vector<SourceControlTarget>& renders()const{return renders_;}
    const SourceCounterState& counters()const{return counters_;}
    SourceCounterStep renderCounters(){
        SourceCounterStep step;
        if(counters_.replaceHold>0){
            ++counters_.replaceFrame;
            if(counters_.replaceFrame==1){step.replaceSmoke=true;step.replaceOrigin=counters_.replaceOrigin;}
            if(counters_.replaceFrame>counters_.replaceHold){
                resume();step.change={true,true,false,false};counters_.replaceHold=0;
            }
        }
        if(counters_.explodeFrame>0&&--counters_.explodeFrame==0){
            step.explodeBurst=true;step.explodeOrigin=counters_.explodeOrigin;
            resume();step.change={true,true,false,false};
        }
        return step;
    }
    SourceControlChange apply(const SourceControlRequest& request){
        SourceControlChange change;
        const auto add=[](auto& list,const auto& value){
            if(std::find(list.begin(),list.end(),value)==list.end())list.push_back(value);
        };
        const auto cancel=[](auto& list,const auto& value){
            if(const auto found=std::find(list.begin(),list.end(),value);found!=list.end())list.erase(found);
            return list.empty();
        };
        switch(request.op){
        case SourceControlOp::Bisha:
        case SourceControlOp::SuperBisha:
        case SourceControlOp::WanKai:
            add(animations_,request.target);pause();renderHit_=false;
            change={true,false,request.op!=SourceControlOp::Bisha,false};
            break;
        case SourceControlOp::EndBisha:
        case SourceControlOp::EndWanKai:
            if(cancel(animations_,request.target)){
                resume();renderHit_=true;change={true,true,false,true};
            }
            break;
        case SourceControlOp::Ghost:
            add(renders_,request.target);add(animations_,request.target);pause();
            change={true,false,false,false};
            break;
        case SourceControlOp::EndGhost:{
            const bool first=cancel(renders_,request.target),second=cancel(animations_,request.target);
            if(first&&second){resume();change={true,true,false,false};}
            break;
        }
        case SourceControlOp::Replace:
            pause();change={true,false,false,false};
            counters_.replaceFrame=0;counters_.replaceHold=gameHz_;
            counters_.replaceOrigin=request.origin;counters_.replaceTarget=request.target;
            break;
        case SourceControlOp::Explode:
            pause();change={true,false,false,false};
            counters_.explodeFrame=counters_.explodeLength=unsigned(.7*gameHz_);
            counters_.explodeOrigin=request.origin;counters_.explodeTarget=request.target;
            break;
        }
        return change;
    }
private:
    bool running_=true,renderHit_=true;
    unsigned gameHz_=60;
    SourceCounterState counters_{};
    std::vector<SourceControlTarget> animations_,renders_;
};
} // namespace bvn
