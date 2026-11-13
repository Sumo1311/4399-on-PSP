// Adapted from 5DPLAY Game Studio's MusouCtrl and EnemyFighterAICtrl.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fighter_cpu.hpp"
#include "musou_campaign.hpp"
#include "combat_dispatch.hpp"
#include "source_game_delivery.hpp"
#include "source_musou_presentation.hpp"
#include "source_round_controllers.hpp"
#include "source_render_callbacks.hpp"
#include "source_combo.hpp"

#include <list>
#include <string_view>

namespace bvn {

struct MusouMember {
    unsigned level = 1, experience = 0;

    static unsigned levelUpExp(unsigned level);
    bool earn(unsigned amount);
};

struct MusouFighterBank {
    std::string_view name;
    std::span<const FighterAsset> forms;
    unsigned firstForm = 0, baseHp = FighterMaxHealth;
    unsigned faction = 2;
    std::optional<unsigned> rosterIndex = std::nullopt;
};

class MusouEnemyBrain {
public:
    FighterControls step(const FighterMotion& actor, const FighterMotion& target, bool animate, std::uint32_t& random);
    void animate(const FighterMotion& actor,const FighterMotion& target,std::uint32_t& random,bool source=false);
    FighterControls sourceControls(const FighterMotion& actor,const FighterMotion& target)const;
private:
    int followTimer_ = 0;
    unsigned attackTimer_ = 0, distance_ = 0;
    bool following_ = false, attack_ = false, special_ = false;
    int direction_ = 0, jump_ = 0;
};

struct MusouOpponent {
    unsigned id;
    MusouSpawn spawn;
    FighterMotion motion;
    MusouEnemyBrain brain;
    bool boss;
    unsigned fade = 0;
    const MusouFighterBank* bank = nullptr;
    std::optional<FighterCpu> bossAI;
    bool bossKoShown = false;
    bool deadHandled = false;
    SourceEnemyFade sourceFade{};
    unsigned displayOpacity()const{return motion.opacity();}
};

struct MusouBossCue {
    enum class Kind { None, Entrance, Knockout };
    static constexpr unsigned EntranceTicks = (175 - 79) * LogicHz / AnimationHz;
    static constexpr unsigned KnockoutDelay = LogicHz;
    static constexpr unsigned KnockoutTicks = KnockoutDelay + (247 - 184) * LogicHz / AnimationHz;
    static constexpr unsigned entranceTicks(unsigned hz){return (175-79)*hz/AnimationHz;}
    static constexpr unsigned knockoutTicks(unsigned hz){return hz+(247-184)*hz/AnimationHz;}
    Kind kind = Kind::None;
    unsigned actor = 0, ticks = 0;
    bool spoke = false;
    bool super = false;
};

class MusouBattle {
public:
    bool start(const MusouCampaign& campaign, unsigned mission, const StageAsset& stage,
               const std::array<MusouFighterBank, 3>& team, std::span<const MusouFighterBank> enemies,
               MusouProgress& progress, std::array<MusouMember, 3>& members, std::uint32_t seed);
    bool step(FighterControls controls, bool switchPressed);
    const FighterMotion& player() const { return party_[leader_]; }
    const FighterMotion& member(unsigned i) const { return party_[i]; }
    unsigned leader() const { return leader_; }
    const std::list<MusouOpponent>& enemies() const { return enemies_; }
    std::span<const FightObject> objects() const { return objects_; }
    const MusouWaveRun& wave() const { return wave_; }
    unsigned kills() const { return kills_; }
    unsigned spawned() const { return nextId_ - 3; }
    unsigned ticks() const { return ticks_; }
    CameraView camera() const { return camera_.view(); }
    SceneEffects scene() const;
    bool hidesEnemy(unsigned id) const;
    bool hidesPlayer() const;
    const MusouOpponent* boss() const;
    const MusouBossCue& bossCue() const { return bossCue_; }
    unsigned takeRushSounds();
    unsigned takeGhostStepSounds();
    unsigned takeCounterSounds() { return std::exchange(counterSounds_, 0); }
    unsigned takeWalkSounds() { return std::exchange(walkSounds_, 0); }
    unsigned takeGhostFootSounds() { return std::exchange(ghostFootSounds_, 0); }
    std::vector<std::string_view> takeCombatSounds() { return std::exchange(combatSounds_, {}); }
    bool hitFreeze() const { return freeze_.paused(); }
    unsigned hitFreezeFrames() const { return freeze_.remaining(); }
    bool freezeEnabled() const { return freeze_.enabled(); }
    unsigned emittedEffects() const { return effects_.emitted(); }
    void stopInput(){for(auto& actor:party_)actor.clearInput();for(auto& enemy:enemies_)enemy.motion.clearInput();queuedControls_.clear();queuedSwitch_=false;}
    void setGameHz(unsigned hz,unsigned shineHz=15){gameHz_=hz;effects_.setProfile(hz,shineHz);for(auto& a:party_)a.setGameHz(hz);for(auto& e:enemies_)e.motion.setGameHz(hz);}
    void setControllerProfile(unsigned hz,unsigned shineHz){setGameHz(hz,shineHz);effects_.enableGameClocks();}
    void setSourceProfile(unsigned hz,unsigned shineHz);
    unsigned deliveryHz()const{return deliveryHz_;}
    void pausedDisplayFrame();
    const SourceMusouPresentation& presentation()const{return sourcePresentation_;}
    bool allowUiPause()const{return !sourcePresentation_.starting()&&(!sourcePresentation_.ending()||wave_.result()==MusouResult::TimeOver);}
    unsigned takeSourceSignals(){return sourcePresentation_.takeSignals();}
    void acknowledgeReturn();
    bool leaveReady()const{return sourceLeaveReady_;}
    const SourceTransitionTimeline& closingTransition()const{return sourceTransition_;}
    void manualPause(){if(sourcePhysics_)sourceControl_.pause();}
    void manualResume(){if(sourcePhysics_)sourceControl_.resume();}
    const DisplayAnimationClocks* gameClocks()const{return effects_.gameClocks();}
    const SourceGameControl* sourceControl()const{return sourcePhysics_?&sourceControl_:nullptr;}
    unsigned retainedSourceActors()const{return retainedEnemies_.size();}
    SourceScene& sourceScene(){return sourceScene_;}
    const SourceScene& sourceScene()const{return sourceScene_;}
    const SourceMusouHits& comboHits()const{return sourceHits_;}
    const SourceHitsDisplay& comboUi()const{return sourceHitsUi_;}

private:
    FighterMotion& player() { return party_[leader_]; }
    FighterMotion* owner(unsigned id);
    MusouOpponent* chooseTarget();
    void populateStates();
    void stepFrame(FighterControls controls, bool switchPressed, bool& animate);
    void stepOrderedFrame(FighterControls controls,bool switchPressed,bool& animate);
    void applyController(const SourceControlRequest& request);
    void syncSourceCounters();
    void stepSelectedAnimation();
    void stepSelectedRender();
    void stepSourceBossUi();
    void sourceRoot(FighterControls controls,bool switchPressed,bool uiPaused);
    void stepDetached(bool animate);
    void syncSourceResult();
    bool sourceActions()const;
    struct SourceUiView {MusouBattle& game;void victory(unsigned actor);void koComplete();};
    void stepSourceSession();
    void animateEnemyBrain(MusouOpponent& enemy);
    void trackSourceCamera();
    void updateSourceCameraPoint();
    void resetSourceCamera(bool update=false);
    FighterMotion* sourceActor(const SourceControlTarget& target,unsigned& id);
    std::list<MusouOpponent>::iterator retireEnemy(std::list<MusouOpponent>::iterator entry);
    void pruneRetainedEnemies();
    void stepSource(FighterControls controls,bool switchPressed);
    void syncSourceSlow(double rate);
    void spawn(MusouSpawn spawn);
    void initializeSourceScene();
    void bindSourcePaint(FightAttacker&,SourceScene::Key);
    void syncSourceObjects();
    void pruneSourceObjects();
    FightObject* sourceObject(SourceScene::Key);
    bool stepSourceScene(FighterControls,bool,FighterMotion*);
    void sourceContactPair(SourceScene::Key,SourceScene::Key,bool);
    void bindSourceHook(FighterMotion& actor,unsigned id);
    void sourceActorDied(unsigned id);
    void sourceActorEvent(unsigned id,SourceFighterEvent event);
    void sourceComboHit(unsigned actor,unsigned target,const StrikeContact& contact);
    void changeMember();
    void updateMember(unsigned slot, bool refill);
    void recoverMembers();
    void updateEnemies();
    void fadeEnemies();
    void stepBossCue(bool animate);
    bool beginBossKo();
    void playContact(const ContactEffectRequest& request);
    void stepOptical();
    void play(FighterEffects effect);
    void collectSpawns(FighterMotion& actor, unsigned id, Point target);
    void addObjects(std::span<const ObjectSpawn> spawns, unsigned owner, Point target);
    void advanceObjects(bool animate, double rate, FighterMotion* primary);
    void contacts(bool hits=true);
    void contactObjects();
    void reward(const MusouOpponent& enemy);
    StrikeContact powered(StrikeContact contact, unsigned owner) const;

    const MusouCampaign* campaign_ = nullptr;
    const StageAsset* stage_ = nullptr;
    MusouProgress* progress_ = nullptr;
    std::array<MusouMember, 3>* members_ = nullptr;
    std::vector<const MusouFighterBank*> banks_;
    std::vector<FighterMotion> party_;
    std::list<MusouOpponent> enemies_;
    std::list<MusouOpponent> retainedEnemies_;
    std::vector<MusouEnemyState> states_;
    std::vector<FightObject> objects_;
    SourceScene sourceScene_{};
    std::vector<SourceScene::Key> sourceObjectKeys_{};
    unsigned sourceNextObject_=0;
    std::vector<ObjectSpawn> spawnedObjects_;
    MusouWaveRun wave_;
    FightCamera camera_;
    FightEffects effects_;
    CombatFreeze freeze_;
    std::vector<std::string_view> combatSounds_;
    MusouBossCue bossCue_;
    unsigned leader_ = 0, target_ = 0, nextId_ = 3, ticks_ = 0, enemyLevel_ = 1;
    unsigned kills_ = 0, switchTicks_ = 0, healTicks_ = 0, slowTicks_ = 0, rushSounds_ = 0, ghostStepSounds_ = 0;
    unsigned slowPeriod_ = 3;
    unsigned counterSounds_ = 0;
    unsigned walkSounds_ = 0, ghostFootSounds_ = 0;
    std::vector<ShadowSource> shadowSources_{};
    std::uint32_t random_ = 1;
    unsigned gameHz_=30;
    unsigned deliveryHz_=LogicHz;
    bool sourcePhysics_=false,queuedSwitch_=false;
    SourceGameControl sourceControl_{};
    SourceMusouHits sourceHits_{};
    SourceHitsDisplay sourceHitsUi_{};
    SourceControlTarget sourceCameraFocus_{};
    Point sourceCameraLast_{};
    std::array<SourceControlTarget,2> sourceCameraBase_{};
    std::array<Point,2> sourceCameraBaseLast_{};
    unsigned sourceCameraCount_=0;
    FighterControls sourceFrameControls_{};
    SourceMusouPresentation sourcePresentation_{};
    SourceRenderCallbacks sourceCallbacks_{};
    SourceTransitionTimeline sourceTransition_{};
    bool sourceReturnClosing_=false,sourceLeaveReady_=false;
    unsigned sourceResultRevision_=0,sourceReturnSerial_=0;
    AnimationPhase sourceUiPhase_{};
    SourceGameDelivery delivery_{};
    QueuedFighterControls queuedControls_{};
    bool selfDieShown_ = false;
    std::vector<OpticalSprite> opticalSprites_{};
};

} // namespace bvn
