// Copyright (C) 2021-2026, 5DPLAY Game Studio
// Adapted from FighterMC and FighterMcHurtCtrl. Animation is 30 Hz; the
// owning scene supplies its actual actor-animation calls and floor inputs.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fighter_ai_policy.hpp"
#include <algorithm>

namespace bvn {
class SourceHurtTimeline {
public:
    enum class Pose : unsigned { None,Hurt,Fly,Fall,Bounce,Down,Recover };
    enum class Fly : unsigned { None,Air,Fall,Bounce,Lie };
    enum class Delay : unsigned { None,Fly,Down };
    struct Input {bool alive=true,air=false;double y=0,secondaryX=0,secondaryY=0;int facing=1;};
    void reset(){*this={};}
    // A normal goFrame/idle cancels the one pending frame delay. It does
    // not call FighterMC.stopHurtFly: a re-hit may leave its flight machine live.
    void externalFrame(AiState state,bool clearState=false){
        state_=state;pose_=Pose::None;age_=0;delay_=Delay::None;delayFrames_=0;playing_=false;playFrames_=0;
        if(clearState)hurting_=flying_=false;
        active_=fly_!=Fly::None||hurting_||flying_;
    }
    template<class View> void stun(int hold,bool catchHit,View& view){
        active_=hurting_=true;flying_=false;hold_=hold;
        go(Pose::Hurt,!catchHit,catchHit?0:7,view);state_=AiState::Hurt;
    }
    template<class View> void launch(double x,double y,const Input& input,bool showHit,View& view){
        active_=flying_=true;hurting_=false;state_=AiState::Flying;
        if(x!=0)view.face(x>0?-1:1);
        if(showHit)go(Pose::Hurt,false,0,view,Delay::Fly,1);
        else go(Pose::Fly,false,0,view);
        heavy_=y>5;flyFrames_=heavy_||input.air?0:15;
        view.velocity(x,y);view.damping(0,.5);
        fly_=Fly::Air;minY_=input.y;hitY_=y;
    }
    // FighterMain.die/playHurtDown retain the preceding action state until
    // the delayed HURT_DOWN frame; zero HP alone never assigns DEAD.
    template<class View> void down(AiState previous,View& view){
        state_=previous;
        active_=true;hurting_=flying_=false;
        go(Pose::Bounce,false,0,view,Delay::Down,2);
        view.impact(1,2);view.damping(2,0);
    }
    template<class View> void animate(const Input& input,View& view){
        if(!active_)return;
        if(playing_){
            if(playFrames_>0&&--playFrames_<=0)playing_=false;
            view.advance();
        }
        // renderChildren runs even when the parent MovieClip is stopped.
        // pose() includes goFrame's initial child advance.
        view.children();
        renderFly(input,view);
        ++age_;
        if(delay_!=Delay::None&&delayFrames_--<=0){
            const auto callback=delay_;
            if(callback==Delay::Fly)go(Pose::Fly,false,0,view);
            else{
                go(Pose::Down,false,0,view);downFrames_=15;fly_=Fly::Lie;
                view.touchFloor();state_=AiState::Down;++downEvents_;view.down();
            }
            delay_=Delay::None;
        }
        // FighterMcCtrler invokes this after FighterMC.renderAnimate.
        if(hurting_&&hold_--<=0){
            if(input.alive)view.recover();
            else{
                hurting_=flying_=false;
                if(input.air)launch(input.secondaryX*input.facing,input.secondaryY,input,false,view);
                else down(state_,view);
            }
        }
    }
    bool active()const{return active_;}bool hurting()const{return hurting_;}bool flying()const{return flying_;}
    bool dead()const{return active_&&state_==AiState::Dead;}
    void authoredState(AiState value){state_=value;}
    AiState state()const{return state_;}Pose pose()const{return pose_;}Fly fly()const{return fly_;}
    bool playing()const{return playing_;}int playFrames()const{return playFrames_;}
    unsigned age()const{return age_;}int hold()const{return hold_;}int flyFrames()const{return flyFrames_;}int downFrames()const{return downFrames_;}
    Delay delay()const{return delay_;}int delayFrames()const{return delayFrames_;}
    bool heavy()const{return heavy_;}double minY()const{return minY_;}double hitY()const{return hitY_;}
    unsigned downEvents()const{return downEvents_;}unsigned deadEvents()const{return deadEvents_;}
private:
    template<class View> void go(Pose pose,bool play,int frames,View& view,Delay delay=Delay::None,int delayFrames=0){
        pose_=pose;playing_=play;playFrames_=play?frames:0;age_=0;delay_=delay;delayFrames_=delayFrames;
        view.pose(pose);
    }
    template<class View> void renderFly(const Input& input,View& view){
        switch(fly_){
        case Fly::Air:
            if(--flyFrames_<=0&&!input.air){go(Pose::Fall,true,0,view);fly_=Fly::Fall;}
            minY_=std::min(minY_,input.y);break;
        case Fly::Fall:
            if(age_<2)return;
            if(heavy_){
                downFrames_=30;go(Pose::Down,false,0,view);state_=AiState::Down;++downEvents_;view.down();
                view.damping(4,0);fly_=Fly::Lie;
                view.impact(2,std::clamp((input.y-minY_)*.04*(1+hitY_*.1),2.,5.));
            }else{
                go(Pose::Bounce,false,0,view);
                const double bounce=std::clamp((input.y-minY_)*.04,3.,8.);
                view.velocityY(-bounce);fly_=Fly::Bounce;state_=AiState::Bounce;
                view.impact(0,std::clamp(bounce,.5,3.));
            }
            break;
        case Fly::Bounce:
            if(age_<2||input.air)return;
            go(Pose::Down,false,0,view);view.damping(2,0);downFrames_=15;fly_=Fly::Lie;
            view.impact(1,1);state_=AiState::Down;++downEvents_;view.down();break;
        case Fly::Lie:
            if(!input.alive){fly_=Fly::None;state_=AiState::Dead;++deadEvents_;view.dead();return;}
            if(--downFrames_<=0){go(Pose::Recover,true,0,view);fly_=Fly::None;}
            break;
        case Fly::None:break;
        }
    }
    AiState state_=AiState::Normal;
    Pose pose_=Pose::None;Fly fly_=Fly::None;Delay delay_=Delay::None;
    unsigned age_=0,downEvents_=0,deadEvents_=0;
    int hold_=0,flyFrames_=0,downFrames_=0,playFrames_=0,delayFrames_=0;
    double minY_=0,hitY_=0;
    bool active_=false,hurting_=false,flying_=false,playing_=false,heavy_=false;
};
}
