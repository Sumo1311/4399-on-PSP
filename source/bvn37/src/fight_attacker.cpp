// Adapted from 5DPLAY Game Studio's FighterAttacker controller.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_attacker.hpp"

#include <algorithm>
#include <utility>

namespace bvn {

FightAttacker::FightAttacker(const FighterAsset& asset, unsigned owner, ObjectSpawn spawn, Point target, std::uint32_t seed)
    : asset_(&asset), def_(&asset.attacker(spawn.id)), owner_(owner), powerRate_(spawn.powerRate), strikePower_(spawn.strikePower),
      hitModel_(spawn.hitModel ? std::move(spawn.hitModel) : makeRuntimeHitModel(asset, spawn.powerRate)),
      random_(seed ? seed : 1), facing_(spawn.facing), phase_(def_->initial) {
    if (strikePower_.strike < hitModel_->hits.size() && strikePower_.power >= 0) {
        hitModel_->hits[strikePower_.strike].customPower = strikePower_.power;
    }
    motion_.position = {spawn.origin.x + def_->ownerX + def_->spawnX * facing_, spawn.origin.y + def_->spawnY};
    if (def_->targetAxes & 5) motion_.position.x = target.x + def_->spawnX * facing_;
    if (def_->targetAxes & 10) motion_.position.y = target.y + def_->spawnY;
    for (const auto& event : asset_->events(frame())) {
        if (event.op != FighterOp::AttackerRange) continue;
        if (unsigned(event.command) == 0) {
            const double distance = (motion_.position.x - spawn.origin.x) * facing_;
            motion_.position.x = spawn.origin.x + facing_ * std::clamp(distance, double(event.x), double(event.y));
        } else {
            const double distance = motion_.position.y - spawn.origin.y;
            const double offset = std::clamp(distance, double(event.x), double(event.y));
            if (offset != distance) motion_.position.y = target.y + offset;
        }
    }
    motion_.applyGravity = def_->gravity;
}

double FightAttacker::randomUnit() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_ / 4294967296.0;
}

FighterEffects FightAttacker::step(const StageGeometry& stage, bool animate, std::optional<CameraLimits> limits,
                                  FighterMotion& target, double rate, std::vector<ObjectSpawn>& shots,
                                  FighterMotion* fighterOwner, FighterMotion* opponentsTarget) {
    FighterEffects effects;
    const auto apply = [&] { applyFrameEvents(target, effects, shots, fighterOwner, opponentsTarget); };
    const auto point = target.motion().position;
    motion_.speed = motion_.defaultSpeed() * rate;
    if (removeAtSide_ && motion_.touchingSide) active_ = false;
    if (homing_ && active_) {
        const auto event = *homing_;
        if (phase_ >= event.target) {
            homing_.reset();
        } else if (std::abs(motion_.position.x - point.x) < event.y) {
            queuedFrame_ = event.target;
            homing_.reset();
        } else {
            motion_.setVelocity({motion_.position.x < point.x ? event.x : -event.x, 0});
        }
    }
    const bool first = !std::exchange(entered_, true);
    if (queuedFrame_ != FinishClip) {
        phase_ = std::exchange(queuedFrame_, FinishClip);
        playing_ = true;
        apply();
    } else if (first && (phase_ == 0 || assister())) {
        apply();
    }
    if (!active_) return effects;
    if (!cutin_) {
    motion_.step(stage, motion_.sourcePhysics()?std::nullopt:limits);
        if (probeFrame_ != FinishClip && overlapsProbe(target.body(), probeId_)) {
            phase_ = probeFrame_;
            playing_ = true;
            apply();
        }
        if (!motion_.airborne && !touchingFloor_ && landing_ != FinishClip) {
            phase_ = std::exchange(landing_, FinishClip);
            playing_ = true;
            apply();
        }
        touchingFloor_ = !motion_.airborne;
        if (tracksTarget_ && (def_->targetAxes & 4)) motion_.position.x = point.x + def_->spawnX * facing_;
        if (tracksTarget_ && (def_->targetAxes & 8)) motion_.position.y = point.y + def_->spawnY;
    }
    if (!animate || !playing_ || !active_) return effects;
    const unsigned count = asset_->clip(def_->clip).count;
    phase_ = std::min(phase_ + 1, count - 1);
    apply();
    if (phase_ + 2 == count && assister()) {
        active_ = false;
    } else if (phase_ + 2 == count) {
        const auto last = asset_->events(asset_->frame(def_->clip, count - 1));
        if (std::ranges::none_of(last, [](const auto& e) { return e.op == FighterOp::Remove; })) active_ = false;
    }
    if (phase_ + 1 == count) active_ = false;
    return effects;
}

void FightAttacker::applyControl(const ObjectSpawn& spawn) {
    if (!spawn.asset || spawn.id >= spawn.asset->attackerCount()) return;
    if (motion_.sourcePhysics() ? key() != spawn.asset->attacker(spawn.id).key : def_ != &spawn.asset->attacker(spawn.id)) return;
    if (spawn.jumpFrame == FinishClip) {
        remove();
    } else if (spawn.jumpFrame >= asset_->clip(def_->clip).count) {
        return;
    } else if (spawn.playImmediately) {
        // A sibling MovieClip goto changes the pose on this step. The destination has no frame script.
        phase_ = spawn.jumpFrame;
        queuedFrame_ = FinishClip;
        entered_ = playing_ = true;
    } else {
        queuedFrame_ = spawn.jumpFrame;
    }
}

bool FightAttacker::overlapsProbe(WorldBox body, unsigned probe) const {
    if (body.empty()) return false;
    return std::ranges::any_of(asset_->hits(frame()), [&](const auto& hit) {
        return hit.probe() && hit.strikeIndex == probe && worldBox(hit.box, motion_.position, facing_).overlaps(body);
    });
}

std::optional<ObjectSpawn> FightAttacker::applyFrameListeners(FighterMotion& owner, FighterMotion& opponent,
                                                             FighterMotion* opponentsTarget, bool animate) {
    std::optional<ObjectSpawn> control;
    if (hurtBinding_ && animate && opponent.clip_ != MoveClip::Hurt) {
        const auto event = *hurtBinding_;
        opponent.motion_.applyGravity = true;
        control = ObjectSpawn{.op = FighterOp::ControlAttacker, .id = event.target, .origin = motion_.position,
                              .facing = facing_, .asset = asset_, .jumpFrame = unsigned(event.x)};
        hurtBinding_.reset();
    }
    if (!active_) return control;
    if (opponentsTarget && owner.followAttacker_ == def_ && owner.performing(FighterCommand::SuperAir)) {
        const auto point = opponentsTarget->motion().position;
        opponentsTarget->place({point.x, motion_.position.y + owner.followAttackerY_});
    }
    if (stopsTargetAction_ && opponent.clip_ != MoveClip::Hurt) {
        opponent.applyEvent({FighterOp::End, FighterCommand{}, FinishClip, 0, 0});
    }
    if (ownerQiCap_ && !opponent.guardClip() && owner.qi() > *ownerQiCap_) {
        owner.setQi(*ownerQiCap_);
    }
    if (followsTarget_) {
        for (const auto& event : asset_->events(frame())) {
            if (event.op != FighterOp::AttackerAnchor) continue;
            opponent.place({motion_.position.x, motion_.position.y + event.y});
            break;
        }
    }
    if (wall_) {
        auto target = opponent.motion().position;
        const auto center = motion_.position;
        if (target.y <= center.y + wall_->top || target.y >= center.y + wall_->bottom) return control;
        if (target.x < center.x && target.x > center.x + wall_->left) {
            target.x = center.x + wall_->left;
            opponent.place(target);
        } else if (target.x > center.x && target.x < center.x + wall_->right) {
            target.x = center.x + wall_->right;
            opponent.place(target);
        }
    }
    return control;
}

void FightAttacker::applyFrameEvents(FighterMotion& target, FighterEffects& effects, std::vector<ObjectSpawn>& shots,
                                    FighterMotion* fighterOwner, FighterMotion* opponentsTarget) {
    const auto flush=[&]{if(emissionHook_.effects&&!effects.empty())emissionHook_.effects(emissionHook_.context,std::exchange(effects,{}));};
    const auto spawn=[&](ObjectSpawn shot){flush();if(emissionHook_.spawn)emissionHook_.spawn(emissionHook_.context,owner_,shot,&target);else shots.push_back(std::move(shot));};
    if(paintHook_.change&&phase_==0)sourceLayerTarget_=target.effectTarget();
    for (unsigned jumps = 0; jumps < 8; ++jumps) {
        bool jumped = false;
        const auto script = asset_->events(frame());
        for (std::size_t i = 0; i < script.size(); ++i) {
            const auto& event = script[i];
            const bool wasActive = active_;
            switch (event.op) {
            case FighterOp::IfHit:
                if (!target.wasHitBy(asset_->strike(unsigned(event.x)).key)) i += event.target;
                break;
            case FighterOp::Velocity:
                motion_.setVelocity({double(event.x) * facing_, event.y});
                break;
            case FighterOp::Damping:
                motion_.setDamping({event.x, event.y});
                break;
            case FighterOp::Remove:
                active_ = false;
                departureEffect_ = event.x != 0;
                break;
            case FighterOp::End:
                striking_ = false;
                break;
            case FighterOp::Gravity:
                motion_.applyGravity = event.x != 0;
                break;
            case FighterOp::AttackerLanding:
                landing_ = event.target;
                break;
            case FighterOp::AttackerPause:
                playing_ = false;
                break;
            case FighterOp::AttackerProbe:
                probeId_ = unsigned(event.x);
                probeFrame_ = event.target;
                break;
            case FighterOp::AttackerGoto: {
                const unsigned mode = unsigned(event.command);
                if (mode == 2 && target.wasHitBy(asset_->strike(unsigned(event.x)).key)) break;
                if (mode == 1 && (!fighterOwner || fighterOwner->clip_ == MoveClip::Idle ||
                    fighterOwner->clip_ == MoveClip::Walk || fighterOwner->guardClip() || fighterOwner->injured())) break;
                phase_ = event.target;
                playing_ = true;
                jumped = true;
                break;
            }
            case FighterOp::AttackerCutin:
                cutin_ = unsigned(event.x);
                if(!controllerTarget_)controllerTarget_=std::make_shared<std::byte>();
                effects.controllerRequests.push_back({cutin_==2?SourceControlOp::SuperBisha:
                    cutin_?SourceControlOp::Bisha:SourceControlOp::EndBisha,controllerTarget_});
                break;
            case FighterOp::OwnerBuff:
                if (fighterOwner) fighterOwner->applyBuff(FighterBuff(event.command), event.x / 100.0,
                    unsigned(std::ceil(event.y*fighterOwner->deliveryHz()/LogicHz)));
                break;
            case FighterOp::OwnerHeal:
                if (fighterOwner && (event.target == FinishClip || overlapsProbe(fighterOwner->body(), event.target))) {
                    if (unsigned(event.command)) {
                        fighterOwner->applyEvent({FighterOp::LoseHealth, FighterCommand{}, FinishClip, event.x, 0});
                    } else {
                        fighterOwner->setHealth(fighterOwner->health() + event.x);
                    }
                }
                break;
            case FighterOp::MoveOnce:
                if (fighterOwner) fighterOwner->applyEvent(event);
                break;
            case FighterOp::AssistWind: {
                constexpr double multipliers[]{.15, .8, .65, .5, .35, .2};
                const int weight = int(target.asset().stats().weight);
                const double factor = weight >= 1 && weight <= 5 ? multipliers[weight] : multipliers[0];
                // getTargetSP and target.getDisplay refer to the same sprite. Both authored writes apply.
                target.motion_.position.x += 2 * int(target.speed()) * factor * facing_;
                break;
            }
            case FighterOp::AssistSwap:
                if (fighterOwner) {
                    if (!unsigned(event.command)) {
                        savedOwner_ = fighterOwner->motion().position;
                        savedTarget_ = target.motion().position;
                    } else {
                        target.place(savedOwner_);
                        fighterOwner->place(savedTarget_);
                    }
                    motion_.position = fighterOwner->motion().position;
                }
                break;
            case FighterOp::TimelineValue:
                assistJudge_ = event.x != 0;
                break;
            case FighterOp::AttackerHurtBind:
                if (assistJudge_) {
                    hurtBinding_ = event;
                    if (target.clip_ == MoveClip::Hurt) target.motion_.applyGravity = false;
                }
                break;
            case FighterOp::AttackerHoming:
                homing_ = event;
                break;
            case FighterOp::AttackerRemoveAtSide:
                removeAtSide_ = true;
                break;
            case FighterOp::AttackerRandomGoto: {
                const unsigned choices[]{event.target, unsigned(event.x), unsigned(event.y)};
                phase_ = choices[unsigned(randomUnit() * 3)];
                playing_ = true;
                jumped = true;
                break;
            }
            case FighterOp::AttackerRandomMove:
                motion_.setVelocity({(randomUnit() > .5 ? double(event.target) : event.x) * facing_, event.y});
                break;
            case FighterOp::TargetHealthPower:
                strikePower_ = {event.target, target.maxHealth() * (event.x / 10000.0)};
                hitModel_->hits[event.target].customPower = strikePower_.power;
                break;
            case FighterOp::AttackerCapture:
                if (target.wasHitBy(asset_->strike(event.target).key)) holdsTarget_ = true;
                break;
            case FighterOp::AttackerMiss:
                if (!holdsTarget_) active_ = false;
                break;
            case FighterOp::AttackerRelease:
                holdsTarget_ = false;
                break;
            case FighterOp::AttackerRecoverOwner:
                if (fighterOwner && fighterOwner->performing(FighterCommand::SuperAir) && !fighterOwner->defeated()) {
                    fighterOwner->recover();
                }
                break;
            case FighterOp::AttackerWaterEnergy: {
                const auto water = MoveClip(unsigned(event.y));
                if (!fighterOwner || (fighterOwner->clip_ != MoveClip(event.target) &&
                    !(fighterOwner->clip_ == water && (fighterOwner->phase_ == 0 || fighterOwner->waterWalkClip_ == water)))) {
                    active_ = false;
                } else if (opponentsTarget) opponentsTarget->useEnergy(event.x);
                break;
            }
            case FighterOp::AttackerQiCap:
                ownerQiCap_.reset();
                if (unsigned(event.command) && fighterOwner) ownerQiCap_ = fighterOwner->qi();
                break;
            case FighterOp::AttackerPushOut: {
                const auto regions = asset_->hits(frame());
                const auto region = std::ranges::find_if(regions, [&](const auto& hit) {
                    return hit.probe() && hit.strikeIndex == event.target;
                });
                if (region == regions.end()) break;
                // The authored wall uses x + wallBody.x even when the attacker faces left.
                const auto wall = worldBox(region->box, motion_.position, 1);
                auto point = target.motion().position;
                if (point.x > wall.left && point.x < wall.right && point.y > wall.top && point.y < wall.bottom) {
                    point.x = point.x > (wall.left + wall.right) * .5 ? wall.right : wall.left;
                    target.place(point);
                }
                break;
            }
            case FighterOp::AttackerWall:
                wall_.reset();
                if (event.target != FinishClip && fighterOwner) {
                    const bool right = fighterOwner->facing() > 0;
                    wall_ = WorldBox{right ? event.x : -event.y, -double(event.target),
                                     right ? event.y : -event.x, double(unsigned(event.command))};
                }
                break;
            case FighterOp::AttackerAlignHitY:
                if (target.wasHitBy(asset_->strike(event.target).key) || target.wasHitBy(asset_->strike(unsigned(event.x)).key)) {
                    target.place({target.motion().position.x, motion_.position.y});
                    if (unsigned(event.command) && fighterOwner) fighterOwner->setTargetHidden(true);
                }
                break;
            case FighterOp::AttackerFollow:
                followsTarget_ = event.x != 0;
                break;
            case FighterOp::AttackerStopTracking:
                tracksTarget_ = false;
                break;
            case FighterOp::AttackerShowTarget:
                if (fighterOwner) fighterOwner->setTargetHidden(false);
                break;
            case FighterOp::AttackerBehind:
                behindFighters_ = true;
                swapsTargetLayer_ = unsigned(event.command) != 0;
                if(paintHook_.change)paintHook_.change(paintHook_.context,paintHook_.key,swapsTargetLayer_,sourceLayerTarget_?sourceLayerTarget_:target.effectTarget());
                break;
            case FighterOp::AttackerStopTargetAction:
                stopsTargetAction_ = event.x != 0;
                break;
            case FighterOp::Hittable:
                target.hittable_ = event.x != 0;
                break;
            case FighterOp::AttackerAllowTargetGuard:
                if (target.clip_ == MoveClip::Idle || target.clip_ == MoveClip::Walk) {
                    target.bindings_[static_cast<unsigned>(FighterCommand::Guard)] = target.asset().command(FighterCommand::Guard);
                }
                break;
            case FighterOp::TargetVelocity:
                target.motion_.setVelocity({event.x, event.y});
                break;
            case FighterOp::TargetDamping:
                target.motion_.setDamping({event.x, event.y});
                break;
            case FighterOp::Fire:
            case FighterOp::Summon:
                spawn({event.op, event.target, motion_.position, facing_, asset_, powerRate_, strikePower_,
                    FinishClip, 0, false, hitModel_,false});
                break;
            case FighterOp::ControlAttacker: {
                const auto mode = unsigned(event.command);
                if (fighterOwner) spawn({.op = event.op, .id = event.target, .origin = motion_.position,
                    .facing = facing_, .asset = asset_, .jumpFrame = mode == 1 ? FinishClip : unsigned(event.x),
                    .playImmediately = mode == 2});
                break;
            }
            case FighterOp::MoveToTarget: {
                const auto axes = unsigned(event.command);
                const auto point = target.motion().position;
                if (axes & 1) motion_.position.x = point.x + event.x * facing_;
                if (axes & 2) motion_.position.y = point.y + event.y;
                if (axes & 4) facing_ = motion_.position.x < point.x ? 1 : -1;
                break;
            }
            case FighterOp::Cross:
                motion_.allowCrossSide = motion_.allowCrossBottom = event.x != 0;
                break;
            case FighterOp::StageCue:
                if (fighterOwner && unsigned(event.command) == 3) effects.ground = fighterOwner->rushCue(event.x != 0);
                break;
            case FighterOp::Shine:
                effects.addShine(std::uint32_t(event.x));
                break;
            case FighterOp::Shadow:
                if (!shadowLifetime_) shadowLifetime_ = std::make_shared<std::byte>();
                shadowActive_ = true;
                shadowTint_ = {int(event.x), int(event.y), std::int16_t(event.target)};
                break;
            case FighterOp::EndShadow:
                shadowActive_ = false;
                break;
            case FighterOp::WalkSound:
                ++effects.walkSounds;
                break;
            case FighterOp::Shake:
                effects.shakeY = event.x;
                effects.shakeTimeMs = event.y;
                break;
            case FighterOp::Slow:
                effects.slowTimeMs = unsigned(event.x);
                break;
            default: break;
            }
            if(wasActive!=active_&&emissionHook_.changed)emissionHook_.changed(emissionHook_.context);
            flush();
            if (jumped) break;
        }
        if (!jumped) return;
    }
}

std::optional<StrikeContact> FightAttacker::strikeAgainst(Point target, WorldBox targetBody) const {
    if (!striking_ || targetBody.empty() || !inStrikeRange(motion_.position, target)) return {};
    for (const FighterHit& hit : asset_->hits(frame())) {
        if (hit.strike() && worldBox(hit.box, motion_.position, facing_).overlaps(targetBody)) {
            const auto& value = hitModel_->hits[hit.strikeIndex];
            return StrikeContact{value.strike, facing_, motion_.position.x, value.damageRate, value.customPower,
                std::shared_ptr<const RuntimeHit>(hitModel_, &value),true,
                worldBox(hit.box,motion_.position,facing_).intersection(targetBody),StrikeOwnerKind::Attacker};
        }
    }
    return {};
}

} // namespace bvn
