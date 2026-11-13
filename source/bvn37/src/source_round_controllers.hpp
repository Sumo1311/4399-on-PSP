// Adapted from GameStartCtrl, GameEndCtrl, FightUI and TransUI.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>

namespace bvn {
// Controller holds use AS3 post-decrement. In particular, an intro animation
// blocks the decrement itself; UI COMPLETE clears a hold before the next main
// controller call. Presentation callbacks are supplied by the owning scene.
class SourceStartController {
public:
    enum class Kind { None, Versus, NextRound, Musou };
    template<class View> void versus(unsigned hz,int team,View& view){
        *this={};kind_=Kind::Versus;hz_=hz;team_=team;step_=-1;
        if(team==1)view.focus(0);else if(team==2)view.focus(1);
        view.transitionOut(true);
    }
    template<class View> void nextRound(unsigned hz,View& view){
        *this={};kind_=Kind::NextRound;hz_=hz;uiPlaying_=true;
        view.transitionOut(false);view.startBanner();
    }
    template<class View> void musou(unsigned hz,View& view){
        *this={};kind_=Kind::Musou;hz_=hz;view.startBanner();
    }
    void transitionComplete(){step_=team_==1?1:team_==2?2:0;}
    void uiComplete(){uiPlaying_=false;musouFinish_=true;}
    template<class View> void skip(View& view){
        if(kind_!=Kind::Versus||step_>=5)return;
        view.quickTransition();view.focus(-1);uiPlaying_=false;step_=6;
        view.fadeIn();view.idle();hold_=int(hz_/2);
    }
    template<class View> bool render(bool introPlaying,View& view){
        if(kind_==Kind::NextRound)return !uiPlaying_;
        if(kind_==Kind::Musou)return musouFinish_;
        if(kind_!=Kind::Versus||uiPlaying_||introPlaying)return false;
        if(hold_-- > 0)return false;
        switch(step_){
        case 0:
            if(team_==-1||team_==1){view.focus(0);hold_=int(hz_/2);step_=1;}
            else step_=2;
            break;
        case 1:view.intro(0);hold_=int(.3*hz_);step_=2;break;
        case 2:
            if(team_==-1||team_==2){view.focus(1);hold_=int(hz_/2);step_=3;}
            else step_=4;
            break;
        case 3:view.intro(1);hold_=int(.3*hz_);step_=4;break;
        case 4:view.focus(-1);hold_=int(.1*hz_);step_=5;break;
        case 5:uiPlaying_=true;view.startBanner();step_=6;break;
        case 6:return true;
        }
        return false;
    }
    void finish(){kind_=Kind::None;}
    bool active()const{return kind_!=Kind::None;}
    Kind kind()const{return kind_;}
    int step()const{return step_;}int hold()const{return hold_;}
    bool uiPlaying()const{return uiPlaying_;}
private:
    Kind kind_=Kind::None;
    unsigned hz_=30;int team_=-1,step_=0,hold_=0;
    bool uiPlaying_=false,musouFinish_=false;
};

class SourceEndController {
public:
    void begin(unsigned hz,bool draw){*this={};hz_=hz;draw_=draw;active_=true;}
    void uiComplete(){hold_=0;}
    void transitionComplete(){step_=3;}
    void skip(){if(step_==2)hold_=0;}
    template<class View> bool render(bool allowWin,View& view){
        if(!active_)return false;
        if(hold_-- > 0)return false;
        if(draw_){
            switch(step_){
            case 0:view.endBanner();step_=1;hold_=10*hz_;break;
            case 1:step_=2;hold_=hz_;break;
            case 2:active_=false;view.clearBanner();return true;
            }
        }else{
            switch(step_){
            case 0:view.endBanner();step_=1;hold_=10*hz_;break;
            case 1:
                if(!allowWin)return false;
                view.win();hold_=3*hz_;step_=2;break;
            case 2:step_=22;view.transitionIn();break;
            case 3:active_=false;view.clearBanner();view.fadeOut();return true;
            }
        }
        return false;
    }
    bool active()const{return active_;}bool draw()const{return draw_;}
    int step()const{return step_;}int hold()const{return hold_;}
private:
    unsigned hz_=30;int step_=0,hold_=0;bool active_=false,draw_=false;
};

// Zero-based frame indices, generated/checked against the pinned XFL labels
// and frame scripts. currentFrameLabel applies only on its labelled frame.
class SourceBannerTimeline {
public:
    enum class Kind { None, Start, Knockout, TimeOver, Draw, Winner };
    void start(unsigned round){kind_=Kind::Start;frame_=round<5?9:94;}
    void knockout(){kind_=Kind::Knockout;frame_=144;}
    void timeOver(){kind_=Kind::TimeOver;frame_=219;}
    void draw(){kind_=Kind::Draw;frame_=266;}
    void winner(){kind_=Kind::Winner;frame_=199;}
    void clear(){kind_=Kind::None;frame_=0;}
    bool renderAnimate(){
        if(!frame_||frame_==87||frame_==189||frame_==209||frame_==262||frame_==308)return false;
        if(frame_==139){frame_=54;return false;}
        ++frame_;
        return frame_==87||frame_==189||frame_==262||frame_==308;
    }
    Kind kind()const{return kind_;}
    std::optional<unsigned> frame()const{return kind_==Kind::None?std::nullopt:std::optional<unsigned>(frame_);}
private:Kind kind_=Kind::None;unsigned frame_=0;
};

class SourceTransitionTimeline {
public:
    enum class Callback { None, Start, End };
    void out(bool notifyStart){frame_=44;callback_=notifyStart?Callback::Start:Callback::None;remove_=true;rendering_=visible_=pending_=true;phase_=0;}
    void in(){frame_=9;callback_=Callback::End;remove_=false;rendering_=visible_=pending_=true;phase_=0;}
    Callback render(unsigned gameHz){
        if(!rendering_)return Callback::None;
        if(phase_++ < gameHz/30-1)return Callback::None;
        phase_=0;
        if(frame_==25||frame_==60){
            if(!pending_){rendering_=false;return Callback::None;}
            const auto back=callback_;callback_=Callback::None;pending_=false;
            if(remove_)visible_=false;
            return back;
        }
        visible_=true;++frame_;return Callback::None;
    }
    unsigned frame()const{return frame_;}bool visible()const{return visible_;}
    bool rendering()const{return rendering_;}
private:
    unsigned frame_=0,phase_=0;Callback callback_=Callback::None;
    bool remove_=true,rendering_=false,visible_=false,pending_=false;
};
} // namespace bvn
