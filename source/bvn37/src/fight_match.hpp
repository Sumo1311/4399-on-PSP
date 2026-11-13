// Adapted from 5DPLAY Game Studio's GameMainLogicCtrl.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fight_camera.hpp"
#include "fight_effects.hpp"
#include "fight_attacker.hpp"
#include "fight_projectile.hpp"
#include "combat_dispatch.hpp"
#include "source_combo.hpp"
#include "source_scene.hpp"

#include <variant>

namespace bvn {

class FighterCpu;

using FightObject = std::variant<FightProjectile, FightAttacker>;
inline bool objectFinished(const FightObject& object) {
    if (const auto* attacker = std::get_if<FightAttacker>(&object)) return attacker->finished();
    return !std::get<FightProjectile>(object).active();
}
enum class Contacts { Training, Round, Locked };
struct AssisterChoice {
    const FighterAsset* asset = nullptr;
    unsigned faction = 0;
};

class FightMatch {
public:
    struct SourceFrameHooks {
        void* context=nullptr;
        void (*stage)(void*)=nullptr;
        void (*beforeMain)(void*,bool)=nullptr;
        void (*afterMain)(void*,bool)=nullptr;
        void (*death)(void*,unsigned)=nullptr;
    };
    void sourceFrameHooks(SourceFrameHooks hooks){sourceHooks_=hooks;bindSourceHooks();}
    void sourceActionEnable(bool value){sourceActionsEnabled_=value;for(auto& actor:actors_)actor.sourceActionEnable(value);}
    bool sourceActionsEnabled()const{return sourceActionsEnabled_;}
    void allowLoseHp(bool value){for(auto& actor:actors_)actor.allowLoseHp(value);}
    static constexpr unsigned ObjectLimit = 64;
    static constexpr unsigned classicScore(unsigned raw) { return raw * 4 / 5; }
    static constexpr unsigned modeScore(unsigned raw,bool classic){return classic?classicScore(raw):raw;}
    void setClassicScore(bool value){classicScore_=value;}
    void sourceArcadeScoring(bool value){sourceArcadeScoring_=value;}
    void advanceComboUi(){for(auto& ui:sourceHitsUi_)ui.render();}
    const SourceHitsDisplay& comboUi(unsigned side)const{return sourceHitsUi_[side];}
    const SourceGameHits& comboHits()const{return sourceHits_;}
    void setEffectProfile(unsigned gameHz,unsigned shineHz){effectGameHz_=gameHz;effects_.setProfile(gameHz,shineHz);}
    void setControllerProfile(unsigned gameHz,unsigned shineHz){setEffectProfile(gameHz,shineHz);effects_.enableGameClocks();}
    void setSourceProfile(unsigned hz,unsigned shineHz,const StageAsset& stage);
    SourceScene& sourceScene(){return sourceScene_;}
    const SourceScene& sourceScene()const{return sourceScene_;}
    const DisplayAnimationClocks* gameClocks()const{return effects_.gameClocks();}
    std::optional<FightEffects::ControllerTiming> controllerTiming()const{return effects_.controllerTiming();}
    void restoreControllerTiming(const FightEffects::ControllerTiming& value){effects_.restoreControllerTiming(value);}
    const SourceGameControl* sourceControl()const{return sourcePhysics_?&sourceControl_:nullptr;}
    void manualPause(){if(sourcePhysics_)sourceControl_.pause();}
    void manualResume(){if(sourcePhysics_)sourceControl_.resume();}

    FightMatch(std::span<const FighterAsset> first, std::span<const FighterAsset> second,
               std::array<unsigned, 2> startingForms = {}, std::array<AssisterChoice, 2> assisters = {})
        : actors_{FighterMotion(first, startingForms[0]), FighterMotion(second, startingForms[1])},
          assisters_(assisters) {
        objects_.reserve(ObjectLimit);
    }
    FightMatch(const FighterAsset& first, const FighterAsset& second) : FightMatch(std::span(&first, 1), std::span(&second, 1)) {}

    void reset(const StageAsset& stage, Point first, Point second, bool keepGauges = false);
    void step(const StageGeometry& stage, const std::array<FighterControls, 2>& controls, bool animate,
              Contacts contacts = Contacts::Training);
    void stepMainFrame(const StageGeometry& stage,const std::array<FighterControls,2>& controls,Contacts contacts){stepFrame(stage,controls,false,contacts);}
    void stepDetached(bool animate);
    bool roundClockTick(bool animate) const;
    bool cinematic() const { return counterSide().has_value() || cutinSide().has_value() || assistCutin(); }
    const AssisterChoice& assister(unsigned side) const { return assisters_[side]; }
    unsigned assistSounds() const { return assistSounds_; }
    unsigned takeRushSounds();
    unsigned takeGhostStepSounds();
    unsigned takeCounterSounds() { return std::exchange(counterSounds_, 0); }
    unsigned takeWalkSounds() { return std::exchange(walkSounds_, 0); }
    unsigned takeGhostFootSounds() { return std::exchange(ghostFootSounds_, 0); }
    std::vector<std::string_view> takeCombatSounds(){return std::exchange(combatSounds_,{});}
    bool hitFreeze() const;
    unsigned hitFreezeFrames() const { return freeze_.remaining(); }
    unsigned emittedEffects() const { return effects_.emitted(); }
    unsigned pointsThisStep() const { return pointsThisStep_; }
    void seedObjects(std::uint32_t seed) { objectRandom_ = seed ? seed : 1; }
    bool hidesTarget(unsigned owner) const;
    void focus(std::optional<unsigned> side) { focus_ = side;sourceCameraFocus_.reset();if(side)camera_.focusOne();else camera_.resumeFocus(); }
    void stepScene(bool animate);
    void flashKnockout(unsigned loser);
    void resumeKnockout() { freeze_.resume();sourceControl_.resume();effects_.blurEnabled(true);effects_.slow(2,0);syncSourceSlow(2); }
    void resumeKnockoutSpeed(){effects_.resumeSlow();slowTicks_=slowFrame_=0;syncSourceSlow(0);}
    bool superKnockout() const { return superKnockout_; }
    void stopInput();
    void bindCpu(unsigned side, FighterCpu* cpu) { cpus_[side] = cpu; }
    unsigned aiAnimation(unsigned side) const { return aiAnimations_[side]; }
    FighterMotion& actor(unsigned i) { return actors_[i]; }
    const FighterMotion& actor(unsigned i) const { return actors_[i]; }
    std::span<const FightObject> objects() const { return objects_; }
    std::span<const OpticalSprite> opticalSprites()const{return opticalSprites_;}
    CameraView camera() const { return camera_.view(); }
    SceneEffects scene() const;

private:
    void initializeSourceScene();
    void syncSourceObjects();
    void pruneSourceObjects();
    FightObject* sourceObject(SourceScene::Key key);
    bool stepSourceScene(const StageGeometry&,const std::array<FighterControls,2>&,bool,Contacts,std::optional<CameraLimits>);
    void sourceContactPair(SourceScene::Key,SourceScene::Key,bool);
    void bindSourceHooks();
    void sourceComboEvent(unsigned target,SourceFighterEvent event);
    void sourceComboHit(unsigned actor,unsigned target,const StrikeContact& contact);
    void stepFrame(const StageGeometry& stage, const std::array<FighterControls, 2>& controls, bool animate,
                   Contacts contacts);
    bool capturesTarget(unsigned owner) const;
    std::optional<unsigned> cutinSide() const;
    std::optional<unsigned> counterSide() const;
    const FightAttacker* assistCutin() const;
    void playAssistEffect(unsigned side, Point position);
    void addObjects(std::span<const ObjectSpawn> spawns, unsigned owner);
    void advanceAttacker(FightAttacker& attacker, const StageGeometry& stage, bool animate,
                         std::optional<CameraLimits> limits, double rate, std::vector<ObjectSpawn>& shots);
    void play(FighterEffects requests);
    void playContact(const ContactEffectRequest& request);
    void stepOptical();
    void syncSourceSlow(double rate);
    void stepSelectedAnimation();
    void stepSelectedRender();
    void applyController(const SourceControlRequest& request);
    void bindSourcePaint(FightAttacker&,SourceScene::Key);
    std::uint32_t nextObjectSeed();

    FighterMotion actors_[2];
    std::array<AssisterChoice, 2> assisters_;
    std::array<std::shared_ptr<RuntimeHitModel>, 2> assistHits_;
    std::vector<FightObject> objects_;
    SourceScene sourceScene_{};
    std::vector<SourceScene::Key> sourceObjectKeys_{};
    unsigned sourceNextObject_=0;
    FightEffects effects_;
    FightCamera camera_;
    std::optional<unsigned> focus_;
    SourceControlTarget sourceCameraFocus_{};
    Point sourceCameraLast_{};
    void updateSourceCameraPoint();
    unsigned slowTicks_ = 0, slowFrame_ = 0, shade_ = 0, assistSounds_ = 0;
    unsigned pointsThisStep_ = 0, rushSounds_ = 0, ghostStepSounds_ = 0;
    unsigned counterSounds_ = 0;
    unsigned walkSounds_ = 0, ghostFootSounds_ = 0;
    std::vector<ShadowSource> shadowSources_{};
    std::uint32_t objectRandom_ = 1;
    std::array<FighterCpu*, 2> cpus_{};
    std::array<unsigned, 2> aiAnimations_{};
    bool classicScore_=true;
    bool sourceArcadeScoring_=true;
    SourceGameHits sourceHits_{};
    std::array<SourceHitsDisplay,2> sourceHitsUi_{};
    unsigned effectGameHz_=60;
    unsigned deliveryHz_=LogicHz;
    bool sourcePhysics_=false;
    bool sourceActionsEnabled_=true;
    SourceFrameHooks sourceHooks_{};
    SourceGameControl sourceControl_{};
    StageGeometry sourceStage_{};
    std::array<FighterControls,2> sourceFrameControls_{};
    CombatFreeze freeze_;
    bool effectAnimate_=false;
    bool superKnockout_=false;
    std::vector<std::string_view> combatSounds_{};
    const MapOpticalAsset* opticalDefinition_=nullptr;
    std::vector<OpticalSprite> opticalSprites_{};
};

} // namespace bvn
