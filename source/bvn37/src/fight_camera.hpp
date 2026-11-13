// Adapted from 5DPLAY Game Studio's GameCamera and GameStage camera initialization.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "stage_asset.hpp"
#include "source_camera.hpp"

namespace bvn {

struct CameraView {
    Point origin;
    double zoom = 1;
};

class FightCamera {
public:
    void sourceHz(unsigned hz){source_=true;sourceHz_=hz;baseTween_=int(2.5/(double(AnimationHz)/hz));tween_=baseTween_;}
    void resumeFocus(){if(source_){tween_=baseTween_;sourceCamera_.tween=int(tween_);sourceCamera_.focus(2);single_=false;}}
    void focusOne(){if(source_){resumeFocus();sourceCamera_.setZoom(3.5);sourceCamera_.focus(1);targetZoom_=3.5;single_=true;}}
    void resumeSoloFocus(){if(source_){resumeFocus();sourceCamera_.setZoom(2);sourceCamera_.focus(1);targetZoom_=2;single_=false;}}
    void updateFocus(unsigned count){if(source_){resumeFocus();sourceCamera_.setZoom(2);sourceCamera_.focus(count);targetZoom_=2;}}
    void slow(double rate){if(source_){tween_=int(2.5*(rate>0?rate:1));sourceCamera_.tween=int(tween_);}}
    void reset(const StageAsset& stage);
    void track(Point first, Point second, bool single = false);
    void trackSolo(Point focus);
    CameraView view() const { return view_; }
    std::optional<CameraLimits> fighterLimits() const;
    const SourceCamera& sourceCamera()const{return sourceCamera_;}

private:
    void trackAtZoom(Point first, Point second);
    CameraView view_{};
    double mapWidth_ = StageViewWidth, offsetY_ = 0;
    double targetX_ = 0, targetZoom_ = 2;
    double tween_=5,baseTween_=5;
    bool source_=false,single_=false;
    SourceCamera sourceCamera_{};
    unsigned sourceHz_=60;
};

} // namespace bvn
