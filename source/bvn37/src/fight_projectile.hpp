// Adapted from 5DPLAY Game Studio's Bullet controller.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fighter_motion.hpp"

namespace bvn {

class FightProjectile {
public:
    FightProjectile(const FighterAsset& asset, unsigned owner, ObjectSpawn spawn);

    bool step(const StageGeometry& stage, bool animate, double rate = 1);
    void sourceHz(unsigned gameHz){
        if(!sourcePhysics_)life_=life_==-1?-1:life_*int(gameHz)/LogicHz;
        defaultSpeed_=double(AnimationHz)/gameHz;sourcePhysics_=true;
    }
    std::optional<StrikeContact> strikeAgainst(Point target, WorldBox body) const;
    void hitFighter(bool againstWall);
    void hitProjectile();
    void receiveHit(int damage);
    void receiveSourceHit(double power);
    int health()const{return health_;}

    const FighterAsset& asset() const { return *asset_; }
    unsigned owner() const { return owner_; }
    bool sourceMainOwner()const{return sourceMainOwner_;}
    Point position() const { return position_; }
    Point velocity() const { return {velocity_.x * facing_, velocity_.y}; }
    int facing() const { return facing_; }
    unsigned phase() const { return phase_; }
    bool active() const { return active_; }
    bool attacking() const { return active_ && canHit_ && !impact_; }
    bool impacting() const { return impact_; }
    unsigned maxHealth() const { return def_->health; }
    WorldBox body() const { return worldBox(def_->bounds, position_, facing_); }
    const FighterPose& pose() const { return asset_->frame(def_->clip, phase_).pose; }

private:
    void consumeHit();
    void impact();

    const FighterAsset* asset_;
    const ProjectileAsset* def_;
    unsigned owner_;
    Point position_;
    Point velocity_, onHit_;
    double powerRate_;
    double customPower_;
    std::shared_ptr<const RuntimeHit> hit_;
    int facing_, life_, hits_, health_;
    unsigned phase_;
    bool active_ = true, canHit_ = true, impact_ = false, expired_ = false;
    double defaultSpeed_=SpeedPerTick;
    bool sourcePhysics_=false;
    bool sourceMainOwner_=true;
};

} // namespace bvn
