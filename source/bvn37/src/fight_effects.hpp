// Adapted from 5DPLAY Game Studio's EffectShakeHandler and ShineEffectView.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_paint.hpp"
#include "map_blur.hpp"
#include "map_optical.hpp"
#include "game_animation_clock.hpp"

#include "stage_motion.hpp"
#include "combat_effects.hpp"
#include "combat_effect_art.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <memory>
#include <optional>
#include <vector>

namespace bvn {

struct FighterEffects;
class FighterAsset;
struct FighterPose;
struct ShineRequest {
    std::uint32_t color;
    unsigned percent;
};
struct ShineView {
    std::uint32_t color;
    unsigned alpha;
};

struct ShadowTint {
    int red = 0, green = 0, blue = 0;
    bool operator==(const ShadowTint&) const = default;
};

// Stable display state survives an actor container relocation or removal while
// source EffectViews still retain their target. Coordinates/state are refreshed
// by the owning actor before EffectCtrl's render phase.
struct EffectTarget {
    Point position{};
    int facing=1,state=0;
    bool steel=false,superSteel=false;
    ShadowTint baseTint{},tint{};
    void resumeColor(){tint=baseTint;}
};

// The same original FighterBuffVO is retained when an active buff is renewed.
// Finished nodes survive while old EffectViews still refer to them.
struct BuffState {
    double value = 0;
    unsigned frames = 0;
    bool finished = true;
};

struct BuffEffectRequest {
    const CombatEffect* burst;
    const CombatEffect* aura;
    Point origin;
    std::shared_ptr<EffectTarget> target;
    std::shared_ptr<BuffState> buff;
    int facing = 1;
};

struct ShadowSource {
    const void* id;
    const FighterAsset* asset;
    const FighterPose* pose;
    Point position;
    int facing;
    bool active;
    ShadowTint tint{0, 0, 255};
    std::shared_ptr<const void> lifetime{};
};

struct ShadowStamp {
    const FighterAsset* asset;
    const FighterPose* pose;
    Point position;
    int facing;
    double alpha = .8;
    std::uint64_t serial = 0;
    ShadowTint tint{0, 0, 255};
};

enum class ReactionCue : std::uint8_t { Replace, ReplaceSmoke, ExplodeCharge, ExplodeBurst, GhostFoot, Count };
struct ReactionCueView {
    ReactionCue cue = ReactionCue::Count;
    Point origin{};
    unsigned age = 0;
    int facing = 1;
    unsigned frame() const { return age ? age - 1 : 0; }
};

enum class GroundCue : std::uint8_t { Jump, AirJump, Land, AssistBleach, AssistNaruto, Rush, AirRush, Count };
struct GroundCueView {
    GroundCue cue = GroundCue::Count;
    Point origin{};
    unsigned frame = 0;
    int facing = 1;
    bool sound = false;
};

struct CombatEffectView {
    const CombatEffect* effect;
    const CombatArt* art;
    Point origin;
    int facing;
    unsigned rotation=0,age=0;
    std::shared_ptr<const void> identity{};
    std::shared_ptr<EffectTarget> target{};
    std::shared_ptr<BuffState> buff{};
    unsigned cursor=0,displayFrame=0;
    int offsetFacing=1;
    bool finished=false,active=false;
    bool background=false;
    int holdFrame=-1;
    unsigned paintId=0;
    unsigned frame() const { return displayFrame; }
};

struct SceneEffects {
    float shiftY = 0;
    std::uint32_t shineColor = 0;
    unsigned shineAlpha = 0;
    unsigned shadeAlpha = 0;
    bool hideMap = false;
    const FighterAsset* portraitAsset = nullptr;
    const FighterPose* portrait = nullptr;
    unsigned portraitFrame = 0;
    bool portraitRight = false;
    std::span<const GroundCueView> ground{};
    std::uint32_t backdropColor = 0;
    std::span<const ReactionCueView> reactions{};
    std::span<const ShadowStamp> shadows{};
    std::span<const ShineView> shines{};
    std::span<const CombatEffectView> combat{};
    float shiftX = 0;
    std::span<const CombatEffectView> backgrounds{};
    bool fillBackdrop=false;
    const SourcePaint* paint=nullptr;
    MapBlurView blur{};
    OpticalView optical{};
};

class FightEffects {
public:
    void sourcePaint(SourcePaint* value){paint_=value;}
    void blackBack(bool value){if(paint_){const auto key=SourcePaint::key(SourcePaint::Kind::BlackBack);if(value)paint_->add(SourcePaint::Layer::Stage,key,0);else paint_->remove(key);}}
    struct FrameCallbacks {
        void* context=nullptr;
        void(*animation)(void*)=nullptr;
        void(*render)(void*)=nullptr;
        void(*afterAnimation)(void*)=nullptr;
    };
    void mapVisible(bool value){scene_.hideMap=!value;}
    void blur(double x,double y,int time=1000){const auto before=blur_.requests();blur_.request(x,y,time);captureBlur(before);}
    void blurEnabled(bool value){blur_.enabled(value);}
    void freezeBlur(unsigned milliseconds){const auto before=blur_.requests();blur_.freeze(milliseconds);captureBlur(before);}
    bool slow(double rate,int time=1000){
        const auto before=blur_.requests();const bool accepted=blur_.slow(rate,time);
        if(accepted&&gameClocks_)gameClocks_->slow(rate,time);
        captureBlur(before);return accepted;
    }
    void resumeSlow(){blur_.resumeSlow();if(gameClocks_)gameClocks_->resume();}
    MapOpticalState& opticalState(){return optical_;}
    const MapOpticalState& opticalState()const{return optical_;}
    void background(const CombatEffect& effect,double hold=-1);
    void buff(const BuffEffectRequest& request);
    void setProfile(unsigned gameHz,unsigned shineHz){sourceProfile_=true;gameHz_=gameHz;shineHz_=shineHz;blur_.gameHz(gameHz);}
    // Explicitly clocked probes keep supplying their own animation signal.
    // The playable game selects the original controller-owned phases instead.
    void enableGameClocks(unsigned deliveryHz=LogicHz){gameClocks_.emplace();gameClocks_->initialize(gameHz_,deliveryHz);}
    void deliveryHz(unsigned hz){deliveryHz_=hz;effectCountPhase_=shineLogic_=0;}
    bool controllerClocks()const{return gameClocks_.has_value();}
    bool effectAnimation(bool supplied){return gameClocks_?gameClocks_->beginFrame():supplied;}
    bool actorAnimation(bool supplied){return gameClocks_?gameClocks_->actorFrame():supplied;}
    bool nextActorAnimation(bool supplied)const{return gameClocks_?gameClocks_->nextActorFrame():supplied;}
    const DisplayAnimationClocks* gameClocks()const{return gameClocks_?&*gameClocks_:nullptr;}
    struct ControllerTiming {
        DisplayAnimationClocks clocks;MapBlurState blur;
        unsigned effectPhase,shinePhase;
    };
    std::optional<ControllerTiming> controllerTiming()const{
        if(!gameClocks_)return {};return ControllerTiming{*gameClocks_,blur_,effectCountPhase_,shineLogic_};
    }
    void restoreControllerTiming(const ControllerTiming& value){
        gameClocks_=value.clocks;blur_=value.blur;effectCountPhase_=value.effectPhase;shineLogic_=value.shinePhase;
    }
    double motionRate()const{const double rate=blur_.view().slowRate;return rate>0?1/rate:1;}
    void play(FighterEffects requests);
    bool emit(const CombatEffect& effect,Point origin,int facing,std::shared_ptr<const void> identity={});
    void setLastTarget(const std::shared_ptr<EffectTarget>& target);
    void special(const CombatEffect& effect,const std::shared_ptr<EffectTarget>& target);
    void freezeOver();
    unsigned emitted() const { return emitted_; }
    void seedCosmetics(std::uint32_t seed){effectRandom_=seed?seed:1;}
    void step(bool animate, std::span<const ShadowSource> sources = {},
              std::optional<std::uint32_t> visualTimeMs = {});
    void stepOrdered(bool animate,std::span<const ShadowSource> sources,FrameCallbacks callbacks,
                     std::optional<std::uint32_t> visualTimeMs = {});
    SceneEffects scene() const {
        auto view = scene_;
        view.ground = ground_;
        view.reactions = reactions_;
        view.shadows = shadowViews_;
        view.shines = shineViews_;
        view.combat = combat_;
        view.backgrounds = backgrounds_;
        view.blur = blur_.view();
        view.optical = optical_.view();
        view.paint=paint_;
        return view;
    }

private:
    SourcePaint* paint_=nullptr;
    unsigned paintSerial_=0;
    SceneEffects scene_{};
    MapBlurState blur_{};
    MapOpticalState optical_{};
    std::optional<DisplayAnimationClocks> gameClocks_{};
    void captureBlur(unsigned before){if(before!=blur_.requests()){const auto b=blur_.view();optical_.capture(b.x,b.y);}}
    std::array<GroundCueView, 16> ground_{};
    std::array<ReactionCueView, 8> reactions_{};
    struct ShakeAxis {
        int hold = 0, power = 0, loss = 0, direction = 1;
        unsigned frame = 0;
        void pulse(float requested);
        void animate(float& offset);
    } shakeX_, shakeY_;
    unsigned shineFrame_ = 0;
    unsigned shinePercent_ = 0;
    bool sourceProfile_=false;
    unsigned gameHz_=60,deliveryHz_=LogicHz,shineHz_=20,shineLogic_=0;
    struct ShineEmitter {
        std::uint32_t color;
        unsigned percent, gap, frameTimeMs, loss, frame = 0;
        std::uint32_t startTimeMs = 0;
    };
    std::vector<ShineEmitter> shines_;
    std::vector<ShineView> shineViews_;
    void updateShineViews();
    void stepShines(std::optional<std::uint32_t> visualTimeMs);
    void stepFrame(bool animate,std::span<const ShadowSource> sources,
                   std::optional<std::uint32_t> visualTimeMs,FrameCallbacks callbacks);
    std::uint64_t visualTicks_ = 0;
    struct ShadowEmitter {
        const void* id;
        unsigned frame = 0;
        std::vector<ShadowStamp> stamps{};
        std::shared_ptr<const void> lifetime{};
    };
    std::vector<ShadowEmitter> shadows_{};
    std::vector<ShadowStamp> shadowViews_{};
    std::uint64_t shadowSerial_ = 0;
    std::vector<CombatEffectView> combat_{};
    std::vector<CombatEffectView> backgrounds_{};
    struct ViewCache {
        const CombatEffect* effect;
        std::weak_ptr<const void> identity;
        bool global;
        std::shared_ptr<CombatEffectView> view;
    };
    std::vector<ViewCache> viewCache_{};
    std::vector<std::shared_ptr<CombatEffectView>> activeCombat_{},freezeCallbacks_{};
    std::shared_ptr<CombatEffectView> lastCombat_{};
    std::shared_ptr<CombatEffectView> startCombat(const CombatEffect& effect,Point origin,int facing,
                                               std::shared_ptr<const void> identity);
    void updateCombatViews();
    struct FrameCount { const CombatEffect* effect; std::shared_ptr<const void> identity; unsigned count; };
    std::vector<FrameCount> effectCounts_{};
    unsigned effectCountPhase_=0;
    std::uint32_t effectRandom_=0x6d2b79f5U;
    unsigned emitted_=0;
};

} // namespace bvn
