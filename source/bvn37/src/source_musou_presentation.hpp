// Copyright (C) 2021-2026, 5DPLAY Game Studio
// Adapted from MusouUI, GameStartCtrl and the pinned KyoTimeout.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_timeout_queue.hpp"
#include <algorithm>

namespace bvn {
class SourceMusouPresentation {
public:
    enum class Kind : unsigned { None, Start, Entrance, Knockout, Win, Lose };
    enum Signal : unsigned { Fight=1, Warning=2, Ko=4, BossMusic=8 };
    void initialize(unsigned hz){*this={};hz_=hz;startActive_=true;play(Kind::Start,9);listeners_.push_back(Complete::Start);}
    void entrance(){entranceDone_=false;play(Kind::Entrance,79);listeners_.push_back(Complete::Entrance);}
    void knockout(){timeouts_.timeout(PlayKo,1000,hz_);}
    void end(bool won,unsigned actor){
        ending_=true;play(won?Kind::Win:Kind::Lose,won?258:315);
        listeners_.push_back(won?Complete::Win:Complete::Lose);
        if(won)timeouts_.timeout(Victory,1000,hz_,{int(actor)});
    }
    // Root ENTER_FRAME registration order is an explicit host adapter. A newly
    // registered timeout listener first participates on the following root.
    void beginRoot(){stripFrame_=stripActive()?((stripFrame_+1)%200):0;listenerAtStart_=timeouts_.listening();++roots_;}
    template<class View> void endRoot(View& view){
        if(!listenerAtStart_)return;
        ++timeoutRoots_;
        timeouts_.render([&](unsigned id,std::span<const int> params){
            if(id==PlayKo){++koListeners_;listeners_.push_back(Complete::Ko);play(Kind::Knockout,184);signals_|=Ko;}
            else if(id==Victory)view.victory(unsigned(params[0]));
            else if(id==Return){
                const bool won=!params.empty()&&params[0];
                passRequested_|=won;returnRequested_=true;++returnSerial_;
                if constexpr(requires{view.finish(won);})view.finish(won);
            }
        });
    }
    template<class View> void renderAnimate(View& view){
        if(kind_==Kind::None||stopped())return;
        ++frame_;if(frame_==90||!stripActive())stripFrame_=0;
        if(frame_==39)signals_|=Fight;
        if(frame_==80)signals_|=Warning;
        if(!stopped())return;
        ++completions_;
        // Win/lose handlers are separate closures, retained until the next
        // COMPLETE even when another show call seeks this shared clip.
        const auto listeners=listeners_;
        listeners_.erase(std::remove_if(listeners_.begin(),listeners_.end(),[](Complete c){return c!=Complete::Ko;}),listeners_.end());
        for(const auto listener:listeners){
            if(listener==Complete::Ko)view.koComplete();
            else if(listener==Complete::Start)startDone_=true;
            else if(listener==Complete::Entrance){entranceDone_=true;signals_|=BossMusic;}
            else timeouts_.timeout(Return,listener==Complete::Win?4000:3000,hz_,{int(listener==Complete::Win)});
        }
    }
    // GameStartCtrl is polled after MainLogic, so this opens actions for the
    // next main frame rather than the frame that delivered UI COMPLETE.
    bool afterMain(){if(startActive_&&startDone_){startActive_=false;return true;}return false;}
    unsigned takeSignals(){const unsigned value=signals_;signals_=0;return value;}
    bool starting()const{return startActive_;}
    bool entranceDone()const{return entranceDone_;}
    bool ending()const{return ending_;}
    bool returnRequested()const{return returnRequested_;}
    bool passRequested()const{return passRequested_;}
    unsigned returnSerial()const{return returnSerial_;}
    unsigned endListeners()const{return std::count_if(listeners_.begin(),listeners_.end(),[](Complete c){return c==Complete::Win||c==Complete::Lose;});}
    Kind kind()const{return kind_;}
    unsigned frame()const{return frame_;}
    unsigned stripFrame()const{return stripFrame_;}
    unsigned roots()const{return roots_;}
    unsigned timeoutRoots()const{return timeoutRoots_;}
    unsigned completions()const{return completions_;}
    unsigned koListeners()const{return koListeners_;}
    const SourceTimeoutQueue& timeouts()const{return timeouts_;}
private:
    enum : unsigned {PlayKo,Victory,Return};
    enum class Complete {Start,Entrance,Ko,Win,Lose};
    bool stripActive()const{return frame_>=90&&frame_<164;}
    void play(Kind kind,unsigned frame){kind_=kind;frame_=frame;stripFrame_=0;}
    bool stopped()const{return frame_==0||frame_==69||frame_==175||frame_==247||frame_==299||frame_==348;}
    unsigned hz_=30,frame_=0,stripFrame_=0,signals_=0,roots_=0,timeoutRoots_=0,completions_=0,koListeners_=0;
    Kind kind_=Kind::None;
    SourceTimeoutQueue timeouts_{};
    std::vector<Complete> listeners_{};
    unsigned returnSerial_=0;
    bool startActive_=false,startDone_=false,entranceDone_=false,ending_=false;
    bool returnRequested_=false,passRequested_=false,listenerAtStart_=false;
};
}
