// Adapted from 5DPLAY Game Studio's FighterMC and ground/air controllers.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_game_control.hpp"

#include "fight_collision.hpp"
#include "fight_effects.hpp"
#include "fighter_ai_policy.hpp"
#include "source_hurt_timeline.hpp"
#include "source_fighter_events.hpp"
#include "source_hit_control.hpp"
#include "combat_effects.hpp"

#include <memory>
#include <vector>

namespace bvn {

inline constexpr unsigned FighterMaxHealth = 1000;
inline constexpr unsigned FighterMaxQi = 300;
inline constexpr unsigned AssistChargeMax = 500;

struct FighterControls {
    int horizontal = 0, vertical = 0;
    bool jumpPressed = false, attackPressed = false, dashPressed = false, attackHeld = false, specialPressed = false;
    bool superPressed = false;
    bool transformHeld = false;
    bool assistHeld = false;
    bool aiControlled = false;
    std::uint32_t aiIntents = 0;
    bool sourceHuman = false, classicInput = false, upHeld = false, downHeld = false, quickJump = false;
    unsigned heldKeys=0,pressedKeys=0,bufferedKeys=0;
};

struct FighterInputEvent {
    FighterCommand command;
    int horizontal = 0;
    bool drop = false;
    bool assist = false;
    bool highlight = false;
    unsigned ghost = 0; // Source SL (1/3) and WL (2), distinct from an ordinary dash.
};

enum class FighterBuff : std::uint8_t { Speed, Attack, Defense, Count };

// HitVOs remain shared with attached attackers; bullets clone them when fired.
// A hurt chain retains these objects, including repeated references to one hit.
struct CombatHitCache {
    std::array<const CombatEffect*,2> effects{};
    std::array<bool,2> enemy{};
    CombatHitCache()=default;
    // EffectManager keys its dictionaries by HitVO identity; a cloned bullet
    // receives a fresh identity rather than inheriting the owner's cache entry.
    CombatHitCache(const CombatHitCache&){}
    CombatHitCache& operator=(const CombatHitCache&){effects={};enemy={};return *this;}
};
struct RuntimeHit {
    FighterStrike strike{};
    double powerRate = -1;
    double damageRate = 1;
    double customPower = -1;
    mutable CombatHitCache effects{};

    double power() const { return customPower >= 0 ? customPower : strike.damage; }
    int damage() const { return int(power() * damageRate); }
};

struct RuntimeHitModel {
    std::vector<RuntimeHit> hits;
};

inline std::shared_ptr<RuntimeHitModel> makeRuntimeHitModel(const FighterAsset& asset, double rate = 1) {
    auto model = std::make_shared<RuntimeHitModel>();
    for (const auto& strike : asset.strikes()) model->hits.push_back({strike, -1, rate});
    return model;
}

enum class StrikeOwnerKind : unsigned {Fighter,Bullet,Attacker};
struct StrikeRecoil {void* context=nullptr;void(*apply)(void*,double)=nullptr;};
struct StrikeContact {
    FighterStrike strike;
    int facing;
    double originX;
    double powerRate = 1;
    double customPower = -1;
    std::shared_ptr<const RuntimeHit> runtime{};
    bool hasOwner = true;
    WorldBox overlap{};
    StrikeOwnerKind ownerKind=StrikeOwnerKind::Fighter;
    StrikeRecoil recoil{};
    bool reversePush=true,hasHitRect=true;

    double power() const { return customPower >= 0 ? customPower : strike.damage; }
    int damage() const { return int(power() * powerRate); }
};

enum class ContactEffectKind : std::uint8_t { Hurt, Guard, Steel, GuardBreak };
struct ContactEffectRequest {
    const CombatEffect* effect;
    ContactEffectKind kind;
    Point origin;
    int facing;
    std::shared_ptr<const RuntimeHit> identity;
    std::shared_ptr<EffectTarget> target{};
    // Hit promotion occurs in EffectManager; defense promotion occurs later in
    // EffectHitHandler. Both reach EffectCtrl with the authored pow mapped to Y.
    double shakeX() const {
        return kind==ContactEffectKind::Guard&&effect->shakePower?0:effect->shakeX;
    }
    double shakeY() const {
        return (kind==ContactEffectKind::Hurt||kind==ContactEffectKind::Guard)&&effect->shakePower?
            effect->shakePower:effect->shakeY;
    }
};

struct FighterEffects {
    float shakeY = 0, shakeTimeMs = 0;
    std::optional<std::uint32_t> shine;
    unsigned slowTimeMs = 0;
    std::optional<float> shakeLoopY{};
    std::optional<GroundCueView> ground{};
    unsigned ghostStepSound = 0;
    std::optional<ReactionCueView> reaction{};
    unsigned counterSounds = 0;
    unsigned walkSounds = 0, ghostFootSounds = 0;
    std::vector<ReactionCueView> reactions{};
    std::vector<ShineRequest> shines{};
    std::vector<ContactEffectRequest> contactEffects{};
    float shakeX = 0;
    std::optional<float> shakeLoopX{};
    bool shakeRequested = false;
    bool startShake = false;
    std::vector<SourceControlRequest> controllerRequests{};
    bool empty() const {
        return !shakeY&&!shakeTimeMs&&!shine&&!slowTimeMs&&!shakeLoopY&&!ground&&!ghostStepSound&&
            !reaction&&!counterSounds&&!walkSounds&&!ghostFootSounds&&reactions.empty()&&shines.empty()&&
            contactEffects.empty()&&!shakeX&&!shakeLoopX&&!shakeRequested&&!startShake&&controllerRequests.empty();
    }
    void addShine(std::uint32_t color,unsigned percent) {
        shine = color;
        shines.push_back({color,percent});
    }
    void addShine(std::uint32_t color) { addShine(color,color==0xffffff?30U:20U); }
};

enum class CounterKind : std::uint8_t { None, Replace, Explode };
enum class GhostKind : std::uint8_t { None, Step, Jump, Down };

struct FighterCutin {
    CutinKind kind = CutinKind::None;
    MoveClip face = MoveClip::Missing;
    unsigned frame = 1;
    const FighterAsset* asset = nullptr;
};

struct StrikePower {
    unsigned strike = FinishClip;
    double power = -1;

    double forStrike(unsigned index) const { return index == strike ? power : -1; }
};

struct ObjectSpawn {
    FighterOp op;
    unsigned id;
    Point origin;
    int facing;
    const FighterAsset* asset;
    double powerRate = 1;
    StrikePower strikePower{};
    unsigned jumpFrame = FinishClip;
    unsigned referenceSlot = 0;
    bool playImmediately = false;
    std::shared_ptr<RuntimeHitModel> hitModel{};
    bool sourceMainOwner = true;
};

class FighterMotion {
public:
    explicit FighterMotion(std::span<const FighterAsset> forms, unsigned initialForm = 0)
        : forms_(forms), initialForm_(initialForm), form_(initialForm), baseFaces_(initialForm == 0) {}
    explicit FighterMotion(const FighterAsset& asset) : FighterMotion(std::span(&asset, 1)) {}

    void reset(Point spawn, int facing, bool keepGauges = false);
    void startTraining();
    void updateTraining();
    std::optional<FighterInputEvent> takeTrainingInput();
    bool standing() const { return clip_ == MoveClip::Idle; }
    bool hidesMap() const { return mapBackdrop_; }
    std::uint32_t backdropColor() const { return mapBackdrop_ ? backdropColor_ : 0; }
    bool canCallAssist() const;
    void spendAssist() { assistCharge_ = 0; }
    void recordAssist();
    void step(const StageGeometry& stage, FighterControls input, bool animate,
              std::optional<CameraLimits> camera = std::nullopt, FighterMotion* target = nullptr, double rate = 1,
              std::span<const std::uint32_t> attackers = {}, bool suspendGravity = false,bool selectedRender=false);
    void stepCutin(FighterMotion* target, bool animate, const StageGeometry& stage,
                   std::optional<CameraLimits> camera = std::nullopt, double rate = 1,
                   std::span<const std::uint32_t> attackers = {});
    void stepSourceCutinAnimation(FighterMotion* target, bool animate, const StageGeometry& stage,
                                 std::span<const std::uint32_t> attackers = {});
    void stepSourceCutinRender(const StageGeometry& stage,std::optional<CameraLimits> camera,double rate,
                              FighterControls input={},FighterMotion* target=nullptr,std::span<const std::uint32_t> attackers={});
    void faceIdleTarget(const FighterMotion& target);
    void resolveBodyContact(FighterMotion& other);
    std::optional<StrikeContact> strikeAgainst(const FighterMotion& defender) const;
    std::optional<StrikeContact> strikeAgainst(Point target, WorldBox body) const;
    WorldBox body() const {
        const auto result=worldBox(frame().body,motion_.position,facing_);
        // FighterMain.getArea retains the shared last non-null body rectangle.
        if(!frame().body.empty()){opticalBody_=result;opticalBodyKnown_=true;}
        return result;
    }
    WorldBox opticalArea()const{if(!opticalBodyKnown_)(void)body();return opticalBody_;}
    WorldBox hurtBody() const;
    bool receiveStrike(const StrikeContact& contact, FighterMotion* attacker = nullptr);
    void scoreStrike(const StrikeContact& contact,FighterMotion* target=nullptr);
    void scoreObjectStrike(const StrikeContact& contact,double rate,FighterMotion* target=nullptr);
    void noteStrike(const StrikeContact& contact) { lastBisha_ = contact.strike.bisha(); }
    bool lastBisha() const { return lastBisha_; }
    void present(FighterCommand command);
    void cancelIntro();
    void clearInput();
    bool performing(FighterCommand command) const { return clip_ == asset().command(command); }
    bool deathComplete()const{return sourcePhysics_?sourceHurt_.dead():knockedDown();}
    const SourceHurtTimeline& sourceHurt()const{return sourceHurt_;}
    struct SourceHooks {
        void* context=nullptr;unsigned id=0;
        void(*die)(void*,unsigned)=nullptr;
        void(*effects)(void*,FighterEffects&&)=nullptr;
        void(*event)(void*,unsigned,SourceFighterEvent)=nullptr;
        void(*hitTarget)(void*,unsigned,unsigned,const StrikeContact&)=nullptr;
        void(*spawn)(void*,unsigned,const ObjectSpawn&,const FighterMotion*)=nullptr;
        bool(*attacker)(void*,unsigned,std::uint32_t)=nullptr;
        void(*buff)(void*,const BuffEffectRequest&)=nullptr;
    };
    void sourceHooks(SourceHooks value){sourceHooks_=value;}
    void die();
    bool alive()const{return sourcePhysics_?sourceAlive_:!defeated();}
    unsigned dieEvents()const{return sourceDieEvents_;}
    unsigned sourceEvents(SourceFighterEvent event)const{return sourceEvents_[unsigned(event)];}
    void hurtRandomState(std::uint32_t& state){sourceHurtRandom_=&state;}
    void setDefense(double value){flatDefense_=value;}
    bool sourceNonboss()const{return musouEnemyEffects_;}
    void sourceRecoil(double x){motion_.setSecondaryVelocity({x,0},{.3,0});}
    unsigned hitGap()const{return hitGap_;}
    bool defenseHitting()const{return guardHit_;}
    int defenseHold()const{return guardHold_;}
    bool knockedDown() const {
        if(sourcePhysics_)return defeated()&&(sourceHurt_.state()==AiState::Down||sourceHurt_.dead());
        return defeated() && (flight_ == Flight::Down ||
            (clip_ == MoveClip::Hurt && !hurtFrames_ && !motion_.airborne));
    }
    bool acting() const { return acting_; }
    bool casting() const { return casting_; }
    bool movingTarget() const { return targetMove_.has_value(); }
    bool hidesTarget() const { return targetHidden_; }
    void setTargetHidden(bool hidden) { targetHidden_ = hidden; }
    bool wasHitBy(std::uint32_t strike) const { return hurtStrike_ == strike; }
    FighterCutin cutin() const { return cutin_; }
    bool hasAction(FighterCommand command) const;
    AiState aiState() const;
    MoveClip aiAction(FighterCommand command) const;
    AiHitTraits aiHitTraits(FighterCommand command) const;
    bool aiHittable() const { return hittable_ && !dodging_ && !transforming_; }
    bool hurtEscaping() const { return hurtEscaping_; }
    unsigned hurtEscapes() const { return hurtEscapes_; }
    bool counterPaused() const { return counterPaused_; }
    CounterKind counterKind() const { return counterKind_; }
    unsigned counterStarts() const { return counterStarts_; }
    unsigned counterHitFrames() const { return explodeHitFrames_; }
    unsigned counterSteelFrames() const { return explodeSteelFrames_; }
    unsigned counterReplaceFrames() const { return replaceSkillFrames_; }
    unsigned counterGameFrame() const { return counterGameFrame_; }
    bool overloaded() const { return overloaded_; }
    GhostKind ghostKind() const { return ghostKind_; }
    bool ghostActive() const { return ghostActive_; }
    int ghostFrames() const { return ghostFrames_; }
    unsigned ghostStarts() const { return ghostStarts_; }
    unsigned ghostEnds() const { return ghostEnds_; }
    void finishGhostAnimation(FighterMotion* target);
    int remainingJumps() const { return jumpsLeft_; }
    bool blueShadow() const { return shadowActive_ && shadowTint_ == ShadowTint{0, 0, 255}; }
    bool shadowing() const { return shadowActive_; }
    ShadowTint shadowTint() const { return shadowTint_; }
    const std::shared_ptr<const void>& shadowLifetime() const { return shadowLifetime_; }
    void setFaction(unsigned faction) { faction_ = faction; }
    void setDefenseType(int type) { defenseType_ = type; }
    unsigned defenseType() const { return defenseType_ < 0 ? asset().defenseType() : unsigned(defenseType_); }
    void setMusouEnemyEffects(bool enemy) { musouEnemyEffects_ = enemy; }
    bool stepCounterPause();
    void syncSourceCounter(const SourceCounterState& state);
    int currentHurtDamage() const;
    bool hurtBreakHit() const;
    void setGameHz(unsigned hz) { gameHz_ = hz == 30 ? 30 : 60; }
    void orderedSourceControl(){sourceControlOrder_=true;}
    void sourceHz(unsigned hz){setGameHz(hz);deliveryHz_=gameHz_;sourcePhysics_=true;motion_.sourceHz(gameHz_);}
    unsigned deliveryHz()const{return deliveryHz_;}
    void speedRate(double rate){motion_.speed=motion_.defaultSpeed()*rate;}
    unsigned gameHz()const{return gameHz_;}
    FighterEffects takeEffects();
    std::optional<bool> takeDashSound();
    std::span<const ObjectSpawn> takeSpawns();
    const FighterAsset& asset() const { return forms_[form_]; }
    unsigned form() const { return form_; }
    bool transforming() const { return transforming_; }
    const Motion& motion() const { return motion_; }
    void place(Point position) { motion_.position = position; }
    void setFacing(int direction) { facing_ = direction; }
    const FighterPose& pose() const;
    const FighterPose* screenPose() const;
    int facing() const { return facing_; }
    unsigned opacity() const { return sourcePhysics_?unsigned(sourceDisplayAlpha_*255+.5):(opacity_ * 255 + 50) / 100; }
    double displayAlpha()const{return sourcePhysics_?sourceDisplayAlpha_:opacity_*.01;}
    void displayAlpha(double value){sourceDisplayAlpha_=std::clamp(value,0.,1.);}
    double health() const { return health_; }
    double maxHealth() const { return maxHealth_; }
    double speed() const { return asset().stats().speed + buffValue(FighterBuff::Speed); }
    double qi() const { return qi_; }
    void setHealth(double value);
    void allowLoseHp(bool value){allowLoseHp_=value;}
    bool allowLoseHp()const{return allowLoseHp_;}
    void sourceActionEnable(bool value){sourceActionEnabled_=value;}
    bool sourceActionEnabled()const{return sourceActionEnabled_;}
    void setRoundStats(double healthMaximum, double attackRate) {
        maxHealth_ = healthMaximum;
        attackRate_ = attackRate;
        updateHitRates();
    }
    void applyBuff(FighterBuff kind, double value, unsigned frames);
    std::vector<BuffEffectRequest> takeBuffEffects() { return std::exchange(buffEffects_, {}); }
    const std::shared_ptr<BuffState>& buffState(FighterBuff kind) const { return buffs_[static_cast<unsigned>(kind)]; }
    void setQi(double value);
    double energy() const { return energy_; }
    double maxQi() const { return maxQi_; }
    double maxEnergy() const { return maxEnergy_; }
    void setGaugeLimits(double qi, double energy);
    void setEnergy(double value);
    unsigned assistCharge() const { return assistCharge_; }
    bool steel() const { return steel_; }
    bool superSteel()const{return superSteel_;}
    void setSteelBody(bool value,bool super=false){steel_=value;superSteel_=value&&super;}
    std::shared_ptr<EffectTarget> effectTarget() const;
    ShadowTint visualTint() const { return effectTarget_->tint; }
    bool guarding() const { return guardReady_; }
    bool guardClip() const { return clip_ == asset().command(FighterCommand::Guard); }
    bool dodging() const { return dodging_; }
    unsigned phase() const { return phase_; }
    unsigned timelineFrame() const;

private:
    friend class FightAttacker;
    enum class Flight { None, Start, Air, Contact, Bounce, Down, Recover };
    struct SelectedAction {
        MoveClip clip = MoveClip::Missing;
        FighterCommand command = FighterCommand::Count;
        unsigned qiCost = 0;
        explicit operator bool() const { return clip != MoveClip::Missing; }
    };
    using StrikeReaction = RuntimeHit;

    void enter(MoveClip clip);
    void recover(MoveClip ground=MoveClip::Idle);
    MoveClip recoveryClip(MoveClip ground = MoveClip::Idle);
    void startAction(MoveClip clip, bool casting = false);
    void prepareAction(bool casting);
    void startGuard(bool keepProtection = false);
    void dropThrough();
    void launchJump();
    SelectedAction chooseCatch(FighterControls input) const;
    SelectedAction chooseAttack(FighterControls input) const;
    SelectedAction chooseSpecial(FighterControls input) const;
    SelectedAction chooseSuper(FighterControls input) const;
    MoveClip chooseDash(FighterControls input) const;
    SelectedAction chooseTransformation(FighterControls input) const;
    void stepAiControls(FighterControls input);
    void executeAiCommand(FighterCommand command,int horizontal=0);
    void recordTrainingInput(FighterCommand command, int horizontal = 0, bool drop = false, bool highlight = false);
    MoveClip switchForm(std::uint16_t encoded, bool playIntro = true);
    void animate(bool holdingGuard);
    void animateGuard(bool held);
    void advancePose();
    void advanceFlight();
    void applyFrameEvents();
    const StrikeReaction* strikeReaction(unsigned index) const;
    double strikePowerRate(const StrikeReaction* reaction) const;
    void setStrikeReaction(unsigned index, unsigned property, float value);
    std::shared_ptr<RuntimeHitModel> runtimeHitModel() const;
    void updateHitRates();
    void rememberHurtHit(const StrikeContact& contact);
    bool stepHurtEscape(FighterControls input);
    void startHurtEscape();
    bool stepCounterRequest(FighterControls input, const StageGeometry& stage,
                            std::optional<CameraLimits> camera, double rate);
    void stepCounterAnimate();
    void stepGhostControls(FighterControls input, unsigned humanRequest, bool airBranch);
    bool startGhost(GhostKind kind);
    void animateGhost();
    void endGhost();
    void startShadow(ShadowTint tint);
    bool passes(const FighterEvent& event) const;
    MoveClip applyEvent(const FighterEvent& event);
    MoveClip selfLaunch(double x, double y);
    void checkProbe();
    WorldBox displayBounds() const;
    GroundCueView rushCue(bool sound) const;
    void clearActions();
    void stopShake();
    void addQi(double value);
    void useEnergy(double value);
    void regenerateEnergy();
    bool receiveGuardHit(const StrikeContact& contact);
    void contactEffect(const StrikeContact& contact,ContactEffectKind kind);
    double buffValue(FighterBuff kind) const {
        const auto& buff = buffs_[static_cast<unsigned>(kind)];
        return buff && !buff->finished ? buff->value : 0;
    }
    double powerRate() const { return attackRate_ + buffValue(FighterBuff::Attack); }
    double defenseRate() const;
    bool defeated() const { return sourcePhysics_?!sourceAlive_:!training_&&health_<=0; }
    bool injured() const {if(sourcePhysics_)return hurtEscaping_||sourceHurt_.hurting()||sourceHurt_.flying();return hurtEscaping_ || flight_ != Flight::None || clip_ == MoveClip::Hurt || clip_ == MoveClip::HurtRecover; }
    bool guardRecovery() const { return clip_ == asset().command(FighterCommand::GuardRecover); }
    const FighterFrame& frame() const { return asset().frame(clip_, phase_); }
    void updateTargetMove();
    void checkComboLimits();
    void stepKamui();
    void tickBuffs();
    unsigned actionBufferTicks()const{return deliveryHz_/10;}
    void stepHinata();
    void stepWaterWalk(int horizontal);

    struct TargetMove {
        std::uint16_t anchor;
        bool limited;
        Point speed;
    };

    std::span<const FighterAsset> forms_;
    unsigned initialForm_ = 0;
    unsigned form_ = 0;
    bool baseFaces_;
    Motion motion_;
    MoveClip clip_ = MoveClip::Idle;
    int facing_ = 1;
    unsigned phase_ = 0;
    unsigned autoFacingFrame_ = 0;
    unsigned jumpDelay_ = 0;
    unsigned attackBuffer_ = 0, specialBuffer_ = 0, superBuffer_ = 0, dashBuffer_ = 0, jumpBuffer_ = 0;
    int jumpsLeft_ = 2;
    unsigned quickJumpDelay_ = 0;
    unsigned normalAirJumpDelay_ = 0;
    bool normalAirJumpReady_ = true;
    int previousSourceMove_ = 0;
    double health_ = FighterMaxHealth, maxHealth_ = FighterMaxHealth, attackRate_ = 1, qi_ = 0, energy_ = 100;
    double maxQi_ = FighterMaxQi, maxEnergy_ = 100;
    std::array<std::shared_ptr<BuffState>, static_cast<unsigned>(FighterBuff::Count)> buffs_{};
    std::vector<BuffEffectRequest> buffEffects_;
    bool training_ = false;
    bool flightMode_ = false;
    bool bankaiIntro_ = false;
    double timelineLoop_ = 0, timelineQiGain_ = 0;
    bool timelineAirFollow_ = false;
    std::array<std::uint32_t, TimelineCounterCount> timelineCounters_{};
    struct ComboLimit { MoveClip clip = MoveClip::Missing; unsigned count = 0; };
    std::array<ComboLimit, 3> comboLimits_{};
    unsigned comboChecks_ = 0;
    mutable std::shared_ptr<RuntimeHitModel> hitModel_;
    struct HurtHit { std::shared_ptr<const RuntimeHit> hit; double rate = 1, powerAdd = 0; };
    std::vector<HurtHit> currentHurts_;
    bool lastBisha_ = false;
    unsigned assistCharge_ = AssistChargeMax;
    unsigned trainingHpDelay_ = 0, trainingQiDelay_ = 0, trainingAssistDelay_ = 0;
    int trainingMove_ = 0;
    std::optional<FighterInputEvent> trainingInput_;
    unsigned hurtFrames_ = 0;
    unsigned hitGap_ = 0;
    unsigned energyDelay_ = 0;
    bool overloaded_ = false;
    int guardDelay_ = 0, guardHold_ = 0, justGuardTicks_ = 0;
    bool guardReady_ = false, guardHit_ = false;
    bool crossBody_ = false, dodging_ = false;
    bool hittable_ = true;
    bool kamui_ = false;
    bool shunko_ = false;
    bool mirroredEffect_ = false;
    unsigned opacity_ = 100;
    double sourceDisplayAlpha_=1;
    bool sourceAlphaRecovery_=false;
    MoveClip waterWalkClip_ = MoveClip::Missing;
    bool waterInput_ = false, waterMoving_ = false;
    unsigned twinLionTicks_ = 0, twinLionCooldown_ = 0, chakraTicks_ = 0;
    std::uint32_t repeatHit_ = 0;
    const AttackerAsset* followAttacker_ = nullptr;
    double followAttackerY_ = 0;
    bool mapBackdrop_ = false;
    std::uint32_t backdropColor_ = 0;
    bool acting_ = false, airMove_ = true, airAttackUsed_ = false;
    bool groundCancels_ = false;
    unsigned walkCancels_ = 0;
    bool steel_ = false, superSteel_ = false;
    bool casting_ = false, transforming_ = false;
    AiState actionState_ = AiState::Normal;
    bool shaking_ = false;
    FighterCutin cutin_{};
    std::uint32_t hurtStrike_ = 0, guardStrike_ = 0;
    MoveClip probeClip_ = MoveClip::Missing, landingClip_ = MoveClip::Missing, hurtClip_ = MoveClip::Missing;
    unsigned probe_ = 0;
    FighterMotion* target_ = nullptr;
    std::optional<TargetMove> targetMove_;
    std::array<std::optional<Point>, 2> counterPositions_;
    bool targetHidden_ = false;
    std::span<const std::uint32_t> attackers_{};
    double stageRight_ = 0;
    std::array<MoveClip, static_cast<std::size_t>(FighterCommand::Count)> bindings_{};
    std::array<unsigned, static_cast<std::size_t>(FighterCommand::Count)> bindingQiCosts_{};
    FighterEffects effects_{};
    std::shared_ptr<EffectTarget> effectTarget_=std::make_shared<EffectTarget>();
    int defenseType_ = -1;
    bool musouEnemyEffects_ = false;
    std::optional<bool> dashSound_;
    std::array<ObjectSpawn, 8> spawns_{};
    unsigned spawnCount_ = 0;
    struct SourceHurtView {
        FighterMotion& actor;
        void pose(SourceHurtTimeline::Pose pose);void advance();void children();void face(int facing);
        void velocity(double x,double y);void velocityY(double y);void damping(double x,double y);
        void impact(unsigned type,double power);void touchFloor();void down();void dead();void recover();
    };
    SourceHurtTimeline::Input sourceHurtInput()const;
    void beginSourceHurt(const StrikeContact& contact);
    bool receiveSourceStrike(const StrikeContact& contact,FighterMotion* attacker);
    struct SourceHitView {
        FighterMotion& actor;const StrikeContact& contact;FighterMotion* attacker;
        bool touchingSide()const;double x()const;double ownerX()const;void reversePush(double x);
        bool alive()const;void alive(bool value);bool checkDie()const;void dieEvent();
        void hurtHit();void guardHit();void loseHp(double value);void energy(double value);double energy()const;double maxEnergy()const;
        bool hittable()const;int state()const;void qi(double value);void cacheHit();void hittable(bool value);void gap(unsigned value);void gravity(bool value);void overload(bool value);void guard(bool value);void guardHitting(bool value);void guardHold(unsigned value);unsigned hz()const;
        void velocity(double x,double y);void damping(double x,double y);void secondary(double x,double y,double dx,double dy);
        void action();void effect(SourceHitResult kind);void hurtAnimate();void breakAnimate(unsigned hold);
    };
    void flushSourceEffects();
    SourceHooks sourceHooks_{};
    void emitSourceEvent(SourceFighterEvent event);
    void emitSourceHitTarget(const StrikeContact& contact,FighterMotion* target);
    std::array<unsigned,unsigned(SourceFighterEvent::Count)> sourceEvents_{};
    bool sourceAlive_=true;
    unsigned sourceDieEvents_=0,sourceGuardResume_=0;
    double flatDefense_=0;
    std::uint32_t hurtRandomLocal_=0x9e3779b9u;
    std::uint32_t* sourceHurtRandom_=nullptr;
    void animateSourceHurt();
    SourceHurtTimeline sourceHurt_{};
    bool sourceReactionPose_=false;
    Flight flight_ = Flight::None;
    int flightFrames_ = 0, reactionFrames_ = 0;
    double lowestY_ = 0, floorHitY_ = 0;
    bool heavyDown_ = false, floorDownPlayed_ = false;
    bool hurtEscaping_ = false, justHurtResume_ = false, pendingHurtEscape_ = false;
    unsigned hurtDownFrame_ = 0, gameHz_ = LogicHz, deliveryHz_=LogicHz, reactionPhase_ = 0;
    bool sourceControlOrder_=false;
    bool allowLoseHp_=true,sourceActionEnabled_=true;
    bool sourcePhysics_=false;
    unsigned pendingHumanPressed_ = 0;
    unsigned hurtEscapes_ = 0;
    CounterKind counterKind_ = CounterKind::None;
    bool counterPaused_ = false;
    unsigned faction_ = 0, counterStarts_ = 0, counterGameFrame_ = 0, counterPausePhase_ = 0;
    unsigned explodeHitFrames_ = 0, explodeSteelFrames_ = 0, replaceSkillFrames_ = 0;
    Point counterOrigin_{};
    WorldBox explodeArea_{};
    std::shared_ptr<RuntimeHit> explodeHit_{};
    GhostKind ghostKind_ = GhostKind::None;
    AiState ghostActionState_ = AiState::Normal;
    bool ghostActive_ = false, shadowActive_ = false, inGhostEffect_ = false;
    ShadowTint shadowTint_{};
    std::shared_ptr<const void> shadowLifetime_{};
    int ghostFrames_ = 0;
    unsigned ghostStarts_ = 0, ghostEnds_ = 0, pendingGhostRequests_ = 0;
    bool ghostAnimationPending_ = false;
    double ghostFloor_ = 0;
    mutable WorldBox opticalBody_{};
    mutable bool opticalBodyKnown_=false;
};

} // namespace bvn
