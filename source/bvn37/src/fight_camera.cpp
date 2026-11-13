// Adapted from 5DPLAY Game Studio's GameCamera and GameStage camera initialization.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_camera.hpp"

#include <algorithm>
#include <cmath>

namespace bvn {
namespace {

constexpr double MinZoom = 1;
constexpr double MaxZoom = 2.5;

double limitedZoom(double first, double second, double extent) {
    const double distance = std::abs(first - second);
    return distance > 0 ? extent * 0.8 / distance : MaxZoom;
}

double limitPosition(double value, double low, double high) {
    return std::min(std::max(value, low), high);
}

} // namespace

void FightCamera::reset(const StageAsset& stage) {
    tween_=baseTween_;single_=false;
    const StageGeometry geometry = stage.geometry();
    mapWidth_ = stage.cameraWidth();
    offsetY_ = geometry.bottom - geometry.playerBottom;
    if(source_){
        sourceCamera_.initialize(mapWidth_,geometry.bottom,geometry.playerBottom,sourceHz_);
        targetZoom_=sourceCamera_.zoom();targetX_=sourceCamera_.target().x;
        view_={{sourceCamera_.display().x,sourceCamera_.display().y},sourceCamera_.scale()};return;
    }
    targetZoom_ = 2;
    targetX_ = limitPosition(-(mapWidth_ - 350), 0, mapWidth_ - StageViewWidth / targetZoom_);
    const double startY = limitPosition(200 - geometry.bottom, -1000 * targetZoom_,
                                        StageViewHeight - StageViewHeight / targetZoom_);
    view_ = {{targetX_ / tween_, startY / tween_}, 1};
}

void FightCamera::track(Point first, Point second, bool single) {
    if(source_){
        if(single&&!single_){sourceCamera_.setZoom(3.5);}
        single_=single;sourceCamera_.focus(single?1:2);
        const std::array<Point,2> points{first,second};sourceCamera_.render(std::span(points).first(single?1:2));
        targetZoom_=sourceCamera_.zoom();targetX_=sourceCamera_.target().x;
        view_={{sourceCamera_.display().x,sourceCamera_.display().y},sourceCamera_.scale()};return;
    }
    single_=single;
    targetZoom_ = single ? 3.5 : std::clamp(std::min(limitedZoom(first.x, second.x, StageViewWidth),
                                                    limitedZoom(first.y, second.y, StageViewHeight)), MinZoom, MaxZoom);
    trackAtZoom(first, second);
}

void FightCamera::trackSolo(Point focus) {
    if(source_){
        single_=false;sourceCamera_.focus(1);sourceCamera_.render(std::span(&focus,1));
        targetZoom_=sourceCamera_.zoom();targetX_=sourceCamera_.target().x;
        view_={{sourceCamera_.display().x,sourceCamera_.display().y},sourceCamera_.scale()};return;
    }
    single_=false;
    // Flash leaves its last zoom unchanged when Musou only has the player as a focus.
    trackAtZoom(focus, focus);
}

void FightCamera::trackAtZoom(Point first, Point second) {
    const double width = StageViewWidth / targetZoom_;
    const double height = StageViewHeight / targetZoom_;
    targetX_ = limitPosition((first.x + second.x - width) * 0.5, 0, mapWidth_ - width);
    const double targetY = limitPosition((first.y + second.y - height) * 0.5 - offsetY_,
                                         -1000 * targetZoom_, StageViewHeight - height);
    if(tween_>1){
        view_.origin.x += (targetX_ - view_.origin.x) / tween_;
        view_.origin.y += (targetY - view_.origin.y) / tween_;
        view_.zoom += (targetZoom_ - view_.zoom) / tween_;
    }else view_={{targetX_,targetY},targetZoom_};
}

std::optional<CameraLimits> FightCamera::fighterLimits() const {
    if ((source_?sourceCamera_.zoom():targetZoom_) != MinZoom) return std::nullopt;
    return CameraLimits{targetX_ + 10, targetX_ + StageViewWidth};
}

} // namespace bvn
