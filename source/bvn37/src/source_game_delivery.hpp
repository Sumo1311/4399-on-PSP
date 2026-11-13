// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game_settings.hpp"
#include <utility>

namespace bvn {
class SourceGameDelivery {
public:
    void initialize(unsigned hz){hz_=hz==30?30:60;phase_=0;}
    bool advance(){phase_+=hz_;if(phase_<60)return false;phase_-=60;return true;}
private:
    unsigned hz_=60,phase_=0;
};

// PSP/link sampling stays at 60 Hz. A press between two 30-Hz game callbacks
// must survive until the next callback, while release/held state stays current.
class QueuedFighterControls {
public:
    void clear(){latest_={};edges_={};pressed_=0;}
    void submit(FighterControls input){
        latest_=input;
        constexpr unsigned vertical=(1U<<unsigned(HumanKey::Up))|(1U<<unsigned(HumanKey::Down));
        constexpr unsigned horizontal=(1U<<unsigned(HumanKey::Left))|(1U<<unsigned(HumanKey::Right));
        for(unsigned axis:{vertical,horizontal})if(input.pressedKeys&axis)pressed_&=~axis;
        pressed_|=input.pressedKeys;
        edges_.jumpPressed|=input.jumpPressed;edges_.attackPressed|=input.attackPressed;
        edges_.dashPressed|=input.dashPressed;edges_.specialPressed|=input.specialPressed;
        edges_.superPressed|=input.superPressed;edges_.quickJump|=input.quickJump;
    }
    FighterControls take(){
        auto result=latest_;
        if(result.sourceHuman&&pressed_)result=HumanInput::controlsForFrame(
            result.heldKeys|pressed_,result.bufferedKeys|pressed_,pressed_,result.classicInput);
        if(!result.sourceHuman){
            result.jumpPressed|=edges_.jumpPressed;result.attackPressed|=edges_.attackPressed;
            result.dashPressed|=edges_.dashPressed;result.specialPressed|=edges_.specialPressed;
            result.superPressed|=edges_.superPressed;result.quickJump|=edges_.quickJump;
        }
        edges_={};pressed_=0;return result;
    }
private:
    FighterControls latest_{},edges_{};
    unsigned pressed_=0;
};
} // namespace bvn
