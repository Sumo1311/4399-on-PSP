// Copyright (C) 2021-2026, 5DPLAY Game Studio
// Adapted from HpBarLerp, EnergyBarFill/Label, QiBar and FightUI.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fighter_ai_policy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace bvn {
class SourceHpBar {
public:
    void render(double rate,AiState state,unsigned hz){
        if(redMoving_&&rate!=rate_){red_=rate_;redMoving_=false;}
        rate_=rate;const auto diff=rate_-green_;
        if(std::abs(diff)<.01)green_=rate_;else green_+=diff*(diff<0?.4:.04);
        switch(state){
        case AiState::Hurt:delay_=100;break;
        case AiState::Flying:case AiState::Down:
            if(delay_>0){if(!justFly_){delay_=int(1.5*hz);justFly_=true;}else if(delay_>0)--delay_;}break;
        default:delay_=0;justFly_=false;
        }
        if(delay_<=0){const auto diff=rate_-red_;
            if(std::abs(diff)<.01){red_=rate_;redMoving_=false;}
            else{red_+=diff*(diff<0?.1:.02);redMoving_=true;}
        }
    }
    double green()const{return green_;}double red()const{return red_;}double rate()const{return rate_;}
    int delay()const{return delay_;}bool moving()const{return redMoving_;}bool justFly()const{return justFly_;}
private:double green_=1,red_=1,rate_=1;int delay_=0;bool redMoving_=false,justFly_=false;
};
class SourceEnergyBar {
public:
    void render(double rate,bool overload){
        if(overload){if(!overload_){overload_=true;flash_=false;frame_=2;}}
        else if(rate<.3){if(!flash_){flash_=true;count_=0;next_=2;}}
        else if(overload_||flash_){overload_=flash_=false;frame_=1;}
        const auto diff=rate-value_;if(std::abs(diff)<.01)value_=rate;else value_+=diff*.4;
        if(flash_&&++count_>2){count_=0;frame_=next_;next_=next_==1?2:1;}
    }
    double value()const{return value_;}unsigned frame()const{return frame_;}
    unsigned count()const{return count_;}bool flashing()const{return flash_;}bool overload()const{return overload_;}
private:double value_=1;unsigned frame_=1,count_=0,next_=2;bool flash_=false,overload_=false;
};
struct SourceQiPose {
    double x=0,y=0,scale=1;
    bool moveFinished=true;unsigned moveType=0;
    void move(double tx,double ty,double ts){
        if(std::abs(tx-x)<2&&std::abs(ty-y)<2&&std::abs(ts-scale)<.2){x=tx;y=ty;scale=ts;moveFinished=true;}
        x+=(tx-x)*.5;y+=(ty-y)*.5;scale+=(ts-scale)*.5;
    }
    void to(double tx,double ty,double ts){if(moveType==1){if(moveFinished)return;}else{moveType=1;moveFinished=false;}move(tx,ty,ts);}
    void resume(double ox,double oy){if(moveType==0){if(moveFinished)return;}else{moveType=0;moveFinished=false;}move(ox,oy,1);}
};
class SourceQiBar {
public:
    explicit SourceQiBar(unsigned side=0):side_(side){pose_.x=side?749:48;pose_.y=side?535.1:534.1;}
    void render(double qi,double assist,double zoom){
        const auto rate=std::min(qi/100.,3.);const auto diff=rate-value_;
        if(std::abs(diff)<.01)value_=rate;else value_+=diff*.4;
        assist_=assist;
        const bool ready=assist_>=1;
        if(readyVisible_!=ready){readyVisible_=ready;if(ready)readyPlaying_=true;else{readyPlaying_=false;readyFrame_=0;}}
        (void)zoom; // FightUI.renderQibarPos exists but has no call site in the desktop source.
    }
    void animateNested(){if(readyPlaying_)readyFrame_=(readyFrame_+1)%20;maxFrame_=(maxFrame_+1)%14;}
    bool readyVisible()const{return readyVisible_&&readyFrame_<14;}
    bool maxVisible()const{return maxFrame_<7;}
    unsigned readyFrame()const{return readyFrame_;}unsigned maxFrame()const{return maxFrame_;}
    void show(){shown_=true;}void hide(){shown_=false;}
    double value()const{return value_;}double assist()const{return assist_;}bool visible()const{return shown_;}
    const SourceQiPose& pose()const{return pose_;}
private:unsigned side_=0;double value_=0,assist_=0;bool shown_=false,readyVisible_=true,readyPlaying_=true;unsigned readyFrame_=0,maxFrame_=0;SourceQiPose pose_{};
};
struct SourceHudActor {double hp=1000,maxHp=1000,qi=0,energy=100,maxEnergy=100,assist=1;AiState state=AiState::Normal;bool overloaded=false;};
class SourceFightHud {
public:
    SourceFightHud():qi_{SourceQiBar(0),SourceQiBar(1)}{}
    void render(const std::array<SourceHudActor,2>& actor,double zoom,unsigned hz){
        sampled_=true;for(unsigned i=0;i<2;++i){hp_[i].render(actor[i].hp/actor[i].maxHp,actor[i].state,hz);energy_[i].render(actor[i].energy/actor[i].maxEnergy,actor[i].overloaded);qi_[i].render(actor[i].qi,actor[i].assist,zoom);sample_[i]=actor[i];}
    }
    // Desktop launch pins BITMAP_UI=true. These are visibility changes,
    // not the unreachable vector HUD's MovieClip fade animation.
    void animateNested(){for(auto& q:qi_)q.animateNested();}
    bool sampled()const{return sampled_;}
    void show(){shown_=true;for(auto& q:qi_)q.show();}
    void hide(){shown_=false;for(auto& q:qi_)q.hide();}
    void hideQi(){for(auto& q:qi_)q.hide();}
    bool visible()const{return shown_;}
    const SourceHpBar& hp(unsigned side)const{return hp_[side];}
    const SourceEnergyBar& energy(unsigned side)const{return energy_[side];}
    const SourceQiBar& qi(unsigned side)const{return qi_[side];}
    const SourceHudActor& sample(unsigned side)const{return sample_[side];}
private:bool shown_=false,sampled_=false;std::array<SourceHpBar,2> hp_{};std::array<SourceEnergyBar,2> energy_{};std::array<SourceQiBar,2> qi_;std::array<SourceHudActor,2> sample_{};
};
}
