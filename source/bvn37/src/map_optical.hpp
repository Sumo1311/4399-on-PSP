// Adapted from 5DPLAY MapMain.render and MapLayer.renderOptical/getBlurBitmap.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "stage_motion.hpp"
#include "map_blur.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace bvn {
struct OpticalRect {
    double x=0,y=0,width=0,height=0;
    bool empty()const{return width<=0||height<=0;}
    bool intersects(const OpticalRect& other)const{
        return x+width>other.x&&other.x+other.width>x&&y+height>other.y&&other.y+other.height>y;
    }
};
struct OpticalSprite {
    OpticalRect area{};
    bool valid=true,destroyed=false;
};
struct OpticalView {
    static constexpr unsigned Missing=0xffffffff;
    unsigned mask=0,counter=0;
    std::array<unsigned,5> cached{Missing,Missing,Missing,Missing,Missing};
    bool active=false;
    unsigned captures=0;
};
class MapOpticalState {
public:
    void initialize(std::span<const OpticalRect> children){children_=children;state_={};state_.active=!children.empty();}
    void advance(Point camera,std::span<const OpticalSprite> sprites){
        // MapMain returns before calling either layer when the stage list is empty.
        if(children_.empty()||sprites.empty())return;
        if(++state_.counter<2)return;
        state_.counter=0;state_.mask=0;
        const double shiftX=-camera.x*.1,shiftY=std::max(0.,(600-camera.y)*.1);
        for(unsigned i=0;i<children_.size();++i){
            auto rect=children_[i];rect.x+=shiftX;rect.y+=shiftY;
            for(const auto& sprite:sprites){
                if(sprite.valid&&!sprite.destroyed&&!sprite.area.empty()&&rect.intersects(sprite.area)){
                    state_.mask|=1u<<i;break;
                }
            }
        }
    }
    void capture(double x,double y){
        if(children_.empty()||y!=0)return;
        const auto found=std::ranges::find(MapBlurStrengths,x);
        if(found==MapBlurStrengths.end())return;
        auto& mask=state_.cached[found-MapBlurStrengths.begin()];
        if(mask==OpticalView::Missing){mask=state_.mask;++state_.captures;}
    }
    OpticalView view()const{return state_;}
private:
    std::span<const OpticalRect> children_{};
    OpticalView state_{};
};
} // namespace bvn
