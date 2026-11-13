// Adapted from 5DPLAY Game Studio's GameRoundCtrl, GameStartCtrl and GameEndCtrl.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fight_match.hpp"
#include "source_game_delivery.hpp"
#include "source_round_controllers.hpp"
#include "source_render_callbacks.hpp"
#include "source_quick_transition.hpp"
#include "source_hud.hpp"

namespace bvn {

enum class RoundPhase {
    IntroFocus, IntroPose, IntroPan, IntroSkip, Ready, Playing,
    KnockoutFlash, Knockout, KnockoutFall, TimeOver, Draw, WinnerDelay, Victory, Finished, TeamChange
};
enum class BattleMode { Versus, Training, TeamVersus, Arcade, TeamArcade, Musou, Wlan, Watch, TeamWatch, Credits, Settings };

constexpr bool isTeamMode(BattleMode mode) {
    return mode == BattleMode::TeamVersus || mode == BattleMode::TeamArcade || mode == BattleMode::TeamWatch;
}

constexpr bool isWatchMode(BattleMode mode) {
    return mode == BattleMode::Watch || mode == BattleMode::TeamWatch;
}

constexpr bool isArcadeMode(BattleMode mode) {
    return mode == BattleMode::Arcade || mode == BattleMode::TeamArcade;
}

inline constexpr unsigned TeamSize = 3;
using TeamFactions = std::array<std::array<unsigned, TeamSize>, 2>;

struct RoundRules {
    std::array<unsigned, 2> members{TeamSize, TeamSize};
    double maxHealth = FighterMaxHealth;
    double enemyAttackRate = 1;
    bool classicScore = true;
    bool sourceQuality = false;
    unsigned gameHz = 30,shineHz=15;
};

struct TeamEntrant {
    std::span<const FighterAsset> forms{};
    unsigned form = 0;
};

struct TrainingLine {
    std::array<FighterInputEvent, 8> commands{};
    unsigned count = 0;
};

class TrainingHistory {
public:
    std::span<const TrainingLine> lines() const { return std::span(lines_).first(count_); }
    void step(FighterMotion& fighter,unsigned deliveryHz=LogicHz);

private:
    std::array<TrainingLine, 10> lines_{};
    unsigned count_ = 0, idleTicks_ = 0;
    bool comboOpen_ = false;
};

class VersusRound {
public:
    static constexpr int DefaultSeconds = 60;
    static constexpr unsigned WinsRequired = 2;

    VersusRound(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second,
                int seconds = DefaultSeconds);
    VersusRound(const StageAsset& stage, std::span<const FighterAsset> first, std::span<const FighterAsset> second,
                int seconds = DefaultSeconds, std::array<unsigned, 2> startingForms = {},
                BattleMode mode = BattleMode::Versus, std::array<AssisterChoice, 2> assisters = {},
                TeamFactions factions = {}, RoundRules rules = {});
    bool step(const std::array<FighterControls, 2>& controls, bool skip = false);

    const FightMatch& match() const { return match_; }
    void setGameHz(unsigned hz) {
        for (unsigned side = 0; side < 2; ++side) match_.actor(side).setGameHz(hz);
    }
    void seedObjects(std::uint32_t seed) { match_.seedObjects(seed); }
    void bindCpu(unsigned side, FighterCpu* cpu) { match_.bindCpu(side, cpu); }
    void stopInput() { match_.stopInput();for(auto& input:queuedControls_)input.clear();queuedSkip_=false; }
    unsigned deliveryHz()const{return rules_.sourceQuality?rules_.gameHz:LogicHz;}
    void pausedDisplayFrame();
    void updateUiClock(std::uint64_t microseconds){sourceExplicitUiClock_=true;sourceUiWallMs_=microseconds*.001;}
    const SourceQuickTransition& quickTransition()const{return sourceQuick_;}
    unsigned sourceRootFrames()const{return sourceRootFrames_;}
    bool sourceTransitionVisible()const{return sourceTransition_.visible();}
    void manualPause(bool pauseUi=false){
        if(pauseUi&&rules_.sourceQuality&&(sourceStart_.active()||sourceEnd_.active())){sourceDeferredPause_=true;return;}
        match_.manualPause();
    }
    void manualResume(){sourceDeferredPause_=false;sourcePauseReady_=false;match_.manualResume();}
    bool takeDeferredPause(){return std::exchange(sourcePauseReady_,false);}
    const SourceFightHud& hud()const{return sourceHud_;}
    const SourceStartController& sourceStart()const{return sourceStart_;}
    const SourceEndController& sourceEnd()const{return sourceEnd_;}
    unsigned sourceTransitionFrame()const{return sourceTransition_.frame();}
    bool sourceTransitionRendering()const{return sourceTransition_.rendering();}
    bool sourceDeferredPause()const{return sourceDeferredPause_;}
    std::optional<bool> takeDashSound(unsigned side) { return match_.actor(side).takeDashSound(); }
    unsigned takeRushSounds() { return match_.takeRushSounds(); }
    unsigned takeGhostStepSounds() { return match_.takeGhostStepSounds(); }
    unsigned takeCounterSounds() { return match_.takeCounterSounds(); }
    std::vector<std::string_view> takeCombatSounds(){return match_.takeCombatSounds();}
    unsigned takeWalkSounds() { return match_.takeWalkSounds(); }
    unsigned takeGhostFootSounds() { return match_.takeGhostFootSounds(); }
    RoundPhase phase() const { return phase_; }
    unsigned phaseTicks() const { return phaseTicks_; }
    unsigned ticks() const { return ticks_; }
    unsigned roundTimeFrame()const{return clockFrames_;}
    unsigned sourceMainFrames()const{return sourceMainFrames_;}
    unsigned round() const { return round_; }
    int seconds() const { return seconds_; }
    int displayedSeconds()const{return rules_.sourceQuality?sourceUiSeconds_:seconds_;}
    int winner() const { return winner_; }
    unsigned wins(unsigned side) const { return wins_[side]; }
    bool training() const { return mode_ == BattleMode::Training; }
    bool team() const { return isTeamMode(mode_); }
    bool perfect() const { return perfect_; }
    unsigned member(unsigned side) const { return members_[side]; }
    unsigned memberCount(unsigned side) const { return rules_.members[side]; }
    std::optional<unsigned> entering(unsigned side) const;
    bool enterTeamMembers(const std::array<TeamEntrant, 2>& fighters);
    std::span<const TrainingLine> trainingHistory(unsigned side) const { return history_[side].lines(); }
    std::optional<unsigned> bannerFrame() const;

private:
    void stepSource(const std::array<FighterControls,2>& controls,bool skip);
    void stepOrderedSource(const std::array<FighterControls,2>& controls,bool skip);
    struct SourceView {
        VersusRound& owner;
        void focus(int side);void intro(unsigned side);void idle();
        void transitionOut(bool notify);void quickTransition();void transitionIn();
        void startBanner();void endBanner();void clearBanner();void win();
        void fadeIn(){owner.sourceHud_.show();}void fadeOut(){owner.sourceHud_.hide();}
    };
    void sourceStartRound(bool next=false);
    void sourceGameCallback(const std::array<FighterControls,2>& controls,bool skip);
    void sourceRootCallback(const std::array<FighterControls,2>& controls,bool skip,bool uiPaused);
    void sourceStageUi();
    void sourceBeforeMain(bool animate);
    void sourceAfterMain(bool animate);
    void sourceDeath(unsigned loser);
    void sourceGates(bool enabled){match_.sourceActionEnable(enabled);match_.allowLoseHp(enabled);}
    unsigned ticksPerAnimation()const{return deliveryHz()/AnimationHz;}
    void resetActors(bool keepGauges);
    void setPhase(RoundPhase phase);
    void endRound(int winner, bool timeout);
    void nextRound();
    void nextTeamRound();
    void startTeamRound(unsigned introMask);
    void showWinner();

    const StageAsset& stage_;
    FightMatch match_;
    const int secondsPerRound_;
    const BattleMode mode_;
    const TeamFactions factions_;
    const RoundRules rules_;
    int seconds_, winner_ = -1;
    unsigned ticks_ = 0, phaseTicks_ = 0, round_ = 1, introSide_ = 0, clockFrames_ = 0;
    unsigned winnerDelay_ = 0;
    unsigned introMask_ = 3, incoming_ = 0;
    unsigned survivorHealth_ = FighterMaxHealth;
    std::optional<unsigned> lastLoserFaction_;
    unsigned lastLoserQi_ = 0;
    bool perfect_ = false;
    std::array<unsigned, 2> wins_{};
    std::array<unsigned, 2> members_{};
    std::array<TrainingHistory, 2> history_{};
    SourceGameDelivery delivery_{};
    std::array<QueuedFighterControls,2> queuedControls_{};
    bool queuedSkip_=false;
    AnimationPhase sourceUiClock_{};
    unsigned sourceUiFrames_=0,sourceMainPhaseTicks_=0;
    unsigned sourceMainFrames_=0;
    int sourceUiSeconds_=-1;
    bool sourceUiComplete_=false,sourceEndPending_=false,sourceSkip_=false;
    enum : unsigned { GameCallback=1,TransitionCallback=2 };
    SourceRenderCallbacks sourceCallbacks_{};
    unsigned sourceRootFrames_=0;
    bool sourceMainDispatched_=false;
    SourceFightHud sourceHud_{};
    SourceQuickTransition sourceQuick_{};
    unsigned sourceDisplayFrames_=0;
    double sourceUiWallMs_=0;
    bool sourceExplicitUiClock_=false;
    SourceStartController sourceStart_{};
    SourceEndController sourceEnd_{};
    SourceBannerTimeline sourceBanner_{};
    SourceTransitionTimeline sourceTransition_{};
    bool sourceDeferredPause_=false,sourcePauseReady_=false;
    bool sourceRenderEnd_=false,sourcePlayOver_=false,sourceSlowStarted_=false;
    unsigned sourceFlyTick_=0;
    RoundPhase phase_ = RoundPhase::IntroFocus;
};

} // namespace bvn
