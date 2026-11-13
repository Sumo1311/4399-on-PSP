// Adapted from GameCtrl and EffectSlowHandler's independent animation phases.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>

namespace bvn {
class AnimationPhase {
public:
    void rate(unsigned gameHz,double animationHz,bool reset){
        gap_=int(std::ceil(gameHz/animationHz))-1;if(reset)frame_=0;
    }
    bool advance(){
        if(gap_>0){if(frame_++>=gap_){frame_=0;return true;}return false;}
        return true;
    }
    int gap()const{return gap_;}
    int frame()const{return frame_;}
private:
    int gap_=0,frame_=0;
};
struct GameAnimationStep {bool actor=false,effect=false;};
class GameAnimationClocks {
public:
    void initialize(unsigned gameHz){gameHz_=gameHz;slowRate_=0;slowFrames_=0;actor_.rate(gameHz_,30,true);effect_.rate(gameHz_,30,true);}
    bool slow(double rate,int milliseconds){
        if(slowRate_>rate)return false;
        slowRate_=rate;actor_.rate(gameHz_,30/rate,true);
        // EffectSlowHandler.slowDown changes its gap without resetting phase.
        effect_.rate(gameHz_,30/rate,false);
        slowFrames_=milliseconds?int(milliseconds*.001*gameHz_):0;return true;
    }
    void resume(){slowRate_=0;slowFrames_=0;actor_.rate(gameHz_,30,true);effect_.rate(gameHz_,30,true);}
    // EffectCtrl runs first; its callbacks may resume the main loop before
    // GameCtrl decides whether to advance the independent actor phase.
    bool renderEffect(){
        if(slowFrames_>0&&--slowFrames_<=0)resume();
        return effect_.advance();
    }
    bool renderActor(bool logicalRunning){return logicalRunning&&actor_.advance();}
    // One original logical game callback; display-rate normalization is external.
    GameAnimationStep render(bool logicalRunning,bool manuallyPaused){
        GameAnimationStep result;
        if(manuallyPaused)return result;
        result.effect=renderEffect();
        result.actor=renderActor(logicalRunning);
        return result;
    }
    const AnimationPhase& actor()const{return actor_;}
    const AnimationPhase& effect()const{return effect_;}
    double slowRate()const{return slowRate_;}
    int slowFrames()const{return slowFrames_;}
private:
    AnimationPhase actor_,effect_;
    unsigned gameHz_=60;
    double slowRate_=0;
    int slowFrames_=0;
};

// The PSP presents at 60 Hz. Deliver each original 30/60-Hz controller
// callback once, independently of whether that callback advances a clip.
// This adapts animation clocks only; sprite physics delivery is separate.
struct AnimationClockView {
    unsigned enabled=0,sourceFrame=0,displayFrames=0,sourceFrames=0,actorFrames=0,effectFrames=0;
    int actorGap=0,actorPhase=0,effectGap=0,effectPhase=0;
    float slowRate=0;
    int slowFrames=0;
};
static_assert(sizeof(AnimationClockView)==48);
class DisplayAnimationClocks {
public:
    void initialize(unsigned gameHz,unsigned deliveryHz=60){gameHz_=gameHz;deliveryHz_=deliveryHz;phase_=0;sourceFrame_=false;displayFrames_=sourceFrames_=actorFrames_=effectFrames_=0;clocks_.initialize(gameHz);}
    bool beginFrame(){
        ++displayFrames_;
        phase_+=gameHz_;sourceFrame_=phase_>=deliveryHz_;
        if(!sourceFrame_)return false;
        phase_-=deliveryHz_;++sourceFrames_;
        const bool animate=clocks_.renderEffect();effectFrames_+=animate;return animate;
    }
    bool actorFrame(bool logicalRunning=true){
        const bool animate=sourceFrame_&&clocks_.renderActor(logicalRunning);actorFrames_+=animate;return animate;
    }
    bool nextActorFrame(bool logicalRunning=true)const{
        auto next=*this;next.beginFrame();return next.actorFrame(logicalRunning);
    }
    bool slow(double rate,int milliseconds){return clocks_.slow(rate,milliseconds);}
    void resume(){clocks_.resume();}
    bool sourceFrame()const{return sourceFrame_;}
    const GameAnimationClocks& clocks()const{return clocks_;}
    AnimationClockView view()const{
        return {1,unsigned(sourceFrame_),displayFrames_,sourceFrames_,actorFrames_,effectFrames_,
            clocks_.actor().gap(),clocks_.actor().frame(),clocks_.effect().gap(),clocks_.effect().frame(),
            float(clocks_.slowRate()),clocks_.slowFrames()};
    }
private:
    GameAnimationClocks clocks_;
    unsigned gameHz_=60,deliveryHz_=60,phase_=0;
    unsigned displayFrames_=0,sourceFrames_=0,actorFrames_=0,effectFrames_=0;
    bool sourceFrame_=false;
};
} // namespace bvn
