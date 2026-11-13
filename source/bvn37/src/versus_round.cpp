// Adapted from 5DPLAY Game Studio's GameRoundCtrl, GameStartCtrl and GameEndCtrl.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "versus_round.hpp"

#include <algorithm>

namespace bvn {
namespace {

constexpr unsigned RoundStart = 9, RoundComplete = 87;
constexpr unsigned FinalStart = 94, FinalJump = 139, FightStart = 54;
constexpr unsigned KnockoutStart = 144, KnockoutComplete = 189;
constexpr unsigned TimeOverStart = 219, TimeOverComplete = 262;
constexpr unsigned DrawStart = 266, DrawComplete = 308;

unsigned readyLength(unsigned round) {
    return round < 5 ? RoundComplete - RoundStart
                     : FinalJump - FinalStart + 1 + RoundComplete - FightStart;
}

} // namespace

void TrainingHistory::step(FighterMotion& fighter,unsigned deliveryHz) {
    const auto input = fighter.takeTrainingInput();
    if (!input) {
        if (fighter.standing()) {
            comboOpen_ = false;
            if (count_ && ++idleTicks_ >= deliveryHz) count_ = 0;
        } else {
            idleTicks_ = 0;
        }
        return;
    }
    idleTicks_ = 0;
    const bool action = !input->highlight && (input->assist ||
        (input->command != FighterCommand::Count && input->command != FighterCommand::Guard));
    if (!action || !comboOpen_ || !count_ || lines_[count_ - 1].count == lines_[0].commands.size()) {
        if (count_ == lines_.size()) {
            std::move(lines_.begin() + 1, lines_.end(), lines_.begin());
            --count_;
        }
        lines_[count_++].count = 0;
    }
    auto& line = lines_[count_ - 1];
    line.commands[line.count++] = *input;
    comboOpen_ = action;
}

VersusRound::VersusRound(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second, int seconds)
    : VersusRound(stage, std::span(&first, 1), std::span(&second, 1), seconds) {}

VersusRound::VersusRound(const StageAsset& stage, std::span<const FighterAsset> first,
                         std::span<const FighterAsset> second, int seconds, std::array<unsigned, 2> startingForms,
                         BattleMode mode, std::array<AssisterChoice, 2> assisters, TeamFactions factions, RoundRules rules)
    : stage_(stage), match_(first, second, startingForms, assisters), secondsPerRound_(seconds), mode_(mode), factions_(factions),
      rules_(rules), seconds_(mode == BattleMode::Training ? -1 : seconds) {
    delivery_.initialize(deliveryHz());
    sourceUiClock_.rate(deliveryHz(),AnimationHz,true);
    sourceUiSeconds_=seconds_;
    resetActors(false);
    if (training()) {
        for (unsigned side = 0; side < 2; ++side) match_.actor(side).startTraining();
        setPhase(RoundPhase::Playing);
        if(rules_.sourceQuality)sourceHud_.show();
    } else {
        if(rules_.sourceQuality)sourceStartRound();
        else match_.focus(0);
    }
    if(rules_.sourceQuality)sourceCallbacks_.add(SourceRenderCallbacks::Group::Main,GameCallback);
}

void VersusRound::resetActors(bool keepGauges) {
    // GameStage.resetFight rebuilds the arena/UI, not GameCtrl/EffectCtrl's
    // global cadence. Keep the source actor/FX timing across round changes.
    const auto timing=rules_.sourceQuality&&keepGauges?match_.controllerTiming():std::nullopt;
    match_.setClassicScore(rules_.classicScore);
    match_.sourceArcadeScoring(isArcadeMode(mode_));
    const auto geometry = stage_.geometry();
    const double center = (geometry.left + geometry.right) * 0.5;
    for (unsigned side = 0; side < 2; ++side) match_.actor(side).setFaction(factions_[side][members_[side]]);
    match_.actor(0).setRoundStats(rules_.maxHealth, 1);
    // GameStage applies the mission attack multiplier when the opening opponent enters.
    match_.actor(1).setRoundStats(rules_.maxHealth, members_[1] == 0 ? rules_.enemyAttackRate : 1);
    match_.reset(stage_, stage_.spawn(0).value_or(Point{center - 130, geometry.playerBottom}),
                 stage_.spawn(1).value_or(Point{center + 130, geometry.playerBottom}), keepGauges);
    if(rules_.sourceQuality){match_.setSourceProfile(rules_.gameHz,rules_.shineHz,stage_);setGameHz(rules_.gameHz);}
    if(timing)match_.restoreControllerTiming(*timing);
    if(rules_.sourceQuality)sourceGates(training());
}

void VersusRound::setPhase(RoundPhase phase) {
    phase_ = phase;
    phaseTicks_ = 0;
    sourceUiFrames_=sourceMainPhaseTicks_=0;sourceUiComplete_=false;
}

void VersusRound::endRound(int winner, bool timeout) {
    winner_ = winner;
    match_.stopInput();
    if(rules_.sourceQuality)sourceGates(false);
    if (winner >= 0) {
        ++wins_[winner];
        perfect_ = match_.actor(winner).health() == match_.actor(winner).maxHealth();
        if (team()) {
            const unsigned loser = 1 - winner;
            lastLoserFaction_ = factions_[loser][members_[loser]];
            lastLoserQi_ = unsigned(match_.actor(loser).qi());
        }
    }
    if(rules_.sourceQuality)sourceEnd_.begin(deliveryHz(),winner<0);
    if (timeout) {
        setPhase(RoundPhase::TimeOver);
    } else {
        if(rules_.sourceQuality)sourceEndPending_=true;
        else{match_.focus(1 - winner);match_.flashKnockout(1-winner);}
        setPhase(RoundPhase::KnockoutFlash);
    }
}

void VersusRound::nextRound() {
    if (team()) {
        nextTeamRound();
        return;
    }
    if (wins_[0] >= WinsRequired || wins_[1] >= WinsRequired) {
        setPhase(RoundPhase::Finished);
        return;
    }
    ++round_;
    seconds_ = secondsPerRound_;
    winner_ = -1;
    resetActors(true);
    setPhase(RoundPhase::Ready);
    if(rules_.sourceQuality)sourceStartRound(true);
}

void VersusRound::nextTeamRound() {
    if (winner_ >= 0) {
        const unsigned loser = 1 - winner_;
        if (members_[loser] + 1 == rules_.members[loser]) {
            setPhase(RoundPhase::Finished);
            return;
        }
        incoming_ = 1U << loser;
    } else {
        const bool left = members_[0] + 1 < rules_.members[0];
        const bool right = members_[1] + 1 < rules_.members[1];
        if (left != right) {
            winner_ = left ? 0 : 1;
            setPhase(RoundPhase::Finished);
            return;
        }
        if (!left) {
            startTeamRound(3);
            return;
        }
        incoming_ = 3;
    }
    setPhase(RoundPhase::TeamChange);
}

std::optional<unsigned> VersusRound::entering(unsigned side) const {
    if (phase_ != RoundPhase::TeamChange || !(incoming_ & (1U << side))) return {};
    return members_[side] + 1;
}

bool VersusRound::enterTeamMembers(const std::array<TeamEntrant, 2>& fighters) {
    if (phase_ != RoundPhase::TeamChange) return false;
    for (unsigned side = 0; side < 2; ++side) {
        if (!entering(side)) continue;
        const auto& fighter = fighters[side];
        if (fighter.form >= fighter.forms.size() || !fighter.forms[fighter.form].has(MoveClip::Idle)) return false;
    }
    for (unsigned side = 0; side < 2; ++side) {
        const auto next = entering(side);
        if (!next) continue;
        const auto& fighter = fighters[side];
        match_.actor(side) = FighterMotion(fighter.forms, fighter.form);
        members_[side] = *next;
        if (lastLoserFaction_ == factions_[side][*next]) match_.actor(side).setQi(lastLoserQi_ + 100);
    }
    startTeamRound(incoming_);
    return true;
}

void VersusRound::startTeamRound(unsigned introMask) {
    ++round_;
    seconds_ = secondsPerRound_;
    resetActors(true);
    if (winner_ >= 0) match_.actor(winner_).setHealth(survivorHealth_);
    winner_ = -1;
    perfect_ = false;
    incoming_ = 0;
    introMask_ = introMask;
    introSide_ = (introMask_ & 1) ? 0 : 1;
    match_.focus(introSide_);
    setPhase(RoundPhase::IntroFocus);
    if(rules_.sourceQuality)sourceStartRound();
}

void VersusRound::showWinner() {
    match_.focus(winner_);
    match_.actor(winner_).present(FighterCommand::Victory);
    if (team()) {
        const double timeRate = secondsPerRound_ > 0 ? double(seconds_) / secondsPerRound_ : 1;
        auto& survivor = match_.actor(winner_);
        const unsigned cap = unsigned(survivor.maxHealth());
        const unsigned heal = std::max(cap / 20, unsigned(cap / 5 * timeRate));
        survivorHealth_ = std::min(cap, unsigned(survivor.health() + heal));
        survivor.setHealth(survivorHealth_);
    }
    setPhase(RoundPhase::Victory);
}

bool VersusRound::step(const std::array<FighterControls, 2>& controls, bool skip) {
    if(phase_==RoundPhase::Finished||phase_==RoundPhase::TeamChange)return false;
    if(!rules_.sourceQuality){stepSource(controls,skip);return true;}
    if(!sourceExplicitUiClock_){sourceUiWallMs_=++sourceDisplayFrames_*1000./60;}
    for(unsigned side=0;side<2;++side)queuedControls_[side].submit(controls[side]);
    queuedSkip_|=skip;if(!delivery_.advance())return false;
    const std::array<FighterControls,2> sampled{queuedControls_[0].take(),queuedControls_[1].take()};
    stepOrderedSource(sampled,std::exchange(queuedSkip_,false));
    return sourceMainDispatched_;
}

void VersusRound::stepSource(const std::array<FighterControls, 2>& controls, bool skip) {
    if (phase_ == RoundPhase::Finished || phase_ == RoundPhase::TeamChange) return;
    const bool animate = ++ticks_ % ticksPerAnimation() == 0;
    ++phaseTicks_;
    if (skip && phase_ <= RoundPhase::IntroPan) {
        for (unsigned side = 0; side < 2; ++side) match_.actor(side).cancelIntro();
        match_.focus(std::nullopt);
        setPhase(RoundPhase::IntroSkip);
    }
    const auto idle = [&] { match_.step(stage_.geometry(), {}, animate, Contacts::Locked); };

    switch (phase_) {
    case RoundPhase::IntroFocus:
        idle();
        if (phaseTicks_ >= deliveryHz() / 2) {
            match_.actor(introSide_).present(FighterCommand::Intro);
            setPhase(RoundPhase::IntroPose);
        }
        break;
    case RoundPhase::IntroPose:
        idle();
        if (match_.actor(introSide_).performing(FighterCommand::Intro)) phaseTicks_ = 0;
        if (phaseTicks_ >= 18*deliveryHz()/60) {
            if (introSide_ == 0 && (introMask_ & 2)) {
                introSide_ = 1;
                match_.focus(1);
                setPhase(RoundPhase::IntroFocus);
            } else {
                match_.focus(std::nullopt);
                setPhase(RoundPhase::IntroPan);
            }
        }
        break;
    case RoundPhase::IntroPan:
    case RoundPhase::IntroSkip:
        idle();
        if (phaseTicks_ >= (phase_ == RoundPhase::IntroSkip ? deliveryHz()/2 : deliveryHz()/10)) setPhase(RoundPhase::Ready);
        break;
    case RoundPhase::Ready:
        idle();
        if (phaseTicks_ >= readyLength(round_) * ticksPerAnimation()) setPhase(RoundPhase::Playing);
        break;
    case RoundPhase::Playing:
        if (training()) {
            const bool frozen = match_.cinematic()||match_.hitFreeze();
            match_.step(stage_.geometry(), controls, animate, Contacts::Training);
            for (unsigned side = 0; side < 2; ++side) {
                auto& fighter = match_.actor(side);
                if (!frozen) fighter.updateTraining();
                history_[side].step(fighter,deliveryHz());
            }
            break;
        }
        if (match_.roundClockTick(animate) && seconds_ >= 0 && ++clockFrames_ > AnimationHz) {
            clockFrames_ = 0;
            if (--seconds_ == 0) {
                const double first = match_.actor(0).health(), second = match_.actor(1).health();
                endRound(first == second ? -1 : first > second ? 0 : 1, true);
                break;
            }
        }
        match_.step(stage_.geometry(), controls, animate, Contacts::Round);
        if (match_.actor(1).health() <= 0) endRound(0, false);
        else if (match_.actor(0).health() <= 0) endRound(1, false);
        break;
    case RoundPhase::KnockoutFlash:
    case RoundPhase::Knockout:
        match_.stepScene(animate);
        if (phase_ == RoundPhase::KnockoutFlash && phaseTicks_ >= deliveryHz() / 2) {
            setPhase(RoundPhase::Knockout);
        } else if (phase_ == RoundPhase::Knockout && phaseTicks_ >= (KnockoutComplete - KnockoutStart) * ticksPerAnimation()) {
            match_.resumeKnockout();
            setPhase(RoundPhase::KnockoutFall);
        }
        break;
    case RoundPhase::KnockoutFall:
        if(rules_.sourceQuality){
            match_.step(stage_.geometry(), {}, animate, Contacts::Locked);
        }else if (phaseTicks_ % 2 == 0) {
            match_.step(stage_.geometry(), {}, phaseTicks_ % (2 * ticksPerAnimation()) == 0, Contacts::Locked);
        } else {
            match_.stepScene(animate);
        }
        if (match_.actor(1 - winner_).knockedDown()) {
            if(rules_.sourceQuality)match_.resumeKnockoutSpeed();
            winnerDelay_ = phaseTicks_ < 2 * deliveryHz() ? 2 * deliveryHz() : deliveryHz();
            setPhase(RoundPhase::WinnerDelay);
        }
        break;
    case RoundPhase::TimeOver:
        idle();
        if (phaseTicks_ >= (TimeOverComplete - TimeOverStart) * ticksPerAnimation()) {
            winnerDelay_ = deliveryHz();
            setPhase(winner_ < 0 ? RoundPhase::Draw : RoundPhase::WinnerDelay);
        }
        break;
    case RoundPhase::Draw:
        idle();
        if (phaseTicks_ >= (DrawComplete - DrawStart) * ticksPerAnimation() + deliveryHz()) nextRound();
        break;
    case RoundPhase::WinnerDelay:
        idle();
        if (phaseTicks_ >= winnerDelay_) showWinner();
        break;
    case RoundPhase::Victory:
        idle();
        if (skip || phaseTicks_ >= 3 * deliveryHz()) nextRound();
        break;
    case RoundPhase::Finished:
    case RoundPhase::TeamChange:
        break;
    }
}

std::optional<unsigned> VersusRound::bannerFrame() const {
    if(rules_.sourceQuality)return sourceBanner_.frame();
    const unsigned frame = phaseTicks_ / ticksPerAnimation();
    switch (phase_) {
    case RoundPhase::Ready:
        if (round_ < 5) return RoundStart + frame;
        if (frame <= FinalJump - FinalStart) return FinalStart + frame;
        return FightStart + frame - (FinalJump - FinalStart + 1);
    case RoundPhase::Knockout: return KnockoutStart + frame;
    case RoundPhase::TimeOver: return TimeOverStart + frame;
    case RoundPhase::Draw:
        if (frame < DrawComplete - DrawStart) return DrawStart + frame;
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace bvn
