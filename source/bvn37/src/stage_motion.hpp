// Adapted from 5DPLAY Game Studio's BaseGameSprite, GameLogic and FloorVO.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>

namespace bvn {

inline constexpr int LogicHz = 60;
inline constexpr int AnimationHz = 30;
inline constexpr double SpeedPerTick = double(AnimationHz) / LogicHz;

struct Point {
    double x = 0;
    double y = 0;
};

struct Floor {
    double left;
    double right;
    double y;

    bool touches(Point position, double range = 10 * SpeedPerTick) const;
};

struct StageGeometry {
    double left;
    double right;
    double bottom;
    double playerBottom;
    std::span<const Floor> floors;
};

struct CameraLimits {
    double left;
    double right;
};

class Motion {
public:
    Point position;
    Point velocity;
    Point damping;
    Point secondaryVelocity;
    double speed = SpeedPerTick;
    bool applyGravity = true;
    bool allowCrossSide = false;
    bool allowCrossBottom = false;
    bool airborne = false;
    bool touchingSide = false;
    bool touchingBottom = false;

    void move(Point delta);
    void setVelocity(Point value);
    void setDamping(Point value);
    void setSecondaryVelocity(Point value, Point drag);
    void jump(double power);
    void sourceHz(unsigned gameHz);
    double defaultSpeed()const{return defaultSpeed_;}
    bool sourcePhysics()const{return sourcePhysics_;}
    double gravity()const{return gravity_;}
    void step(const StageGeometry& stage, std::optional<CameraLimits> cameraLimits = std::nullopt);
    void renderVelocity();
    void fixPosition(const StageGeometry& stage,std::optional<CameraLimits> cameraLimits = std::nullopt);

private:
    bool updateFloor(const StageGeometry& stage);

    Point secondaryDamping_;
    double gravity_ = 0;
    double defaultSpeed_ = SpeedPerTick;
    bool sourcePhysics_ = false;
    std::span<const Floor> floors_;
    std::optional<std::size_t> platform_;
};

} // namespace bvn
