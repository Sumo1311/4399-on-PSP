// Adapted from 5DPLAY's EffectSlowHandler; map-layer cache parameters stay separate.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace bvn {
inline constexpr std::array<unsigned,5> MapBlurStrengths{3,4,6,8,10};
struct MapBlurView {
    double x=0,y=0,slowRate=0;
    int frames=0,slowFrames=0;
    bool enabled=true;
};
class MapBlurState {
public:
    unsigned requests()const{return requests_;}
    void gameHz(unsigned value){gameHz_=value;}
    void enabled(bool value){state_.enabled=value;}
    bool request(double x,double y,int milliseconds=1000){
        if(!state_.enabled||state_.x>x||state_.y>0)return false;
        state_.x=x;state_.y=y;state_.frames=int(milliseconds*.001*30);
        ++requests_;
        return true;
    }
    void cancel(){state_.frames=0;state_.x=state_.y=0;}
    bool slow(double rate,int milliseconds=1000){
        if(state_.slowRate>rate)return false;
        state_.slowRate=rate;request(rate*2,0,250);
        state_.slowFrames=milliseconds?int(milliseconds*.001*gameHz_):0;
        return true;
    }
    void freeze(unsigned milliseconds){if(milliseconds>300&&state_.slowRate>0)request(state_.slowRate*4,0,500);}
    void resumeSlow(){state_.slowRate=0;state_.slowFrames=0;}
    void render(){if(state_.slowFrames>0&&--state_.slowFrames<=0)resumeSlow();}
    void animate(){if(state_.frames>0&&--state_.frames<=0)cancel();}
    MapBlurView view()const{return state_;}
private:
    unsigned gameHz_=60;
    MapBlurView state_{};
    unsigned requests_=0;
};
} // namespace bvn
