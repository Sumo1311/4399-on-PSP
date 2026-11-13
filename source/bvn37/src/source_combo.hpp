// Adapted from GameLogic, FighterEventCtrl, MusouLogic and HitsUI.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "combo_art_data.hpp"
#include "fighter_ai_policy.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <vector>
namespace bvn {
inline bool sourceHitTargetState(AiState state){return state==AiState::Hurt||state==AiState::Flying;}
inline unsigned sourceHitScore(double power,bool bisha,bool catchSkill,bool breaksGuard,bool launches,bool classic){
    const unsigned raw=unsigned(power)*(bisha?2:1)+500*catchSkill+200*breaksGuard+200*launches;
    return classic?raw*4/5:raw;
}
class SourceGameHits {
public:
    struct Entry{unsigned actor,target,ui,hits;};
    unsigned add(unsigned actor,unsigned target,unsigned ui){
        auto found=std::ranges::find(entries_,actor,&Entry::actor);
        if(found==entries_.end())found=entries_.insert(entries_.end(),{actor,target,ui,0});
        found->target=target;return ++found->hits;
    }
    std::optional<unsigned> clearTarget(unsigned target){
        std::optional<unsigned> ui;
        for(const auto& e:entries_)if(e.target==target){ui=e.ui;break;}
        std::erase_if(entries_,[&](const auto& e){return e.target==target;});return ui;
    }
    unsigned count(unsigned actor)const{const auto f=std::ranges::find(entries_,actor,&Entry::actor);return f==entries_.end()?0:f->hits;}
    std::span<const Entry> entries()const{return entries_;}
private:
    std::vector<Entry> entries_;
};
class SourceMusouHits {
public:
    unsigned add(unsigned target){
        if(std::ranges::find(targets_,target)==targets_.end())targets_.push_back(target);
        return ++hits_;
    }
    bool remove(unsigned target){
        const auto found=std::ranges::find(targets_,target);if(found==targets_.end())return false;
        targets_.erase(found);if(!targets_.empty())return false;hits_=0;return true;
    }
    unsigned count()const{return hits_;}
    std::span<const unsigned> targets()const{return targets_;}
private:
    unsigned hits_=0;
    std::vector<unsigned> targets_;
};
class SourceHitsDisplay {
public:
    static double numberWidth(std::uint32_t value){
        char digits[16];std::snprintf(digits,sizeof(digits),"%u",unsigned(value));
        double left=1e30,right=-1e30,x=0;
        for(const char* p=digits;*p;++p,x+=35){const auto& art=ComboDigitArt[*p-'0'];left=std::min(left,x+art.x);right=std::max(right,x+art.x+art.w);}
        return right-left;
    }
    void show(std::uint32_t number,bool left){
        number_=number;offset_=-numberWidth(number)+45;
        if(left)shift_=-offset_;
        if(shown_){frame_=30;playing_=true;return;}
        shown_=visible_=playing_=true;frame_=10;
    }
    void hide(){if(!shown_)return;shown_=false;frame_=51;playing_=true;}
    void render(){
        if(!playing_)return;
        if(++frame_==56){frame_=1;visible_=playing_=false;}
        else if(frame_==20||frame_==38)playing_=false;
    }
    unsigned number()const{return number_;}
    unsigned frame()const{return frame_;}
    bool visible()const{return visible_;}bool shown()const{return shown_;}bool playing()const{return playing_;}
    double offset()const{return offset_;}double shift()const{return shift_;}
private:
    unsigned number_=0,frame_=1;
    double offset_=0,shift_=0;
    bool visible_=false,shown_=false,playing_=false;
};
template<class Draw> void sourceComboDraw(const SourceHitsDisplay& ui,bool musou,unsigned side,Draw&& draw){
    if(!ui.visible())return;
    const auto& frame=(musou?ComboMusouArt:ComboVersusArt)[ui.frame()-1];
    const double x=(musou?106.15:side?662.15:62.15)+ui.shift(),y=musou?116.95:121.95;
    const auto& label=frame[0];draw(label.key,x+label.x,y+label.y,label.a*label.w,label.d*label.h);
    const auto& number=frame[1];char digits[16];std::snprintf(digits,sizeof(digits),"%u",ui.number());double xx=ui.offset();
    for(const char* p=digits;*p;++p,xx+=35){const auto& art=ComboDigitArt[*p-'0'];draw(art.key,x+number.x+number.a*(xx+art.x),y+number.y+number.d*art.y,number.a*art.w,number.d*art.h);}
}
}
