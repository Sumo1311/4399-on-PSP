// Adapted from MusouCtrl.missionComplete/onSelfDead/onTimeOver.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace bvn {
enum class MusouResult { Idle, Playing, Won, TimeOver, Defeated };
class SourceMusouResult {
public:
    bool complete(){
        if(complete_)return false;
        complete_=finished_=true;result_=MusouResult::Won;++revision_;return true;
    }
    void defeat(){finished_=true;result_=MusouResult::Defeated;++revision_;}
    void timeOver(){result_=MusouResult::TimeOver;++revision_;}
    MusouResult result()const{return result_;}
    bool completeAccepted()const{return complete_;}
    bool finished()const{return finished_;}
    unsigned revision()const{return revision_;}
private:
    MusouResult result_=MusouResult::Playing;
    unsigned revision_=0;
    bool complete_=false,finished_=false;
};
}
