// Adapted from 5DPLAY Game Studio's FighterAttacker controller.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fighter_motion.hpp"

#include <vector>

namespace bvn {

class FightAttacker {
public:
    struct PaintHook {void* context=nullptr;unsigned key=0;void(*change)(void*,unsigned,bool,const SourceControlTarget&)=nullptr;};
    void sourcePaintHook(PaintHook value){paintHook_=value;}
    struct EmissionHook {
        void* context=nullptr;
        void(*effects)(void*,FighterEffects&&)=nullptr;
        void(*spawn)(void*,unsigned,const ObjectSpawn&,const FighterMotion*)=nullptr;
        void(*changed)(void*)=nullptr;
    };
    void sourceEmissionHook(EmissionHook value){emissionHook_=value;}
    void sourceHz(unsigned hz){motion_.sourceHz(hz);}
    void sourceRecoil(double x){motion_.setSecondaryVelocity({x,0},{.3,0});}
    void speedRate(double rate){motion_.speed=motion_.defaultSpeed()*rate;}
    FightAttacker(const FighterAsset& asset, unsigned owner, ObjectSpawn spawn, Point target = {},
                  std::uint32_t seed = 1);

    FighterEffects step(const StageGeometry& stage, bool animate, std::optional<CameraLimits> limits,
                        FighterMotion& target, double rate, std::vector<ObjectSpawn>& shots,
                       FighterMotion* fighterOwner = nullptr, FighterMotion* opponentsTarget = nullptr);
    std::optional<StrikeContact> strikeAgainst(Point target, WorldBox body) const;
    std::optional<ObjectSpawn> applyFrameListeners(FighterMotion& owner, FighterMotion& opponent,
                                                  FighterMotion* opponentsTarget, bool animate);
    const FighterAsset& asset() const { return *asset_; }
    const AttackerAsset& definition() const { return *def_; }
    unsigned owner() const { return owner_; }
    std::uint32_t key() const { return def_->key; }
    Point position() const { return motion_.position; }
    Point velocity() const { return motion_.velocity; }
    int facing() const { return facing_; }
    unsigned phase() const { return phase_; }
    void applyControl(const ObjectSpawn& spawn);
    void remove() { active_ = false; }
    bool referenced(unsigned slot, unsigned requester) const {
        for (const auto& ref : references_) if (ref.requester == requester) return slot < 32 && (ref.slots & (1U << slot));
        return false;
    }
    void clearReference(unsigned slot, unsigned requester) {
        if (slot >= 32) return;
        for (auto& ref : references_) if (ref.requester == requester) ref.slots &= ~(1U << slot);
    }
    void setReference(unsigned slot, unsigned requester) {
        if (slot >= 32) return;
        for (auto& ref : references_) if (ref.requester == requester) { ref.slots |= 1U << slot; return; }
        references_.push_back({requester,1U << slot});
    }
    bool active() const { return active_; }
    bool attacking() const { return active_ && striking_; }
    bool finished() const { return !active_ && !hurtBinding_; }
    bool behindFighters() const { return behindFighters_; }
    bool swapsTargetLayer() const { return swapsTargetLayer_; }
    bool assister() const { return asset_->assister() && def_->clip == MoveClip::Idle; }
    unsigned cutin() const { return cutin_; }
    const SourceControlTarget& controllerTarget()const{return controllerTarget_;}
    bool departureEffect() const { return departureEffect_; }
    bool holdsTarget() const { return active_ && holdsTarget_; }
    const FighterPose& pose() const { return frame().pose; }
    OpticalSprite opticalArea()const{
        auto rect=asset_->objectArea(unsigned(def_-&asset_->attacker(0)));
        if(!rect)return {{},false,!active_};
        // BaseGameSprite translates its constructor bounds without reflecting direct.
        rect->x+=motion_.position.x;rect->y+=motion_.position.y;
        return {*rect,true,!active_};
    }
    bool shadowing() const { return active_ && shadowActive_; }
    ShadowTint shadowTint() const { return shadowTint_; }
    const std::shared_ptr<const void>& shadowLifetime() const { return shadowLifetime_; }

private:
    const FighterFrame& frame() const { return asset_->frame(def_->clip, phase_); }
    void applyFrameEvents(FighterMotion& target, FighterEffects& effects, std::vector<ObjectSpawn>& shots,
                          FighterMotion* fighterOwner, FighterMotion* opponentsTarget);
    bool overlapsProbe(WorldBox body, unsigned probe) const;
    double randomUnit();

    const FighterAsset* asset_;
    const AttackerAsset* def_;
    unsigned owner_;
    Motion motion_;
    double powerRate_;
    StrikePower strikePower_;
    std::shared_ptr<RuntimeHitModel> hitModel_;
    std::optional<double> ownerQiCap_;
    std::optional<WorldBox> wall_;
    std::optional<FighterEvent> homing_, hurtBinding_;
    Point savedOwner_, savedTarget_;
    std::uint32_t random_ = 1;
    int facing_;
    unsigned phase_;
    unsigned landing_ = FinishClip, queuedFrame_ = FinishClip;
    unsigned probeFrame_ = FinishClip, probeId_ = 0;
    unsigned cutin_ = 0;
    SourceControlTarget controllerTarget_{};
    SourceControlTarget sourceLayerTarget_{};
    PaintHook paintHook_{};
    EmissionHook emissionHook_{};
    // Authored saved-controller variables belong to the requesting fighter,
    // even when the named attacker was created by another member of its team.
    struct Reference { unsigned requester, slots; };
    std::vector<Reference> references_;
    bool entered_ = false;
    bool touchingFloor_ = false;
    bool playing_ = true;
    bool active_ = true;
    bool striking_ = true, departureEffect_ = true;
    bool holdsTarget_ = false;
    bool followsTarget_ = false;
    bool tracksTarget_ = true;
    bool behindFighters_ = false, swapsTargetLayer_ = false;
    bool stopsTargetAction_ = false;
    bool removeAtSide_ = false, assistJudge_ = false;
    bool shadowActive_ = false;
    ShadowTint shadowTint_{};
    std::shared_ptr<const void> shadowLifetime_{};
};

} // namespace bvn
