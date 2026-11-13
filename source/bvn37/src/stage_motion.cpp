// Adapted from 5DPLAY Game Studio's BaseGameSprite, GameLogic and FloorVO.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "stage_motion.hpp"

#include <algorithm>

namespace bvn {
namespace {

constexpr double Gravity = 12;
constexpr double GravityStep = 1.2;
constexpr double GravityOnFloor = 4;
constexpr double SideInset = 10;
constexpr double JumpDamping = 0.5;

double weaken(double value, double amount) {
    if (amount <= 0 || value == 0) return value;
    return value > 0 ? std::max(0.0, value - amount) : std::min(0.0, value + amount);
}

} // namespace

bool Floor::touches(Point position, double range) const {
    return position.y > y - range && position.y < y + range &&
           position.x > left && position.x < right;
}

void Motion::sourceHz(unsigned gameHz){
    const double next=double(AnimationHz)/gameHz;
    const double ratio=next/defaultSpeed_;
    damping.x*=ratio;damping.y*=ratio;
    secondaryDamping_.x*=ratio;secondaryDamping_.y*=ratio;
    speed*=ratio;defaultSpeed_=next;sourcePhysics_=true;
}

void Motion::move(Point delta) {
    position.x += delta.x * speed;
    position.y += delta.y * speed;
}

void Motion::setVelocity(Point value) {
    velocity = value;
    damping = {};
}

void Motion::setDamping(Point value) {
    damping = {value.x * defaultSpeed_ * 2, value.y * defaultSpeed_ * 2};
}

void Motion::setSecondaryVelocity(Point value, Point drag) {
    secondaryVelocity = value;
    secondaryDamping_ = {drag.x * defaultSpeed_ * 6, drag.y * defaultSpeed_ * 6};
}

void Motion::jump(double power) {
    gravity_ = 0;
    setVelocity({0, -power});
    setDamping({0, JumpDamping});
}

bool Motion::updateFloor(const StageGeometry& stage) {
    touchingBottom = false;
    if (velocity.y < 0) return false;
    if (position.y > stage.playerBottom - Gravity) {
        position.y = stage.playerBottom;
        touchingBottom = true;
        return true;
    }
    if (velocity.y >= 5*(sourcePhysics_?speed:defaultSpeed_)) return false;

    if (platform_) {
        const Floor& previous = stage.floors[*platform_];
        if (previous.touches(position,10*defaultSpeed_)) {
            position.y = previous.y;
            return true;
        }
        // Flash releases a lost contact for one tick before testing other platforms.
        platform_.reset();
        return false;
    }
    for (std::size_t i = 0; i < stage.floors.size(); ++i) {
        const Floor& floor = stage.floors[i];
        if (floor.touches(position,10*defaultSpeed_)) {
            position.y = floor.y;
            platform_ = i;
            return true;
        }
    }
    return false;
}

void Motion::step(const StageGeometry& stage, std::optional<CameraLimits> cameraLimits) {
    if (stage.floors.data() != floors_.data() || stage.floors.size() != floors_.size()) {
        floors_ = stage.floors;
        platform_.reset();
    }
    fixPosition(stage,cameraLimits);

    airborne = !updateFloor(stage);
    if (!airborne) {
        gravity_ = GravityOnFloor;
    } else if (!applyGravity || velocity.y < 0) {
        gravity_ = 0;
    } else {
        gravity_ = std::min(Gravity, gravity_ + GravityStep * (sourcePhysics_?speed:defaultSpeed_));
        move({0, gravity_});
    }
    renderVelocity();
}

void Motion::fixPosition(const StageGeometry& stage,std::optional<CameraLimits> cameraLimits) {
    if (!allowCrossSide) {
        double left = stage.left + SideInset;
        double right = stage.right - SideInset;
        if (velocity.x != 0 && cameraLimits) {
            left = std::max(left, cameraLimits->left);
            right = std::min(right, cameraLimits->right);
        }
        touchingSide = position.x <= left || position.x >= right;
        position.x = std::min(right, std::max(left, position.x));
    }
    if (!allowCrossBottom) position.y = std::min(stage.bottom, position.y);

}

void Motion::renderVelocity() {
    const Point velocityThisTick{velocity.x + secondaryVelocity.x, velocity.y + secondaryVelocity.y};
    const double dampingRate = speed / defaultSpeed_;
    velocity.x = weaken(velocity.x, damping.x * dampingRate);
    velocity.y = weaken(velocity.y, damping.y * dampingRate);
    secondaryVelocity.x = weaken(secondaryVelocity.x, secondaryDamping_.x * dampingRate);
    secondaryVelocity.y = weaken(secondaryVelocity.y, secondaryDamping_.y * dampingRate);
    move(velocityThisTick);
}

} // namespace bvn
