// Adapted from 5DPLAY Game Studio's Bullet controller.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_projectile.hpp"

#include <algorithm>

namespace bvn {
namespace {

double accelerate(double speed, const ProjectileAxis& axis,double factor) {
    return std::max(double(axis.minimum), std::min(double(axis.maximum), speed + axis.acceleration*factor));
}

double dampOnWall(double speed, double onHit) {
    if (std::isnan(onHit)) onHit = speed;
    return std::abs(onHit) < 1 ? 0 : onHit * 0.5;
}

} // namespace

FightProjectile::FightProjectile(const FighterAsset& asset, unsigned owner, ObjectSpawn spawn)
    : asset_(&asset), def_(&asset.projectile(spawn.id)), owner_(owner),
      position_{spawn.origin.x + def_->ownerX + def_->spawnX * spawn.facing, spawn.origin.y + def_->spawnY},
      velocity_{def_->horizontal.speed, def_->vertical.speed}, onHit_{def_->horizontal.onHit, def_->vertical.onHit},
      powerRate_(spawn.powerRate), customPower_(spawn.strikePower.forStrike(def_->strikeIndex)),
      facing_(spawn.facing), life_(def_->life), hits_(def_->hits), health_(def_->health), phase_(def_->initial) {
    sourceMainOwner_=spawn.sourceMainOwner;
    auto hit = std::make_shared<RuntimeHit>();
    if (spawn.hitModel) *hit = spawn.hitModel->hits[def_->strikeIndex];
    else {
        hit->strike = asset.strike(def_->strikeIndex);
        hit->damageRate = powerRate_;
        hit->customPower = customPower_;
    }
    hit_ = std::move(hit);
}

bool FightProjectile::step(const StageGeometry& stage, bool animate, double rate) {
    position_.y = std::min(position_.y, stage.bottom);
    bool floorHit = false;
    if (!impact_) {
        position_.x += velocity_.x * facing_ * defaultSpeed_ * rate;
        position_.y += velocity_.y * defaultSpeed_ * rate;
        // Packed acceleration is the authored params.add. Bullet's constructor
        // multiplies it by two before render applies current SPEED_PLUS.
        const double acceleration=sourcePhysics_?2*defaultSpeed_*rate:1;
        velocity_.x = accelerate(velocity_.x, def_->horizontal,acceleration);
        velocity_.y = accelerate(velocity_.y, def_->vertical,acceleration);
        if (life_ != -1 && !expired_ && --life_ <= 0) {
            if (def_->timeout == FinishClip) {
                active_ = false;
                return false;
            }
            expired_ = true;
            phase_ = def_->timeout;
        }
        if (position_.y > stage.playerBottom) {
            consumeHit();
            floorHit = true;
        } else if (position_.x < stage.left - 20 || position_.x > stage.right + 20 || position_.y > stage.bottom + 20) {
            active_ = false;
            return false;
        }
    }
    if (animate) {
        const unsigned count = asset_->clip(def_->clip).count;
        phase_ = std::min(phase_ + 1, count - 1);
        if (phase_ == def_->loopAt) phase_ = def_->loopTo;
        else if (phase_ == def_->disableHitAt) canHit_ = false;
        else if (phase_ == def_->removeAt || phase_ + 2 == count) active_ = false;
    }
    return floorHit;
}

std::optional<StrikeContact> FightProjectile::strikeAgainst(Point target, WorldBox targetBody) const {
    if (!canHit_ || targetBody.empty() || !inStrikeRange(position_, target) || !body().overlaps(targetBody)) return {};
    return StrikeContact{hit_->strike, facing_, position_.x, hit_->damageRate, hit_->customPower, hit_,true,
                        body().intersection(targetBody),StrikeOwnerKind::Bullet};
}

void FightProjectile::consumeHit() {
    if (hits_ > 0 && --hits_ == 0) impact();
}

void FightProjectile::hitFighter(bool againstWall) {
    consumeHit();
    if (onHit_.x == 0 && onHit_.y == 0) return;
    if (againstWall) {
        onHit_.x = dampOnWall(velocity_.x, onHit_.x);
        onHit_.y = dampOnWall(velocity_.y, onHit_.y);
    }
    hitProjectile();
}

void FightProjectile::hitProjectile() {
    if (!std::isnan(onHit_.x)) velocity_.x = onHit_.x;
    if (!std::isnan(onHit_.y)) velocity_.y = onHit_.y;
}

void FightProjectile::receiveHit(int damage) {
    if (!impact_ && (health_ -= damage) <= 0) impact();
}

void FightProjectile::receiveSourceHit(double power) {
    // Bullet.hp is int. Coerce the result of the subtraction, not its power.
    double value=double(health_)-power;
    if(!std::isfinite(value))health_=0;
    else{
        value=std::fmod(std::trunc(value),4294967296.);
        if(value<0)value+=4294967296.;
        health_=int(value>=2147483648.?value-4294967296.:value);
    }
    if(health_<=0)impact();
}

void FightProjectile::impact() {
    if (!impact_ && def_->impact != FinishClip) phase_ = def_->impact;
    impact_ = true;
    canHit_ = false;
}

} // namespace bvn
