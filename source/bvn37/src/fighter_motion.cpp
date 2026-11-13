// Adapted from 5DPLAY Game Studio's FighterMC and ground/air controllers.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_motion.hpp"
#include "floor_effects.hpp"
#include "game_settings.hpp"

#include <algorithm>
#include <limits>
#include <cmath>
#include <utility>

namespace bvn {
namespace {

constexpr unsigned JumpDelayFrames = 2;
constexpr double JumpApexThreshold = 0.35;
constexpr unsigned HitGap = 4;
constexpr double HurtDampingX = 0.1;
constexpr double HurtDampingY = 0.5;
constexpr unsigned TwinLionFlag = 10 - FirstTimelineCounter;
constexpr unsigned TwinLionCooldownFlag = 11 - FirstTimelineCounter;

AiState commandState(FighterCommand command) {
    switch (command) {
    case FighterCommand::Attack: case FighterCommand::AirAttack: return AiState::Attack;
    case FighterCommand::Super: case FighterCommand::SuperUp: case FighterCommand::SuperAir: return AiState::Super;
    case FighterCommand::SuperDown: return AiState::Ultimate;
    case FighterCommand::Dash: return AiState::Dash;
    case FighterCommand::Intro: return AiState::Intro;
    case FighterCommand::Victory: return AiState::Win;
    case FighterCommand::Defeat: return AiState::Lose;
    default: return AiState::Skill;
    }
}

} // namespace

void FighterMotion::reset(Point spawn, int facing, bool keepGauges) {
    effectTarget_=std::make_shared<EffectTarget>();
    if (!keepGauges) {
        form_ = initialForm_;
        baseFaces_ = form_ == 0;
        bankaiIntro_ = false;
        timelineCounters_ = {};
    }
    motion_ = {};
    if(sourcePhysics_)motion_.sourceHz(gameHz_);
    sourceHurt_.reset();sourceReactionPose_=false;sourceAlive_=true;sourceDieEvents_=sourceGuardResume_=0;sourceEvents_={};
    floorDownPlayed_ = false;
    flightMode_ = false;
    mirroredEffect_ = false;
    kamui_ = false;
    opacity_ = 100;
    sourceDisplayAlpha_=1;sourceAlphaRecovery_=false;
    waterWalkClip_ = MoveClip::Missing;
    waterInput_ = waterMoving_ = false;
    twinLionTicks_ = twinLionCooldown_ = chakraTicks_ = repeatHit_ = 0;
    timelineCounters_[TwinLionFlag] = timelineCounters_[TwinLionCooldownFlag] = 0;
    timelineLoop_ = timelineQiGain_ = std::numeric_limits<double>::quiet_NaN();
    timelineAirFollow_ = false;
    comboLimits_ = {};
    comboChecks_ = 0;
    hitModel_.reset();
    currentHurts_.clear();
    hurtEscaping_ = justHurtResume_ = pendingHurtEscape_ = false;
    hurtDownFrame_ = reactionPhase_ = 0;
    hurtEscapes_ = 0;
    counterKind_ = CounterKind::None;
    counterPaused_ = false;
    ghostKind_ = GhostKind::None;
    ghostActive_ = shadowActive_ = inGhostEffect_ = false;
    shadowLifetime_.reset();
    ghostFrames_ = 0;
    ghostStarts_ = ghostEnds_ = pendingGhostRequests_ = 0;
    ghostAnimationPending_ = false;
    counterStarts_ = counterGameFrame_ = counterPausePhase_ = 0;
    explodeHitFrames_ = explodeSteelFrames_ = replaceSkillFrames_ = 0;
    explodeHit_.reset();
    counterPositions_ = {};
    motion_.position = spawn;
    facing_ = facing;
    health_ = maxHealth_;
    if (!keepGauges) {
        qi_ = training_ ? FighterMaxQi : 0;
        energy_ = maxEnergy_;
        assistCharge_ = AssistChargeMax;
        energyDelay_ = 0;
        overloaded_ = false;
        lastBisha_ = false;
        buffs_ = {};
        shunko_ = false;
    }
    trainingHpDelay_ = trainingQiDelay_ = trainingAssistDelay_ = 0;
    trainingMove_ = 0;
    trainingInput_.reset();
    clearInput();
    normalAirJumpDelay_ = 0;
    normalAirJumpReady_ = true;
    hurtFrames_ = hitGap_ = 0;
    airAttackUsed_ = false;
    effects_ = {};
    buffEffects_.clear();
    dashSound_.reset();
    shaking_ = false;
    spawnCount_ = 0;
    flight_ = Flight::None;
    recover();
}

void FighterMotion::startTraining() {
    training_ = true;
    qi_ = FighterMaxQi;
}

void FighterMotion::updateTraining() {
    // TrainingCtrl starts a one-second HP delay when the hurt state ends.
    const auto restore = [&](auto& value, auto maximum, unsigned& delay) {
        if (delay > 0) {
            if (--delay == 0) value = maximum;
        } else if (value < maximum) {
            delay = 2 * deliveryHz_;
        }
    };
    if (injured()) trainingHpDelay_ = deliveryHz_;
    else restore(health_, maxHealth_, trainingHpDelay_);
    restore(qi_, FighterMaxQi, trainingQiDelay_);
    restore(assistCharge_, AssistChargeMax, trainingAssistDelay_);
}

bool FighterMotion::canCallAssist() const {
    return assistCharge_ == AssistChargeMax && !motion_.airborne && !injured() && !defeated() &&
        !acting_ && !transforming_ && (standing() || clip_ == MoveClip::Walk || guardClip());
}

void FighterMotion::recordAssist() {
    if (training_) trainingInput_ = FighterInputEvent{.command = FighterCommand::Count, .assist = true};
}

void FighterMotion::recordTrainingInput(FighterCommand command, int horizontal, bool drop, bool highlight) {
    if (training_) trainingInput_ = FighterInputEvent{command, horizontal, drop, false, highlight};
}

std::optional<FighterInputEvent> FighterMotion::takeTrainingInput() {
    return std::exchange(trainingInput_, std::nullopt);
}

void FighterMotion::enter(MoveClip clip) {
    if(sourcePhysics_&&!sourceReactionPose_)sourceHurt_.externalFrame(actionState_);
    if (clip != clip_) mapBackdrop_ = false;
    clip_ = clip;
    phase_ = 0;
    jumpDelay_ = clip == MoveClip::JumpStart ? JumpDelayFrames : 0;
    if (clip == MoveClip::Idle) autoFacingFrame_ = 0;
    applyFrameEvents();
}

void FighterMotion::clearActions() {
    bindings_.fill(MoveClip::Missing);
    bindingQiCosts_.fill(FinishClip);
    groundCancels_ = false;
    walkCancels_ = 0;
    steel_ = superSteel_ = false;
    airMove_ = false;
    casting_ = false;
    mapBackdrop_ = false;
    cutin_ = {};
    probeClip_ = landingClip_ = hurtClip_ = MoveClip::Missing;
    targetMove_.reset();
    followAttacker_ = nullptr;
    targetHidden_ = false;
}

void FighterMotion::recover(MoveClip ground) {
    if(sourcePhysics_&&!alive())return;
    enter(recoveryClip(ground));
    emitSourceEvent(SourceFighterEvent::Idle);
}

MoveClip FighterMotion::recoveryClip(MoveClip ground) {
    if (injured()) justHurtResume_ = true;
    if(sourcePhysics_)sourceHurt_.externalFrame(AiState::Normal,true);
    hurtEscaping_ = pendingHurtEscape_ = false;
    hurtDownFrame_ = 0;
    flight_ = Flight::None;
    currentHurts_.clear();
    clearActions();
    shadowActive_ = false;
    stopShake();
    transforming_ = false;
    hittable_ = !justHurtResume_;
    hurtStrike_ = guardStrike_ = 0;
    acting_ = false;
    actionState_ = AiState::Normal;
    motion_.applyGravity = true;
    dodging_ = crossBody_ = guardReady_ = guardHit_ = false;
    justGuardTicks_ = actionBufferTicks();
    if (motion_.airborne) {
        motion_.velocity.x = 0;
        airMove_ = true;
        normalAirJumpDelay_ = 0;
        normalAirJumpReady_ = true;
        return MoveClip::Fall;
    }
    motion_.setVelocity({});
    if (ground == MoveClip::Idle) {
        jumpsLeft_ = 2;
        quickJumpDelay_ = 0;
        airAttackUsed_ = false;
    }
    return ground;
}

void FighterMotion::startAction(MoveClip clip, bool casting) {
    prepareAction(casting);
    enter(clip);
}

void FighterMotion::prepareAction(bool casting) {
    if(sourcePhysics_)shadowActive_=false;
    stopShake();
    hittable_ = true;
    mapBackdrop_ = false;
    bindings_.fill(MoveClip::Missing);
    bindingQiCosts_.fill(FinishClip);
    groundCancels_ = false;
    walkCancels_ = 0;
    probeClip_ = landingClip_ = hurtClip_ = MoveClip::Missing;
    guardStrike_ = 0;
    acting_ = true;
    casting_ = casting;
    airMove_ = false;
    dodging_ = crossBody_ = guardReady_ = guardHit_ = false;
    motion_.setVelocity({});
    motion_.applyGravity = true;
}

void FighterMotion::dropThrough() {
    clearActions();
    acting_ = false;
    actionState_ = AiState::Jump;
    guardReady_ = guardHit_ = false;
    jumpsLeft_ = 0;
    motion_.position.y += 1;
    motion_.velocity.y = 5;
    motion_.setDamping({0, 1});
    airMove_ = true;
    enter(MoveClip::Fall);
}

void FighterMotion::launchJump() {
    normalAirJumpReady_ = false;
    normalAirJumpDelay_ = 0;
    effects_.ground = GroundCueView{jumpsLeft_ == 2 ? GroundCue::Jump : GroundCue::AirJump, motion_.position};
    --jumpsLeft_;
    actionState_ = AiState::Jump;
    motion_.jump(asset().stats().jumpPower);
    airMove_ = true;
    quickJumpDelay_ = 2;
    enter(MoveClip::JumpRise);
}

void FighterMotion::startGuard(bool keepProtection) {
    clearActions();
    acting_ = false;
    actionState_ = AiState::Guard;
    guardReady_ = keepProtection || justGuardTicks_ > 0;
    guardDelay_ = 1;
    motion_.setDamping({asset().stats().speed, asset().stats().speed});
    enter(asset().command(FighterCommand::Guard));
    emitSourceEvent(SourceFighterEvent::Defense);
}

void FighterMotion::addQi(double value) {
    qi_ = std::min(qi_ + value, maxQi_);
}

void FighterMotion::setHealth(double value) {
    health_ = std::clamp(value, 0.0, maxHealth_);
}

void FighterMotion::applyBuff(FighterBuff kind, double value, unsigned frames) {
    const auto index = static_cast<unsigned>(kind);
    auto& buff = buffs_[index];
    if (!buff || buff->finished) buff = std::make_shared<BuffState>();
    *buff = {value, frames, false};
    if (kind == FighterBuff::Attack) updateHitRates();
    if (value > 0) {
        constexpr std::string_view bursts[]{"buff_effect_speed", "buff_effect_power", "buff_effect_defense"};
        constexpr std::string_view auras[]{"buff_speed", "buff_power", "buff_defense"};
        const BuffEffectRequest request{combatEffect(bursts[index]), combatEffect(auras[index]),
                               motion_.position, effectTarget(), buff, facing_};
        if(sourcePhysics_&&sourceHooks_.buff)sourceHooks_.buff(sourceHooks_.context,request);
        else buffEffects_.push_back(request);
    }
}

double FighterMotion::defenseRate() const {
    return std::clamp(1 - buffValue(FighterBuff::Defense), 0.1, 1.0);
}

void FighterMotion::setQi(double value) {
    qi_ = std::clamp(value, 0.0, maxQi_);
}

void FighterMotion::setGaugeLimits(double qi, double energy) {
    maxQi_ = qi;
    maxEnergy_ = energy;
    setQi(qi_);
    setEnergy(energy_);
}

void FighterMotion::setEnergy(double value) {
    energy_ = std::clamp(value, 0.0, maxEnergy_);
}

void FighterMotion::stopShake() {
    if (shaking_) {
        effects_.shakeLoopX = 0;
        effects_.shakeLoopY = 0;
        shaking_ = false;
    }
}

void FighterMotion::applyFrameEvents() {
    for (unsigned jumps = 0; jumps < 8; ++jumps) {
        flushSourceEffects();
        MoveClip next = MoveClip::Missing;
        const auto events = asset().events(frame());
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto& event = events[i];
            if (event.op == FighterOp::Recover) {
                if (alive()) recover(event.target == FinishClip ? MoveClip::Idle : MoveClip(event.target));
                flushSourceEffects();
                // An AS3 frame method continues after idle()/gotoAndPlay().
                // Hinata's frame 548 ends the ghost after changing to idle;
                // dropping that trailing call strands BlackBack's target.
                if(sourceControlOrder_)continue;
                return;
            }
            if (isFighterBranch(event.op)) {
                if (!passes(event)) i += event.target;
                continue;
            }
            next = applyEvent(event);
            flushSourceEffects();
            if (next != MoveClip::Missing) break;
        }
        if (next == MoveClip::Missing) return;
        if (next != clip_) mapBackdrop_ = false;
        clip_ = next;
        phase_ = 0;
    }
}

bool FighterMotion::passes(const FighterEvent& event) const {
    if (event.op == FighterOp::IfWaterInput) return waterInput_;
    if (event.op == FighterOp::IfFlight) return flightMode_;
    if (event.op == FighterOp::IfShunko) return shunko_;
    if (event.op == FighterOp::IfEnergy) {
        const auto mode = unsigned(event.command);
        if (mode == 1) return energy_ >= event.x || (event.y && !overloaded_);
        // In these two-fighter modes the opponent's current fighter target is this fighter.
        if (mode == 3) return target_ && energy_ >= event.x;
        return target_ && (mode == 2 ? energy_ <= event.x : energy_ < event.x);
    }
    if (event.op == FighterOp::IfTimelineValue) {
        const auto mode = unsigned(event.command);
        if (mode == 2) return timelineAirFollow_;
        const double value = event.y ? timelineCounters_[unsigned(event.y) - FirstTimelineCounter] : timelineLoop_;
        return mode == 1 ? value < event.x : value == event.x;
    }
    if (event.op == FighterOp::IfHealth) return health_ > event.x;
    if (event.op == FighterOp::IfAttacker) {
        const auto key = std::uint32_t(event.x) | (std::uint32_t(event.y) << 16);
        if(sourcePhysics_&&sourceHooks_.attacker)return sourceHooks_.attacker(sourceHooks_.context,sourceHooks_.id,key);
        if (std::ranges::find(attackers_, key) != attackers_.end()) return true;
        return std::ranges::any_of(std::span(spawns_).first(spawnCount_), [key](const auto& spawn) {
            return spawn.op == FighterOp::Summon && spawn.asset->attacker(spawn.id).key == key;
        });
    }
    if (!target_ || event.op == FighterOp::Skip) return false;
    if (event.op == FighterOp::IfDisplayOverlap) {
        const auto bounds = target_->displayBounds();
        return std::ranges::any_of(asset().hits(frame()), [&](const auto& hit) {
            return hit.probe() && hit.strikeIndex == unsigned(event.x) &&
                worldBox(hit.box, motion_.position, facing_).overlaps(bounds);
        });
    }
    if (event.op == FighterOp::IfTarget) {
        const unsigned mode = unsigned(event.command);
        if (mode == 3) return target_->steel();
        return mode == 0 || target_->motion_.airborne == (mode == 2);
    }
    if (event.op == FighterOp::IfHit) {
        const auto recent = target_->hurtStrike_ ? target_->hurtStrike_ :
            static_cast<unsigned>(event.command) ? target_->guardStrike_ : 0;
        return recent == asset().strike(unsigned(event.x)).key;
    }
    if (event.op == FighterOp::IfHurt) {
        const unsigned mask = static_cast<unsigned>(event.command);
        const bool normal = !target_->acting_ && !target_->transforming_ && !target_->guardClip() &&
            !target_->injured() && !target_->defeated() && target_->clip_ != MoveClip::JumpStart;
        return ((mask & 1) && target_->guardClip()) ||
            ((mask == 0 || (mask & 2)) && target_->clip_ == MoveClip::Hurt && target_->flight_ == Flight::None) ||
            ((mask & 4) && normal) ||
            ((mask & 8) && target_->flight_ >= Flight::Start && target_->flight_ <= Flight::Contact);
    }
    const auto point = target_->motion_.position;
    const auto self = motion_.position;
    const double distance = static_cast<unsigned>(event.command) ? point.y - self.y : (point.x - self.x) * facing_;
    return distance >= event.x && distance <= event.y;
}

MoveClip FighterMotion::applyEvent(const FighterEvent& event) {
    switch (event.op) {
    case FighterOp::Velocity: {
        const unsigned mode = unsigned(event.command);
        if (mode == 2) {
            motion_.setVelocity({(event.x * speed() + event.y) * facing_, 0});
        } else {
            const double rate = mode ? speed() / asset().stats().speed : 1;
            motion_.setVelocity({event.x * rate * facing_, event.y * rate});
        }
        if (motion_.airborne && event.x != 0) airMove_ = false;
        break;
    }
    case FighterOp::Damping: {
        const double rate = unsigned(event.command) ? speed() / asset().stats().speed : 1;
        motion_.setDamping({event.x * rate, event.y * rate});
        break;
    }
    case FighterOp::Allow: {
        const auto slot = static_cast<std::size_t>(event.command);
        bindings_[slot] = MoveClip(event.target);
        bindingQiCosts_[slot] = event.y ? unsigned(event.x) : FinishClip;
        break;
    }
    case FighterOp::End: {
        const auto cutin = cutin_;
        clearActions();
        actionState_ = AiState::Freeze;
        cutin_ = cutin;
        break;
    }
    case FighterOp::Gravity:
        if (!unsigned(event.command)) motion_.applyGravity = event.x != 0;
        else if (target_) target_->motion_.applyGravity = event.x != 0;
        break;
    case FighterOp::FlightMode:
        flightMode_ = event.x != 0;
        break;
    case FighterOp::RememberFacing:
        mirroredEffect_ = facing_ < 0;
        break;
    case FighterOp::TimelineValue: {
        const auto field = unsigned(event.command);
        if (field >= FirstTimelineCounter) {
            auto& value = timelineCounters_[field - FirstTimelineCounter];
            if (event.y) value += static_cast<std::uint32_t>(int(event.x));
            else value = static_cast<std::uint32_t>(event.x);
            break;
        }
        switch (field) {
        case 0: timelineLoop_ = event.y ? timelineLoop_ + event.x : event.x; break;
        case 1: timelineAirFollow_ = event.x != 0; break;
        case 2: timelineQiGain_ = event.x; break;
        case 3: bankaiIntro_ = event.x != 0; break;
        }
        break;
    }
    case FighterOp::TimelineQi:
        addQi(timelineQiGain_);
        break;
    case FighterOp::ComboLimit: {
        auto& limit = comboLimits_[unsigned(event.command)];
        limit.clip = MoveClip(event.target);
        ++limit.count;
        ++comboChecks_;
        break;
    }
    case FighterOp::Hittable:
        if (!unsigned(event.command)) hittable_ = event.x != 0;
        else if (target_) target_->hittable_ = event.x != 0;
        break;
    case FighterOp::StrikeReaction:
        setStrikeReaction(event.target, unsigned(event.command), event.x);
        break;
    case FighterOp::Steel:
        setSteelBody(event.x!=0,event.y!=0);
        break;
    case FighterOp::Qi:
        addQi(event.x);
        break;
    case FighterOp::TransferQi:
        if (target_) {
            const double transferred = std::min(target_->qi_, double(event.x));
            addQi(transferred);
            target_->qi_ -= transferred;
        }
        break;
    case FighterOp::LoseHealth:
        if(allowLoseHp_)health_ = std::max(0.0, health_ - event.x * defenseRate());
        break;
    case FighterOp::LoseEnergy:
        useEnergy(event.x);
        break;
    case FighterOp::FighterBuff: {
        const unsigned mode = unsigned(event.command);
        const auto duration = unsigned(std::ceil(event.y*deliveryHz_/LogicHz));
        const double value = event.x / 100.0;
        if (mode == 1) {
            applyBuff(FighterBuff::Attack, value, duration);
        } else if (target_) {
            if (!mode) target_->applyBuff(FighterBuff::Speed, value, duration);
            else {
                applyBuff(FighterBuff::Speed, value, duration);
                shunko_ = true;
            }
        }
        break;
    }
    case FighterOp::GainEnergy:
        energy_ += event.x;
        break;
    case FighterOp::HealPercent:
        setHealth(health_ + maxHealth_ * (event.x / 10000.0));
        break;
    case FighterOp::AirMove:
        airMove_ = event.x != 0;
        break;
    case FighterOp::Shake:
        effects_.shakeY = event.x;
        effects_.shakeTimeMs = event.y;
        break;
    case FighterOp::Shadow:
        startShadow({int(event.x), int(event.y), std::int16_t(event.target)});
        break;
    case FighterOp::EndShadow:
        shadowActive_ = false;
        break;
    case FighterOp::WalkSound:
        if (inGhostEffect_) {
            effects_.reactions.push_back({ReactionCue::GhostFoot, motion_.position, 0, facing_});
            ++effects_.ghostFootSounds;
        } else ++effects_.walkSounds;
        break;
    case FighterOp::Shine:
        effects_.addShine(static_cast<std::uint32_t>(event.x));
        break;
    case FighterOp::DashStart:
        motion_.setVelocity({event.x * (speed() / asset().stats().speed) * facing_, 0});
        crossBody_ = dodging_ = true;
        break;
    case FighterOp::DashStop:
        motion_.setDamping({std::abs(motion_.velocity.x) * event.x, 0});
        crossBody_ = dodging_ = false;
        bindings_.fill(MoveClip::Missing);
        bindingQiCosts_.fill(FinishClip);
        airMove_ = false;
        break;
    case FighterOp::Fire:
    case FighterOp::Summon:
    case FighterOp::ControlAttacker:
    case FighterOp::RememberAttacker:
    case FighterOp::ControlRemembered:
        if ((sourcePhysics_&&sourceHooks_.spawn)||spawnCount_ < spawns_.size()) {
            const unsigned option = unsigned(event.command);
            const bool jump = (event.op == FighterOp::ControlAttacker && option != 1) ||
                event.op == FighterOp::ControlRemembered;
            const auto projectileStrike = event.op == FighterOp::Fire ? asset().projectile(event.target).strikeIndex : FinishClip;
            const auto* reaction = projectileStrike != FinishClip ? strikeReaction(projectileStrike) : nullptr;
            const double rate = projectileStrike != FinishClip ? strikePowerRate(reaction) : powerRate();
            const StrikePower power = reaction ? StrikePower{projectileStrike, double(reaction->strike.damage)} : StrikePower{};
            const ObjectSpawn spawn{event.op, event.target, motion_.position, facing_, &asset(), rate,
                                      power, jump ? unsigned(event.x) : FinishClip, option,
                                      event.op == FighterOp::ControlAttacker && option == 2, runtimeHitModel()};
            if(sourcePhysics_&&sourceHooks_.spawn){flushSourceEffects();sourceHooks_.spawn(sourceHooks_.context,sourceHooks_.id,spawn,target_);}
            else spawns_[spawnCount_++]=spawn;
        }
        break;
    case FighterOp::Cutin:
        cutin_ = {CutinKind(event.command), event.x && !baseFaces_ ? MoveClip::Missing : MoveClip(event.target), 1, &asset()};
        effects_.controllerRequests.push_back({cutin_.kind==CutinKind::GhostStep?SourceControlOp::Ghost:
            cutin_.kind==CutinKind::Transformation?SourceControlOp::WanKai:
            cutin_.kind==CutinKind::Ultimate?SourceControlOp::SuperBisha:SourceControlOp::Bisha,effectTarget_});
        if (cutin_.kind == CutinKind::GhostStep) {
            ++effects_.ghostStepSound;
            inGhostEffect_ = true;
            startShadow({0, 0, 255});
        }
        break;
    case FighterOp::EndCutin:{
        const unsigned method=unsigned(event.command);
        if(method==2&&sourceControlOrder_&&aiState()!=AiState::Transform)break;
        const auto op=method==3?SourceControlOp::EndGhost:method==2?SourceControlOp::EndWanKai:
            method==1?SourceControlOp::EndBisha:cutin_.kind==CutinKind::GhostStep?SourceControlOp::EndGhost:
            cutin_.kind==CutinKind::Transformation?SourceControlOp::EndWanKai:SourceControlOp::EndBisha;
        effects_.controllerRequests.push_back({op,effectTarget_});
        if(op==SourceControlOp::EndGhost)shadowActive_=inGhostEffect_=false;
        cutin_ = {};
        break;
    }
    case FighterOp::Landing:
        landingClip_ = MoveClip(event.target);
        break;
    case FighterOp::Goto:
        return MoveClip(event.target);
    case FighterOp::BranchHit:
        return target_ && target_->hurtStrike_ == asset().strike(unsigned(event.x)).key ?
            MoveClip(event.target) : MoveClip(unsigned(event.y));
    case FighterOp::QiOnHit:
        if (target_ && target_->hurtStrike_ == asset().strike(event.target).key) addQi(event.x);
        break;
    case FighterOp::Probe:
        probe_ = unsigned(event.x);
        probeClip_ = MoveClip(event.target);
        break;
    case FighterOp::MoveOnce:
        motion_.position.x += double(event.x) * facing_;
        motion_.position.y += event.y;
        break;
    case FighterOp::TimelineMove:
        motion_.position.x += double(event.x) * timelineCounters_[unsigned(event.command) - FirstTimelineCounter] * facing_;
        break;
    case FighterOp::Kamui:
        kamui_ = true;
        break;
    case FighterOp::WaterWalk:
        waterWalkClip_ = event.x ? clip_ : MoveClip::Missing;
        if (!event.x) {
            waterMoving_ = false;
            motion_.velocity.x = 0;
        }
        break;
    case FighterOp::HinataState:
        if (unsigned(event.command) == 0) {
            twinLionTicks_ = 15 * deliveryHz_;
            twinLionCooldown_ = 25 * deliveryHz_;
            applyBuff(FighterBuff::Speed, 2.5, twinLionTicks_);
        } else if (unsigned(event.command) == 1) {
            chakraTicks_ = 5 * deliveryHz_;
            applyBuff(FighterBuff::Attack, .2, chakraTicks_);
        } else repeatHit_ = asset().strike(event.target).key;
        break;
    case FighterOp::FollowAttackerY:
        followAttacker_ = &asset().attacker(event.target);
        followAttackerY_ = event.x;
        break;
    case FighterOp::MoveToTarget:
        if (target_) {
            const auto point = target_->motion_.position;
            const unsigned options = static_cast<unsigned>(event.command);
            if (options & 1) {
                auto offset = event.x * facing_;
                if (event.x > 0) {
                    if (point.x < event.x) offset = event.x;
                    else if (stageRight_ > 0 && point.x > stageRight_ - event.x) offset = -event.x;
                }
                motion_.position.x = point.x + offset;
            }
            if (options & 2) motion_.position.y = point.y + event.y;
            if (options & 4) facing_ = motion_.position.x < point.x ? 1 : -1;
        }
        break;
    case FighterOp::MoveTarget:
        if (event.target == FinishClip) {
            if (targetMove_ && target_) target_->motion_.applyGravity = true;
            targetMove_.reset();
        } else if (target_) {
            target_->motion_.setVelocity({});
            targetMove_ = TargetMove{event.target, static_cast<unsigned>(event.command) != 0,
                                     {event.x * motion_.speed, event.y * motion_.speed}};
        }
        break;
    case FighterOp::TargetOffset:
        if (target_) {
            const auto point = target_->motion().position;
            target_->place({point.x + event.x, point.y + event.y});
        }
        break;
    case FighterOp::TargetVisible:
        if (target_) targetHidden_ = event.x == 0;
        break;
    case FighterOp::TargetVelocity:
        if (target_) target_->motion_.setVelocity({event.x, event.y});
        break;
    case FighterOp::SelfLaunch:
        return selfLaunch(double(event.x) * facing_, event.y);
    case FighterOp::SwitchForm:
        return switchForm(event.target);
    case FighterOp::SwapForm:
        return switchForm(event.target, false);
    case FighterOp::Cross:
        crossBody_ = event.x != 0;
        break;
    case FighterOp::FaceTarget:
        if (target_) facing_ = motion_.position.x < target_->motion_.position.x ? 1 : -1;
        break;
    case FighterOp::Slow:
        effects_.slowTimeMs = unsigned(event.x);
        break;
    case FighterOp::ShakeLoop:
        if (event.x != 0 || event.y != 0 || unsigned(event.command) == 1) {
            effects_.startShake = true;
            effects_.shakeLoopX = event.y;
            effects_.shakeLoopY = event.x;
            shaking_ = true;
        } else {
            stopShake();
        }
        break;
    case FighterOp::HurtAction:
        actionState_ = AiState::Counter;
        if(sourcePhysics_)sourceHurt_.authoredState(actionState_);
        if (unsigned(event.command)) {
            prepareAction(false);
            return MoveClip(event.target);
        }
        hurtClip_ = MoveClip(event.target);
        break;
    case FighterOp::UlquiorraDashSound:
        dashSound_ = event.x != 0;
        break;
    case FighterOp::MapBackdrop:
        mapBackdrop_ = event.x != 0;
        backdropColor_ = std::uint32_t(event.y);
        break;
    case FighterOp::CounterPosition: {
        auto& position = counterPositions_[unsigned(event.command)];
        if (target_) {
            if (!event.x) position = motion_.position;
            else if (position) motion_.position = *position;
        }
        break;
    }
    case FighterOp::StageCue:
        effects_.ground = unsigned(event.command) == 3 ? rushCue(event.x != 0) :
            GroundCueView{GroundCue(event.command), motion_.position};
        break;
    case FighterOp::JumpCount:
        jumpsLeft_ = int(event.x);
        break;
    case FighterOp::AirAttackLock:
        airAttackUsed_ = true;
        break;
    case FighterOp::GroundCancel:
        groundCancels_ = true;
        walkCancels_ = 3;
        break;
    case FighterOp::WalkCancel:
        if (event.x) walkCancels_ |= unsigned(event.command);
        else walkCancels_ &= ~unsigned(event.command);
        break;
    case FighterOp::Remove:
    case FighterOp::IfHit:
    case FighterOp::IfRange:
    case FighterOp::IfHurt:
    case FighterOp::Skip:
    case FighterOp::IfAttacker:
    case FighterOp::IfHealth:
    case FighterOp::IfEnergy:
    case FighterOp::IfTarget:
    case FighterOp::IfDisplayOverlap:
    case FighterOp::IfWaterInput:
    case FighterOp::IfShunko:
    case FighterOp::ScreenPose:
    case FighterOp::BankaiPose:
    case FighterOp::WaterPose:
    case FighterOp::TwinLionPose:
    case FighterOp::FacingPose:
    case FighterOp::TargetAnchor:
    case FighterOp::AttackerLanding:
    case FighterOp::AttackerStopTracking:
    case FighterOp::AttackerPause:
    case FighterOp::AttackerPushOut:
    case FighterOp::AttackerStopTargetAction:
    case FighterOp::AttackerAllowTargetGuard:
    case FighterOp::AttackerProbe:
    case FighterOp::AttackerGoto:
    case FighterOp::AttackerRange:
    case FighterOp::AttackerCutin:
    case FighterOp::OwnerBuff:
    case FighterOp::OwnerHeal:
    case FighterOp::TargetHealthPower:
    case FighterOp::AttackerCapture:
    case FighterOp::AttackerMiss:
    case FighterOp::AttackerRelease:
    case FighterOp::AttackerRecoverOwner:
    case FighterOp::AttackerWaterEnergy:
    case FighterOp::Recover:
    case FighterOp::Count:
        break;
    }
    return MoveClip::Missing;
}

MoveClip FighterMotion::switchForm(std::uint16_t encoded, bool playIntro) {
    const unsigned next = encoded == FinishClip ? form_ + 1 : encoded;
    if ((playIntro && !transforming_) || next == form_ || next >= forms_.size()) return MoveClip::Missing;
    const auto entry = playIntro ? forms_[next].command(FighterCommand::Intro) : MoveClip::Idle;
    if (!forms_[next].has(entry)) return MoveClip::Missing;

    form_ = next;
    timelineCounters_ = {};
    hitModel_.reset();
    counterPositions_ = {};
    baseFaces_ |= form_ == 0;
    if (!playIntro) return recoveryClip();
    bindings_.fill(MoveClip::Missing);
    bindingQiCosts_.fill(FinishClip);
    hurtStrike_ = guardStrike_ = 0;
    airAttackUsed_ = false;
    motion_.setVelocity({});
    return entry;
}

MoveClip FighterMotion::selfLaunch(double x, double y) {
    clearActions();
    acting_ = false;
    dodging_ = crossBody_ = guardReady_ = guardHit_ = false;
    motion_.applyGravity = true;
    if(sourcePhysics_){SourceHurtView view{*this};sourceHurt_.launch(x,y,sourceHurtInput(),false,view);return MoveClip::Missing;}
    if (x != 0) facing_ = x > 0 ? -1 : 1;
    motion_.setVelocity({x, y});
    motion_.setDamping({0, HurtDampingY});
    if (!asset().has(MoveClip::HurtFly)) return MoveClip::Fall;
    flight_ = Flight::Air;
    hurtDownFrame_ = 0;
    lowestY_ = motion_.position.y;
    heavyDown_ = y > 5;
    floorHitY_ = y;
    floorDownPlayed_ = false;
    flightFrames_ = heavyDown_ || motion_.airborne ? 0 : 15;
    return MoveClip::HurtFly;
}

GroundCueView FighterMotion::rushCue(bool sound) const {
    const auto cue = motion_.airborne ? GroundCue::AirRush : GroundCue::Rush;
    return {cue, motion_.position, 0, facing_, sound};
}

WorldBox FighterMotion::displayBounds() const {
    const auto hits = asset().hits(frame());
    const auto display = std::ranges::find_if(hits, &FighterHit::display);
    return display == hits.end() ? body() : worldBox(display->box, motion_.position, facing_);
}

void FighterMotion::checkProbe() {
    if (!target_ || probeClip_ == MoveClip::Missing) return;
    const auto body = worldBox(target_->frame().body, target_->motion_.position, target_->facing_);
    if (body.empty()) return;
    for (const auto& hit : asset().hits(frame())) {
        if (hit.probe() && hit.strikeIndex == probe_ &&
            worldBox(hit.box, motion_.position, facing_).overlaps(body)) {
            startAction(probeClip_, casting_);
            return;
        }
    }
}

FighterEffects FighterMotion::takeEffects() {
    return std::exchange(effects_, {});
}

std::optional<bool> FighterMotion::takeDashSound() {
    return std::exchange(dashSound_, std::nullopt);
}

std::span<const ObjectSpawn> FighterMotion::takeSpawns() {
    return std::span(spawns_).first(std::exchange(spawnCount_, 0));
}

FighterMotion::SelectedAction FighterMotion::chooseCatch(FighterControls input) const {
    if (!input.horizontal || (!attackBuffer_ && !specialBuffer_) || motion_.airborne ||
        !target_ || target_->defeated() || (target_->clip_ == MoveClip::Hurt && target_->flight_ == Flight::None) ||
        (!acting_ && clip_ != MoveClip::Idle && clip_ != MoveClip::Walk)) return {};

    const auto bound = [&](FighterCommand command) {
        return acting_ ? bindings_[std::size_t(command)] : asset().command(command);
    };
    const auto first = attackBuffer_ ? bound(FighterCommand::CatchAttack) : MoveClip::Missing;
    const auto second = specialBuffer_ ? bound(FighterCommand::CatchSpecial) : MoveClip::Missing;
    if (first == MoveClip::Missing && second == MoveClip::Missing) return {};
    if (std::abs(motion_.position.y - target_->motion_.position.y) >= 1) return {};

    const auto self = body();
    const auto opponent = target_->body();
    if (self.empty() || opponent.empty()) return {};
    const bool opponentRight = self.left < opponent.left;
    if ((facing_ > 0) != opponentRight) return {};
    const double gap = opponentRight ? opponent.left - self.right : self.left - opponent.right;
    if (gap >= 2) return {};
    if (first != MoveClip::Missing) return {first, FighterCommand::CatchAttack};
    return {second, FighterCommand::CatchSpecial};
}

FighterMotion::SelectedAction FighterMotion::chooseAttack(FighterControls input) const {
    if (!attackBuffer_ || defeated()) return {};
    FighterCommand command = FighterCommand::Attack;
    if (motion_.airborne) {
        if (!acting_ && airAttackUsed_) return {};
        command = FighterCommand::AirAttack;
    } else if (input.vertical < 0) {
        command = FighterCommand::SkillDown;
    } else if (input.vertical > 0) {
        command = FighterCommand::SkillUp;
    }
    if (acting_) return {bindings_[static_cast<std::size_t>(command)], command};
    if (motion_.airborne || clip_ == MoveClip::Idle ||
        (clip_ == MoveClip::Walk && command != FighterCommand::SkillDown) ||
        (guardClip() && !guardHit_ && command == FighterCommand::SkillDown) ||
        (clip_ == MoveClip::Land && phase_ >= 2 && command == FighterCommand::Attack)) return {asset().command(command), command};
    return {};
}

FighterMotion::SelectedAction FighterMotion::chooseSpecial(FighterControls input) const {
    if (!specialBuffer_ || defeated()) return {};
    const auto command = motion_.airborne ? FighterCommand::AirSpecial : input.vertical < 0 ? FighterCommand::SpecialDown :
                         input.vertical > 0 ? FighterCommand::SpecialUp : FighterCommand::Special;
    if (acting_) return {bindings_[static_cast<std::size_t>(command)], command};
    if (motion_.airborne) return {airAttackUsed_ ? MoveClip::Missing : asset().command(command), command};
    if (clip_ == MoveClip::Idle || (clip_ == MoveClip::Walk && command != FighterCommand::SpecialDown) ||
        (guardClip() && !guardHit_ && command == FighterCommand::SpecialDown)) return {asset().command(command), command};
    return {};
}

FighterMotion::SelectedAction FighterMotion::chooseSuper(FighterControls input) const {
    if (!superBuffer_ || defeated()) return {};
    const auto command = motion_.airborne ? FighterCommand::SuperAir : input.vertical < 0 ? FighterCommand::SuperDown :
                         input.vertical > 0 ? FighterCommand::SuperUp : FighterCommand::Super;
    const auto slot = static_cast<std::size_t>(command);
    const unsigned standard = command == FighterCommand::SuperDown ? FighterMaxQi : 100;
    const unsigned cost = acting_ && bindingQiCosts_[slot] != FinishClip ? bindingQiCosts_[slot] : standard;
    if (qi_ < cost) return {};
    if (acting_) return {bindings_[slot], command, cost};
    if (motion_.airborne) return {airAttackUsed_ ? MoveClip::Missing : asset().command(command), command, cost};
    if (clip_ == MoveClip::Idle || (clip_ == MoveClip::Walk && command != FighterCommand::SuperDown) ||
        (guardClip() && !guardHit_ && command == FighterCommand::SuperDown)) return {asset().command(command), command, cost};
    return {};
}

MoveClip FighterMotion::chooseDash(FighterControls input) const {
    const double cost = motion_.airborne ? 30 : 20;
    if (!dashBuffer_ || input.vertical < 0 || (energy_ < cost && overloaded_) ||
        (motion_.airborne && jumpsLeft_ <= 0)) return MoveClip::Missing;
    if (acting_) return bindings_[static_cast<std::size_t>(FighterCommand::Dash)];
    if ((motion_.airborne && clip_ != MoveClip::JumpStart) || clip_ == MoveClip::Idle || clip_ == MoveClip::Walk) {
        return asset().command(FighterCommand::Dash);
    }
    return MoveClip::Missing;
}

FighterMotion::SelectedAction FighterMotion::chooseTransformation(FighterControls input) const {
    if (!input.transformHeld || motion_.airborne) return {};
    if (!groundCancels_ && clip_ != MoveClip::Idle && !(performing(FighterCommand::Attack) && phase_ < 2)) return {};
    const auto command = input.vertical < 0 ? FighterCommand::TransformDown :
                         input.vertical > 0 ? FighterCommand::TransformUp : FighterCommand::Transform;
    const auto clip = groundCancels_ ? bindings_[std::size_t(command)] : asset().command(command);
    const auto next = asset().nextForm(clip, form_);
    if (!next || *next == form_ || *next >= forms_.size() ||
        !forms_[*next].has(forms_[*next].command(FighterCommand::Intro))) return {};
    return {clip, command};
}

void FighterMotion::executeAiCommand(FighterCommand command,int horizontal) {
    const auto clip = aiAction(command);
    if (clip == MoveClip::Missing) return;
    const bool super = command == FighterCommand::Super || command == FighterCommand::SuperUp ||
        command == FighterCommand::SuperDown || command == FighterCommand::SuperAir;
    const bool grab = command == FighterCommand::CatchAttack || command == FighterCommand::CatchSpecial;
    if (grab) {
        if (!target_ || target_->defeated() || target_->aiState() == AiState::Hurt ||
            std::abs(motion_.position.y - target_->motion_.position.y) >= 1) return;
        const auto own = body(), other = target_->body();
        if (own.empty() || other.empty()) return;
        const bool right = own.left < other.left;
        const double gap = right ? other.left - own.right : own.left - other.right;
        if ((facing_ > 0) != right || gap >= 2) return;
    }
    const bool air = command == FighterCommand::AirAttack || command == FighterCommand::AirSpecial ||
        command == FighterCommand::SuperAir;
    if (air && !acting_ && airAttackUsed_) return;
    if (super) {
        const auto override = bindingQiCosts_[unsigned(command)];
        const unsigned cost = acting_ && override != FinishClip ? override :
            command == FighterCommand::SuperDown ? FighterMaxQi : 100;
        if (qi_ < cost) return;
        addQi(-double(cost));
    }
    if (air) {
        airAttackUsed_ = true;
        jumpsLeft_ = 0;
    }
    recordTrainingInput(command,horizontal);
    startAction(clip, super);
    actionState_ = commandState(command);
}

void FighterMotion::stepAiControls(FighterControls input) {
    const auto has = [&](AiIntent flag) { return bool(input.aiIntents & (1U << unsigned(flag))); };
    const auto run = [&](AiIntent flag, FighterCommand command) { if (has(flag)) executeAiCommand(command,input.sourceHuman?input.horizontal:0); };
    const auto floorAttacks = [&] {
        run(AiIntent::Ultimate, FighterCommand::SuperDown);
        run(AiIntent::SuperUp, FighterCommand::SuperUp);
        run(AiIntent::Super, FighterCommand::Super);
        run(AiIntent::SkillUp, FighterCommand::SkillUp);
        run(AiIntent::SkillDown, FighterCommand::SkillDown);
        run(AiIntent::SpecialUp, FighterCommand::SpecialUp);
        run(AiIntent::SpecialDown, FighterCommand::SpecialDown);
        run(AiIntent::Attack, FighterCommand::Attack);
        run(AiIntent::Special, FighterCommand::Special);
    };
    const auto airAttacks = [&] {
        run(AiIntent::AirAttack, FighterCommand::AirAttack);
        run(AiIntent::AirSpecial, FighterCommand::AirSpecial);
        run(AiIntent::AirSuper, FighterCommand::SuperAir);
    };
    const auto dash = [&] {
        if (!has(AiIntent::Dash)) return;
        const auto clip = aiAction(FighterCommand::Dash);
        const unsigned cost = motion_.airborne ? 30 : 20;
        if (clip == MoveClip::Missing || (energy_ < cost && overloaded_) ||
            (motion_.airborne && jumpsLeft_ <= 0)) return;
        if (motion_.airborne) jumpsLeft_ = 0;
        else if (input.horizontal) facing_ = input.horizontal;
        useEnergy(cost);
        recordTrainingInput(FighterCommand::Dash);
        startAction(clip);
        actionState_ = AiState::Dash;
        motion_.applyGravity = false;
        dodging_ = true;
    };
    const auto jump = [&] {
        if (!has(AiIntent::Jump) || jumpsLeft_ <= 0) return;
        if (acting_) {
            const auto clip = bindings_[unsigned(FighterCommand::Jump)];
            if (clip == MoveClip::Missing) return;
            clearActions();
            acting_ = false;
            recordTrainingInput(FighterCommand::Jump);
            enter(clip);
        } else if (motion_.airborne) {
            if ((!input.sourceHuman || normalAirJumpReady_ || input.quickJump) && !jumpDelay_ && !quickJumpDelay_ && (clip_ == MoveClip::JumpRise ||
                clip_ == MoveClip::JumpApex || clip_ == MoveClip::Fall)) {
                clearActions();
                recordTrainingInput(FighterCommand::Jump);
                jumpDelay_ = 1;
            }
        } else if (clip_ == MoveClip::Idle || clip_ == MoveClip::Walk) {
            recordTrainingInput(FighterCommand::Jump);
            enter(MoveClip::JumpStart);
        }
    };
    const bool inAir = motion_.airborne;
    if (inAir) {
        airAttacks();
        jump();
        if (actionState_ == AiState::Attack || actionState_ == AiState::Skill ||
            actionState_ == AiState::Super || actionState_ == AiState::Ultimate) floorAttacks();
        dash();
        return;
    }
    run(AiIntent::CatchAttack, FighterCommand::CatchAttack);
    run(AiIntent::CatchSpecial, FighterCommand::CatchSpecial);
    floorAttacks();
    if (has(AiIntent::Guard) && aiAction(FighterCommand::Guard) != MoveClip::Missing && !flightMode_) {
        recordTrainingInput(FighterCommand::Guard);
        startGuard();
    }
    dash();
    if (guardClip() && input.horizontal && input.horizontal != facing_) {
        facing_ = input.horizontal;
        startGuard(guardHit_);
    }
    const bool grounded = acting_ ? groundCancels_ : clip_ == MoveClip::Idle || clip_ == MoveClip::Walk;
    const bool walk = input.horizontal && (acting_ ?
        (walkCancels_ & (input.horizontal < 0 ? 1U : 2U)) : grounded);
    if (walk) {
        if (input.sourceHuman && training_) {
            trainingMove_ = input.horizontal;
            if (previousSourceMove_ != input.horizontal) recordTrainingInput(FighterCommand::Count,input.horizontal);
        }
        if (acting_) {
            clearActions();
            acting_ = false;
        }
        if (clip_ != MoveClip::Walk) enter(MoveClip::Walk);
        actionState_ = AiState::Normal;
        facing_ = input.horizontal;
        motion_.setVelocity({speed() * facing_, 0});
    } else if (clip_ == MoveClip::Walk) {
        motion_.setVelocity({});
        enter(MoveClip::Idle);
    }
    jump();
    if (has(AiIntent::JumpDown) && !motion_.touchingBottom &&
        ((!acting_ && (clip_ == MoveClip::Idle || clip_ == MoveClip::Walk)) || (guardClip() && !guardHit_))) {
        recordTrainingInput(FighterCommand::Jump, 0, true);
        dropThrough();
    }
    if (actionState_ == AiState::Attack || actionState_ == AiState::Skill ||
        actionState_ == AiState::Super || actionState_ == AiState::Ultimate) airAttacks();
}

void FighterMotion::updateTargetMove() {
    if (!target_ || !targetMove_ || dodging_) return;
    const auto move = *targetMove_;
    const auto events = asset().events(frame());
    const auto anchor = std::ranges::find_if(events, [move](const auto& event) {
        return event.op == FighterOp::TargetAnchor && event.target == move.anchor;
    });
    if (anchor == events.end()) return;
    const Point aim{motion_.position.x + facing_ * anchor->x, motion_.position.y + anchor->y};
    auto& target = target_->motion_;
    if (!move.limited) {
        target.position = aim;
    } else if (move.speed.x > 0) {
        if (target.position.x > aim.x + move.speed.x) target.position.x -= move.speed.x;
        if (target.position.x < aim.x - move.speed.x) target.position.x += move.speed.x;
        if (std::abs(target.position.y - aim.y) > move.speed.y) {
            target.velocity.y = std::copysign(move.speed.y, aim.y - target.position.y);
            target.damping.y = 1;
        }
    }
}

void FighterMotion::checkComboLimits() {
    if (!comboChecks_ || !target_) return;
    // BaseGameSprite runs delayCall after movement. Removing a completed callback advances past the next slot.
    for (unsigned check = 0; check < comboChecks_; ++check) {
        if (target_->clip_ != MoveClip::Hurt || target_->flight_ != Flight::None) {
            comboLimits_ = {};
            --comboChecks_;
            continue;
        }
        const auto limit = std::ranges::find_if(comboLimits_, [](const auto& slot) { return slot.count >= 2; });
        if (limit != comboLimits_.end()) {
            limit->count = 0;
            --comboChecks_;
            enter(limit->clip);
        }
        // Without a terminal branch, the script schedules its next check in the same queue.
    }
}

void FighterMotion::stepKamui() {
    if(sourcePhysics_){
        const auto recover=[&]{
            if(sourceDisplayAlpha_<1)displayAlpha(sourceDisplayAlpha_+.05);
            else sourceAlphaRecovery_=false;
        };
        if(!kamui_){if(sourceAlphaRecovery_)recover();return;}
        if(energy_<=20||(clip_!=MoveClip::Idle&&clip_!=MoveClip::Walk&&!performing(FighterCommand::SpecialUp))){
            kamui_=false;hittable_=true;crossBody_=false;sourceAlphaRecovery_=true;recover();return;
        }
        useEnergy(.7);
        if(sourceDisplayAlpha_>.5)displayAlpha(sourceDisplayAlpha_-.05);
        hittable_=false;crossBody_=true;return;
    }
    if (!kamui_) {
        opacity_ = std::min(100U, opacity_ + 5);
        return;
    }
    if (energy_ <= 20 || (clip_ != MoveClip::Idle && clip_ != MoveClip::Walk && !performing(FighterCommand::SpecialUp))) {
        kamui_ = false;
        hittable_ = true;
        crossBody_ = false;
        opacity_ = std::min(100U, opacity_ + 5);
        return;
    }
    useEnergy(.7);
    opacity_ = std::max(50U, opacity_ - 5);
    hittable_ = false;
    crossBody_ = true;
}

void FighterMotion::stepHinata() {
    if (twinLionTicks_ && --twinLionTicks_ == 0) timelineCounters_[TwinLionFlag] = 0;
    if (twinLionCooldown_ && --twinLionCooldown_ == 0) timelineCounters_[TwinLionCooldownFlag] = 0;
    if (chakraTicks_) {
        if (--chakraTicks_) steel_ = superSteel_ = true;
        else if ((!acting_ && !injured()) || guardClip()) steel_ = superSteel_ = false;
    }
    if (repeatHit_) {
        if (target_ && target_->wasHitBy(repeatHit_)) target_->hittable_ = true;
        else repeatHit_ = 0;
    }
}

void FighterMotion::tickBuffs() {
    const double oldAttack = buffValue(FighterBuff::Attack);
    for (auto& buff : buffs_) {
        if (buff && !buff->finished) {
            if (buff->frames) --buff->frames;
            if (!buff->frames) { buff->value = 0; buff->finished = true; }
        }
    }
    if (shunko_ && buffValue(FighterBuff::Speed) == 0) shunko_ = false;
    if (oldAttack != buffValue(FighterBuff::Attack)) updateHitRates();
}

void FighterMotion::stepWaterWalk(int horizontal) {
    if (waterWalkClip_ != clip_) {
        waterWalkClip_ = MoveClip::Missing;
        waterMoving_ = false;
        motion_.velocity.x = 0;
        return;
    }
    waterMoving_ = horizontal != 0;
    motion_.velocity.x = 8 * horizontal;
    if (waterMoving_) facing_ = horizontal;
    else if (target_) facing_ = motion_.position.x < target_->motion_.position.x ? 1 : -1;
}

void FighterMotion::rememberHurtHit(const StrikeContact& contact) {
    if (contact.runtime) {
        const double rate = contact.runtime->damageRate;
        if (rate != 0 || contact.powerRate == 0) {
            currentHurts_.push_back({contact.runtime, rate != 0 ? contact.powerRate / rate : 1,
                contact.power() - contact.runtime->power()});
            return;
        }
    }
    // Contacts created outside a fighter/attacker/bullet model have their own HitVO.
    auto hit = std::make_shared<RuntimeHit>();
    hit->strike = contact.strike;
    hit->damageRate = contact.powerRate;
    hit->customPower = contact.customPower;
    currentHurts_.push_back({std::move(hit)});
}

int FighterMotion::currentHurtDamage() const {
    int damage = 0;
    for (const auto& entry : currentHurts_) damage += int((entry.hit->power() + entry.powerAdd) * entry.hit->damageRate * entry.rate);
    return damage;
}

bool FighterMotion::hurtBreakHit() const {
    return std::ranges::any_of(currentHurts_, [](const auto& entry) { return entry.hit->strike.breaksGuard(); });
}

void FighterMotion::startHurtEscape() {
    const auto clip = asset().hurtEscape();
    if (hurtEscaping_ || currentHurtDamage() > 240 || energy_ < 30 || !asset().has(clip)) return;
    const auto state = aiState();
    const double vx = motion_.velocity.x;
    if(sourcePhysics_)sourceHurt_.reset();
    flight_ = Flight::None;
    hurtFrames_ = 0;
    hurtEscaping_ = true;
    ++hurtEscapes_;
    actionState_ = state;
    useEnergy(30);
    dashBuffer_ = 0;
    recordTrainingInput(FighterCommand::Dash, 0, false, true);
    startAction(clip);
    hittable_ = false;
    motion_.setVelocity({vx, 0});
    // Negative damping is intentionally retained: the original only weakens positive damping.
    motion_.setDamping({vx * .1, 0});
    emitSourceEvent(SourceFighterEvent::HurtResume);
}

bool FighterMotion::stepHurtEscape(FighterControls input) {
    const bool request = input.aiControlled ?
        (input.aiIntents & (1U << unsigned(AiIntent::DownJump))) != 0 : input.dashPressed;
    pendingHurtEscape_ |= request;
    reactionPhase_ += gameHz_;
    if (reactionPhase_ < deliveryHz_) return false;
    reactionPhase_ -= deliveryHz_;
    const bool pressed = std::exchange(pendingHurtEscape_, false);
    if ((sourcePhysics_?!sourceHurt_.flying():flight_==Flight::None)||defeated()) return true;
    if(sourceControlOrder_&&!sourceActionEnabled_)return true;
    if (flight_ == Flight::Bounce) hurtDownFrame_ = 1;
    if (hurtDownFrame_ && ++hurtDownFrame_ < 20 && pressed) {
        startHurtEscape();
        // A rejected request also closes this window; bounce can reopen it on its next render.
        hurtDownFrame_ = 0;
    }
    return true;
}

bool FighterMotion::stepCounterRequest(FighterControls input, const StageGeometry& stage,
                                      std::optional<CameraLimits> camera, double rate) {
    const bool requested = input.aiControlled ?
        (input.aiIntents & (1U << unsigned(AiIntent::Counter))) != 0 : input.assistHeld;
    if (!requested || defeated() || hurtEscaping_ || flight_ != Flight::None || clip_ != MoveClip::Hurt ||
        energy_ < 50 || qi_ < 100 || currentHurts_.empty() || currentHurts_.back().hit->strike.bisha() ||
        hurtBreakHit() || currentHurtDamage() > 210) return false;
    addQi(-100);
    useEnergy(100);
    counterKind_ = faction_ == 1 ? CounterKind::Replace : CounterKind::Explode;
    counterOrigin_ = motion_.position;
    counterPaused_ = true;
    counterGameFrame_ = counterPausePhase_ = 0;
    ++counterStarts_;
    if(sourceControlOrder_)effects_.controllerRequests.push_back({counterKind_==CounterKind::Replace?
        SourceControlOp::Replace:SourceControlOp::Explode,effectTarget_,counterOrigin_});
    if (counterKind_ == CounterKind::Replace) {
        effects_.reaction = ReactionCueView{ReactionCue::Replace, counterOrigin_};
        effects_.counterSounds |= 1;
        // BaseGameSprite.move uses FPS_ANIMATE/FPS_GAME, including the original quality-dependent distance.
        motion_.position.x += 250 * facing_ * (double(AnimationHz) / gameHz_) * rate;
        recover();
        hittable_ = false;
        if(sourceControlOrder_)motion_.renderVelocity();else motion_.step(stage,camera);
        animate(false);
        if (target_) facing_ = motion_.position.x < target_->motion_.position.x ? 1 : -1;
        replaceSkillFrames_ = 30;
    } else {
        effects_.reaction = ReactionCueView{ReactionCue::ExplodeCharge, counterOrigin_};
        effects_.counterSounds |= 2;
        steel_ = superSteel_ = true;
        explodeHit_ = std::make_shared<RuntimeHit>();
        auto& hit = explodeHit_->strike;
        hit.hitX = 15 * facing_;
        hit.damage = 50;
        hit.hitType = 5;
        hit.flags = 1 | 8;
        hit.hurtTimeMs = 300;
        hit.hurtFrames = 12;
        explodeArea_ = {counterOrigin_.x - 100, counterOrigin_.y - 200,
                        counterOrigin_.x + 100, counterOrigin_.y + 10};
        explodeHitFrames_ = 10;
        explodeSteelFrames_ = 60;
        hittable_ = false;
    }
    if (training_) trainingInput_ = FighterInputEvent{FighterCommand::Count, 0, false, true, true};
    emitSourceEvent(SourceFighterEvent::HurtResume);
    return true;
}

bool FighterMotion::stepCounterPause() {
    if (!counterPaused_) return false;
    counterPausePhase_ += gameHz_;
    if (counterPausePhase_ < deliveryHz_) return true;
    counterPausePhase_ -= deliveryHz_;
    ++counterGameFrame_;
    if (counterKind_ == CounterKind::Replace) {
        if (counterGameFrame_ == 1) effects_.reaction = ReactionCueView{ReactionCue::ReplaceSmoke, counterOrigin_};
        if (counterGameFrame_ > gameHz_) counterPaused_ = false;
    } else if (counterGameFrame_ >= gameHz_ * 7 / 10) {
        effects_.reaction = ReactionCueView{ReactionCue::ExplodeBurst, counterOrigin_};
        effects_.counterSounds |= 4;
        counterPaused_ = false;
    }
    return counterPaused_;
}

void FighterMotion::syncSourceCounter(const SourceCounterState& state){
    if(counterKind_==CounterKind::None)return;
    if(counterKind_==CounterKind::Replace&&state.replaceTarget==effectTarget_){
        counterGameFrame_=state.replaceFrame;counterPaused_=state.replaceHold>0;
    }else if(counterKind_==CounterKind::Explode&&state.explodeTarget==effectTarget_){
        counterGameFrame_=state.explodeLength-state.explodeFrame;counterPaused_=state.explodeFrame>0;
    }else counterPaused_=false;
}

void FighterMotion::stepCounterAnimate() {
    if (explodeHitFrames_) {
        --explodeHitFrames_;
        if (explodeHitFrames_ == 8) {
            recover();
            hittable_ = false;
        }
        if (!explodeHitFrames_) {
            explodeHit_.reset();
            hittable_ = true;
        }
    }
    if (explodeSteelFrames_) {
        --explodeSteelFrames_;
        steel_ = superSteel_ = explodeSteelFrames_ != 0;
    }
    if (replaceSkillFrames_ && --replaceSkillFrames_ == 0) hittable_ = true;
}

bool FighterMotion::startGhost(GhostKind kind) {
    // hasEnergy(80, true) permits an overdraft until the fighter is overloaded.
    if (qi_ < 60 || (energy_ < 80 && overloaded_)) return false;
    addQi(-60);
    useEnergy(80);
    if (target_) facing_ = motion_.position.x < target_->motion_.position.x ? 1 : -1;
    ghostActionState_ = aiState();
    ghostKind_ = kind;
    ghostActive_ = inGhostEffect_ = true;
    startShadow({0, 0, 255});
    ghostFrames_ = 12;
    hittable_ = false;
    crossBody_ = true;
    ++effects_.ghostStepSound;
    ++ghostStarts_;
    // goFrame changes the displayed timeline without clearing the current action.
    if (kind == GhostKind::Step) {
        motion_.setVelocity({8.0 * facing_, 0});
        enter(MoveClip::Walk);
    } else if (kind == GhostKind::Jump) {
        motion_.setVelocity({0, -12});
        motion_.setDamping({0, .1});
        enter(MoveClip::JumpRise);
        --jumpsLeft_;
    } else {
        motion_.setVelocity({0, 15});
        enter(MoveClip::Fall);
    }
    cutin_ = {CutinKind::GhostStep, MoveClip::Missing, 1, &asset()};
    effects_.controllerRequests.push_back({SourceControlOp::Ghost,effectTarget_});
    if (training_) trainingInput_ = FighterInputEvent{FighterCommand::Dash, 0, false, false, false, unsigned(kind)};
    return true;
}

void FighterMotion::startShadow(ShadowTint tint) {
    if (!shadowLifetime_) shadowLifetime_ = std::make_shared<std::byte>();
    shadowActive_ = true;
    shadowTint_ = tint;
}

void FighterMotion::stepGhostControls(FighterControls input, unsigned humanRequest, bool airBranch) {
    const auto state = aiState();
    if (defeated() || injured() || guardHit_ || state == AiState::Super || state == AiState::Ultimate ||
        state == AiState::Transform) return;
    const auto requested = [&](AiIntent intent, unsigned bit) {
        return input.aiControlled || input.sourceHuman ? bool(input.aiIntents & (1U << unsigned(intent))) : bool(humanRequest & bit);
    };
    // Keep the source's separate tests: simultaneous AI intents can pay twice.
    if (!airBranch && requested(AiIntent::GhostStep, 1)) startGhost(GhostKind::Step);
    if (requested(AiIntent::GhostJump, 2)) startGhost(GhostKind::Jump);
    if (airBranch && requested(AiIntent::GhostJumpDown, 1)) startGhost(GhostKind::Down);
}

void FighterMotion::endGhost() {
    effects_.controllerRequests.push_back({SourceControlOp::EndGhost,effectTarget_});
    ghostActive_ = shadowActive_ = inGhostEffect_ = false;
    ++ghostEnds_;
    if (target_) facing_ = motion_.position.x < target_->motion_.position.x ? 1 : -1;
    recover();
}

void FighterMotion::animateGhost() {
    if (ghostFrames_-- <= 0) {
        if (ghostKind_ == GhostKind::Jump) {
            const double vy = motion_.velocity.y;
            endGhost();
            motion_.setVelocity({0, vy});
            motion_.setDamping({0, -vy / 10});
            airMove_ = true;
            return;
        }
        endGhost();
    }
    // The source checks the bottom floor after its timer, even on the expiry frame.
    if (ghostKind_ == GhostKind::Down && motion_.position.y > ghostFloor_) endGhost();
}

void FighterMotion::finishGhostAnimation(FighterMotion* target) {
    if (!std::exchange(ghostAnimationPending_, false)) return;
    target_ = target;
    if (ghostActive_) animateGhost();
    stepCounterAnimate();
    target_ = nullptr;
}

void FighterMotion::step(const StageGeometry& stage, FighterControls input, bool animateFrame,
                         std::optional<CameraLimits> camera, FighterMotion* target, double rate,
                         std::span<const std::uint32_t> attackers, bool suspendGravity,bool selectedRender) {
    target_ = target;
    attackers_ = attackers;
    if(sourceControlOrder_&&!sourceActionEnabled_){clearInput();pendingHurtEscape_=false;input={};}
    if(input.sourceHuman) {
        constexpr unsigned vertical=(1U<<unsigned(HumanKey::Up))|(1U<<unsigned(HumanKey::Down));
        constexpr unsigned horizontal=(1U<<unsigned(HumanKey::Left))|(1U<<unsigned(HumanKey::Right));
        for(unsigned axis:{vertical,horizontal})if(input.pressedKeys&axis)pendingHumanPressed_&=~axis;
        pendingHumanPressed_|=input.pressedKeys;
        if(reactionPhase_+gameHz_>=deliveryHz_) {
            const auto pressed=std::exchange(pendingHumanPressed_,0U);
            // A 60 Hz display/link tap remains down for one actual controller
            // frame, including the alternate-frame 30 Hz Classic branch.
            if(pressed&~input.heldKeys)input=HumanInput::controlsForFrame(
                input.heldKeys|pressed,input.bufferedKeys|pressed,input.pressedKeys|pressed,input.classicInput);
        }
    }
    waterInput_ = input.horizontal || (input.sourceHuman ? bool(input.aiIntents & (1U << unsigned(AiIntent::SkillUp))) : input.vertical > 0 && input.attackHeld);
    tickBuffs();
    if(sourceControlOrder_&&ghostActive_){
        // A flat resume (e.g. freeze expiry) may let MainLogic run while the
        // ghost is still in BlackBack's selected lists. MainLogic applies its
        // own physics prelude, but FighterMcCtrler.render skips action input.
        motion_.speed=motion_.defaultSpeed()*rate;
        const bool gravity=motion_.applyGravity;
        if(suspendGravity)motion_.applyGravity=false;
        if(selectedRender)motion_.renderVelocity();else motion_.step(stage,camera);
        motion_.applyGravity=gravity;
        if(animateFrame)stepSourceCutinAnimation(target, true,stage,attackers);
        target_=nullptr;attackers_={};return;
    }
    const int previousMove = std::exchange(trainingMove_, 0);
    previousSourceMove_ = previousMove;
    stageRight_ = stage.right;
    ghostFloor_ = stage.playerBottom;
    if (input.aiControlled) {
        clearInput();
        input.vertical = input.aiIntents & (1U << unsigned(AiIntent::Guard)) ? -1 : 0;
    }
    if (input.attackPressed) attackBuffer_ = actionBufferTicks();
    else if (attackBuffer_) --attackBuffer_;
    if (input.specialPressed) specialBuffer_ = actionBufferTicks();
    else if (specialBuffer_) --specialBuffer_;
    if (input.superPressed) superBuffer_ = actionBufferTicks();
    else if (superBuffer_) --superBuffer_;
    if (input.dashPressed && !hurtEscaping_) dashBuffer_ = actionBufferTicks();
    else if (dashBuffer_) --dashBuffer_;
    if (input.jumpPressed) jumpBuffer_ = actionBufferTicks();
    else if (jumpBuffer_) --jumpBuffer_;
    if (justGuardTicks_ > 0) --justGuardTicks_;

    const bool wasInAir = motion_.airborne;
    bool branchedOnLanding = false;
    motion_.speed = motion_.defaultSpeed() * rate;
    const bool gravity = motion_.applyGravity;
    if (suspendGravity) motion_.applyGravity = false;
    if(selectedRender)motion_.renderVelocity();else motion_.step(stage,camera);
    if (suspendGravity) motion_.applyGravity = gravity;
    if(sourceControlOrder_&&!sourceActionEnabled_&&!motion_.airborne&&(clip_==MoveClip::Walk||guardClip()))recover();
    if (kamui_ || (sourcePhysics_?sourceAlphaRecovery_:opacity_ != 100)) stepKamui();
    if (twinLionCooldown_ || chakraTicks_ || repeatHit_) stepHinata();
    if (!motion_.airborne) {
        // A locked directional move consumes its buffered press before the direction can change.
        if (input.vertical > 0 && (kamui_ || timelineCounters_[TwinLionCooldownFlag] || chakraTicks_)) specialBuffer_ = 0;
        if (input.vertical < 0 && (timelineCounters_[TwinLionFlag] || chakraTicks_)) superBuffer_ = 0;
        if (input.sourceHuman) {
            if (input.upHeld && (kamui_ || timelineCounters_[TwinLionCooldownFlag] || chakraTicks_)) input.aiIntents &= ~(1U << unsigned(AiIntent::SpecialUp));
            if (input.downHeld && (timelineCounters_[TwinLionFlag] || chakraTicks_)) input.aiIntents &= ~(1U << unsigned(AiIntent::Ultimate));
        }
    }
    checkComboLimits();
    updateTargetMove();
    if (!input.aiControlled && !input.sourceHuman && input.dashPressed) pendingGhostRequests_ |= input.vertical < 0 ? 1U : input.vertical > 0 ? 2U : 0U;
    const bool actionRender = stepHurtEscape(input);
    const unsigned ghostRequest = actionRender ? std::exchange(pendingGhostRequests_, 0) : 0;
    if (actionRender && stepCounterRequest(input, stage, camera, rate)) {
        if(sourceControlOrder_&&animateFrame)animate(input.vertical<0);
        target_ = nullptr;
        attackers_ = {};
        return;
    }
    if (!injured() && !defeated() && !transforming_) {
        const bool ghostAirBranch = motion_.airborne;
        if (wasInAir && !motion_.airborne) {
            airAttackUsed_ = false;
            if (landingClip_ != MoveClip::Missing) {
                effects_.ground = GroundCueView{GroundCue::Land, motion_.position};
                jumpsLeft_ = 2;
                quickJumpDelay_ = 0;
                startAction(landingClip_, casting_);
                branchedOnLanding = true;
            } else if (!acting_) {
                effects_.ground = GroundCueView{GroundCue::Land, motion_.position};
                jumpsLeft_ = 2;
                quickJumpDelay_ = 0;
                motion_.setVelocity({});
                enter(MoveClip::Land);
            }
        }
        checkProbe();
        if (input.sourceHuman) {
            if (actionRender) {
                auto transformInput = input;
                transformInput.vertical = input.upHeld ? 1 : input.downHeld ? -1 : 0;
                if (const auto transform = chooseTransformation(transformInput); transform && qi_ >= FighterMaxQi) {
                    clearInput();addQi(-double(FighterMaxQi));transforming_=true;actionState_=AiState::Transform;
                    recordTrainingInput(transform.command);startAction(transform.clip);
                } else stepAiControls(input);
            }
        } else if (input.aiControlled) {
            stepAiControls(input);
        } else {
        const auto grab = chooseCatch(input);
        const auto super = grab ? SelectedAction{} : chooseSuper(input);
        auto attack = grab ? grab : super ? super : chooseAttack(input);
        if (!attack) attack = chooseSpecial(input);
        const MoveClip dash = chooseDash(input);
        const bool jump = jumpBuffer_ && !input.attackHeld;
        const bool groundedControl = acting_ ? groundCancels_ && !motion_.airborne :
            clip_ == MoveClip::Idle || clip_ == MoveClip::Walk;
        const bool walk = input.horizontal && (acting_ ?
            !motion_.airborne && (walkCancels_ & (input.horizontal < 0 ? 1U : 2U)) : groundedControl);
        if (const auto transform = chooseTransformation(input)) {
            clearInput();
            if (qi_ >= FighterMaxQi) {
                addQi(-double(FighterMaxQi));
                transforming_ = true;
                actionState_ = AiState::Transform;
                recordTrainingInput(transform.command);
                startAction(transform.clip);
            }
        } else if (attack) {
            if (motion_.airborne) {
                airAttackUsed_ = true;
                jumpsLeft_ = 0;
            }
            if (super) {
                addQi(-double(super.qiCost));
                superBuffer_ = 0;
            }
            recordTrainingInput(attack.command, grab ? input.horizontal : 0);
            if (grab.command == FighterCommand::CatchAttack) attackBuffer_ = 0;
            if (grab.command == FighterCommand::CatchSpecial) specialBuffer_ = 0;
            startAction(attack.clip, bool(super));
            actionState_ = commandState(attack.command);
        } else if (jump && !motion_.airborne && acting_ && jumpsLeft_ > 0 &&
                   (!groundCancels_ || input.vertical >= 0 || motion_.touchingBottom) &&
                   bindings_[static_cast<std::size_t>(FighterCommand::Jump)] != MoveClip::Missing) {
            const auto jumpClip = bindings_[static_cast<std::size_t>(FighterCommand::Jump)];
            clearActions();
            acting_ = false;
            recordTrainingInput(FighterCommand::Jump);
            enter(jumpClip);
        } else if (jump && motion_.airborne && !acting_ && jumpsLeft_ > 0 && !jumpDelay_ && !quickJumpDelay_ &&
                   (clip_ == MoveClip::JumpRise || clip_ == MoveClip::JumpApex || clip_ == MoveClip::Fall)) {
            clearActions();
            recordTrainingInput(FighterCommand::Jump);
            jumpDelay_ = 1;
        } else if (jump && input.vertical < 0 && !motion_.airborne && !motion_.touchingBottom &&
                   (groundedControl || (guardClip() && !guardHit_))) {
            recordTrainingInput(FighterCommand::Jump, 0, true);
            dropThrough();
        } else if (dash != MoveClip::Missing) {
            if (motion_.airborne) {
                jumpsLeft_ = 0;
                useEnergy(30);
            } else {
                if (input.horizontal) facing_ = input.horizontal;
                useEnergy(20);
            }
            recordTrainingInput(FighterCommand::Dash);
            startAction(dash);
            actionState_ = AiState::Dash;
            motion_.applyGravity = false;
            dodging_ = true;
        } else if (!acting_ && motion_.airborne && (guardClip() || guardRecovery())) {
            recover();
        } else if (guardClip() && input.horizontal && input.horizontal != facing_) {
            facing_ = input.horizontal;
            startGuard(guardHit_);
        } else if (!acting_ && motion_.airborne && (clip_ == MoveClip::Idle || clip_ == MoveClip::Walk)) {
            motion_.velocity.x = 0;
            airMove_ = true;
            enter(MoveClip::Fall);
        } else if (groundedControl || walk) {
            if (groundedControl && input.vertical < 0 && !flightMode_ && asset().has(asset().command(FighterCommand::Guard))) {
                recordTrainingInput(FighterCommand::Guard);
                startGuard();
            } else if (groundedControl && jump && input.vertical >= 0 && jumpsLeft_ > 0) {
                recordTrainingInput(FighterCommand::Jump);
                enter(MoveClip::JumpStart);
            } else if (walk) {
                if (training_) {
                    trainingMove_ = input.horizontal;
                    if (previousMove != input.horizontal) recordTrainingInput(FighterCommand::Count, input.horizontal);
                }
                if (acting_) {
                    clearActions();
                    acting_ = false;
                }
                if (clip_ != MoveClip::Walk) enter(MoveClip::Walk);
                facing_ = input.horizontal;
                motion_.setVelocity({speed() * facing_, 0});
            } else if (clip_ == MoveClip::Walk) {
                motion_.setVelocity({});
                enter(MoveClip::Idle);
            }
        }
        }
        if (actionRender) stepGhostControls(input, ghostRequest, ghostAirBranch);
        if (!ghostActive_ && motion_.airborne && airMove_ && clip_ != MoveClip::JumpStart) {
            motion_.move({speed() * input.horizontal, 0});
        }
    }
    if(animateFrame&&!branchedOnLanding){
        if(sourceControlOrder_&&cutin_.kind==CutinKind::GhostStep)
            stepSourceCutinAnimation(target_,true,stage,attackers_);
        else if(sourceControlOrder_||cutin_.kind==CutinKind::None)animate(input.vertical<0);
    }
    if (waterWalkClip_ != MoveClip::Missing) stepWaterWalk(input.horizontal);
    target_ = nullptr;
    attackers_ = {};
}

const FighterPose& FighterMotion::pose() const {
    const auto& current = frame();
    const bool twinLion = timelineCounters_[TwinLionFlag] != 0;
    if (flightMode_ || bankaiIntro_ || waterMoving_ || twinLion || mirroredEffect_) {
        for (const auto& event : asset().events(current)) {
            if ((flightMode_ && event.op == FighterOp::FlightPose) || (bankaiIntro_ && event.op == FighterOp::BankaiPose) ||
                (waterMoving_ && event.op == FighterOp::WaterPose) || (twinLion && event.op == FighterOp::TwinLionPose) ||
                (mirroredEffect_ && event.op == FighterOp::FacingPose)) {
                return asset().frame(MoveClip(event.target), unsigned(event.x)).pose;
            }
        }
    }
    return current.pose;
}

const FighterPose* FighterMotion::screenPose() const {
    const auto events = asset().events(frame());
    const auto screen = std::ranges::find(events, FighterOp::ScreenPose, &FighterEvent::op);
    return screen == events.end() ? nullptr : &asset().frame(MoveClip(screen->target), unsigned(screen->x)).pose;
}

void FighterMotion::stepCutin(FighterMotion* target, bool animateFrame, const StageGeometry& stage,
                              std::optional<CameraLimits> camera, double rate, std::span<const std::uint32_t> attackers) {
    ghostFloor_ = stage.playerBottom;
    if (cutin_.kind == CutinKind::GhostStep) {
        tickBuffs();
        motion_.speed = motion_.defaultSpeed() * rate;
        motion_.step(stage, camera);
        if (twinLionCooldown_ || chakraTicks_ || repeatHit_) {
            target_ = target;
            stepHinata();
            target_ = nullptr;
        }
    }
    if (!animateFrame) return;
    target_ = target;
    attackers_ = attackers;
    ++cutin_.frame;
    if (cutin_.kind == CutinKind::GhostStep) {
        if (justHurtResume_) {
            hittable_ = true;
            justHurtResume_ = false;
        }
        regenerateEnergy();
        if (assistCharge_ < AssistChargeMax) ++assistCharge_;
        if (hitGap_ && --hitGap_ == 0) hittable_ = true;
        if (!ghostActive_ || ghostKind_ == GhostKind::Step) advancePose();
        ghostAnimationPending_ = true;
    } else advancePose();
    target_ = nullptr;
    attackers_ = {};
}

void FighterMotion::stepSourceCutinRender(const StageGeometry& stage,std::optional<CameraLimits> camera,double rate,
                                         FighterControls input,FighterMotion* target,std::span<const std::uint32_t> attackers) {
    if(!ghostActive_){
        // Authored effect.ghostStep() does not set GhostHandler.ghostStepIng.
        // FighterMain.render therefore still runs its normal action controller.
        step(stage,input,false,camera,target,rate,attackers,false,true);
        motion_.fixPosition(stage,camera);return;
    }
    motion_.speed=motion_.defaultSpeed()*rate;
    // BlackBack calls FighterMain.render(), then fixGameSpritePosition(). It
    // does not run MainLogic's floor/gravity prelude for the selected ghost.
    motion_.renderVelocity();
    tickBuffs();
    if(twinLionCooldown_||chakraTicks_||repeatHit_)stepHinata();
    motion_.fixPosition(stage,camera);
}

void FighterMotion::stepSourceCutinAnimation(FighterMotion* target,bool animateFrame,const StageGeometry& stage,
                                           std::span<const std::uint32_t> attackers) {
    ghostFloor_=stage.playerBottom;
    if (!animateFrame) return;
    target_ = target;
    attackers_ = attackers;
    ++cutin_.frame;
    if (ghostActive_) {
        if (justHurtResume_) {
            hittable_ = true;
            justHurtResume_ = false;
        }
        regenerateEnergy();
        if (assistCharge_ < AssistChargeMax) ++assistCharge_;
        if (hitGap_ && --hitGap_ == 0) hittable_ = true;
        if (!ghostActive_ || ghostKind_ == GhostKind::Step) advancePose();
        ghostAnimationPending_ = true;
    } else animate(false);
    target_ = nullptr;
    attackers_ = {};
}

void FighterMotion::faceIdleTarget(const FighterMotion& target) {
    if (clip_ == MoveClip::Idle && ++autoFacingFrame_ > 5) {
        facing_ = motion_.position.x < target.motion_.position.x ? 1 : -1;
        autoFacingFrame_ = 0;
    }
}

void FighterMotion::resolveBodyContact(FighterMotion& other) {
    if (crossBody_ || other.crossBody_ || std::abs(motion_.position.x - other.motion_.position.x) > 200 ||
        std::abs(motion_.position.y - other.motion_.position.y) > 200) return;
    // The original pair query refreshes both shared body rectangles before its
    // null/overlap checks. MapLayer observes these references on the next render.
    const WorldBox a = body();
    const WorldBox b = other.body();
    if (a.empty() || b.empty()) return;
    if (!a.overlaps(b)) return;

    const bool aOnLeft = motion_.position.x < other.motion_.position.x;
    const double va = motion_.velocity.x;
    const double vb = other.motion_.velocity.x;
    if (aOnLeft ? (va < 0 && va < vb) || (vb > 0 && vb > va)
                : (va > 0 && va > vb) || (vb < 0 && vb < va)) return;

    const double fraction = std::clamp((double(other.asset().stats().weight) / asset().stats().weight) * 0.5, 0.1, 0.9);
    const auto transfer = [&](double velocity) {
        double aPart = velocity * fraction;
        double bPart = velocity * (1 - fraction);
        if (motion_.touchingSide || other.motion_.touchingSide) {
            aPart = other.motion_.touchingSide ? velocity : 0;
            bPart = motion_.touchingSide ? velocity : 0;
        }
        motion_.move({-aPart, 0});
        other.motion_.move({bPart, 0});
    };
    if (std::min(a.right, b.right) - std::max(a.left, b.left) > 2) {
        transfer((aOnLeft ? 5 : -5) * (sourcePhysics_?motion_.speed:SpeedPerTick));
    }
    if (va != 0) transfer(va);
    if (vb != 0) transfer(-vb);
}

std::optional<StrikeContact> FighterMotion::strikeAgainst(const FighterMotion& defender) const {
    if (!inStrikeRange(motion_.position, defender.motion_.position)) return std::nullopt;
    return strikeAgainst(defender.motion_.position, defender.hurtBody());
}

WorldBox FighterMotion::hurtBody() const {
    if (defeated() || !hittable_ || dodging_ || transforming_) return {};
    return body();
}

std::optional<StrikeContact> FighterMotion::strikeAgainst(Point target, WorldBox body) const {
    if (defeated() || body.empty() || !inStrikeRange(motion_.position, target)) return std::nullopt;
    if (explodeHit_ && explodeHitFrames_ < 8) {
        if (!explodeArea_.overlaps(body)) return std::nullopt;
        return StrikeContact{explodeHit_->strike, 1, counterOrigin_.x, 1, -1, explodeHit_, false,explodeArea_.intersection(body)};
    }
    const auto hitRegions = asset().hits(frame());
    for (const FighterHit& hit : hitRegions) {
        if (hit.strike() && worldBox(hit.box, motion_.position, facing_).overlaps(body)) {
            const auto model = runtimeHitModel();
            const auto& value = model->hits[hit.strikeIndex];
            return StrikeContact{value.strike, facing_, motion_.position.x, value.damageRate, value.customPower,
                std::shared_ptr<const RuntimeHit>(model, &value),true,
                worldBox(hit.box,motion_.position,facing_).intersection(body)};
        }
    }
    return std::nullopt;
}

const FighterMotion::StrikeReaction* FighterMotion::strikeReaction(unsigned index) const {
    return hitModel_ && index < hitModel_->hits.size() ? &hitModel_->hits[index] : nullptr;
}

std::shared_ptr<RuntimeHitModel> FighterMotion::runtimeHitModel() const {
    if (!hitModel_) hitModel_ = makeRuntimeHitModel(asset(), powerRate());
    return hitModel_;
}

void FighterMotion::updateHitRates() {
    if (hitModel_) for (auto& hit : hitModel_->hits) hit.damageRate = strikePowerRate(&hit);
}

double FighterMotion::strikePowerRate(const StrikeReaction* reaction) const {
    return reaction && reaction->powerRate >= 0 ?
        reaction->powerRate * (1 + buffValue(FighterBuff::Attack)) : powerRate();
}

void FighterMotion::setStrikeReaction(unsigned index, unsigned property, float value) {
    auto* reaction = &runtimeHitModel()->hits[index];
    if (property == 0) reaction->strike.hitX = value;
    else if (property == 1) reaction->strike.flags = (reaction->strike.flags & ~1U) | unsigned(value);
    else if (property == 2) reaction->powerRate = value;
    else if (property == 3) reaction->strike.flags = (reaction->strike.flags & ~2U) | (unsigned(value) << 1);
    else if (property == 4) {
        reaction->strike.damage = std::uint16_t(value);
        reaction->customPower = -1;
    }
    else if (property == 5) reaction->strike.hitType = std::uint16_t(value);
    else {
        reaction->strike.hurtTimeMs = std::uint16_t(value);
        const unsigned frames = (unsigned(value) * AnimationHz + 500) / 1000 + 3;
        reaction->strike.hurtFrames = std::max(4U, frames);
    }
    reaction->damageRate = strikePowerRate(reaction);
}

void FighterMotion::scoreStrike(const StrikeContact& contact,FighterMotion* target) {
    noteStrike(contact);
    if (!contact.strike.bisha()) addQi(std::min(15.0, contact.power() * 0.17));
    emitSourceHitTarget(contact,target);
}

void FighterMotion::scoreObjectStrike(const StrikeContact& contact,double rate,FighterMotion* target) {
    addQi(contact.power() * rate);
    emitSourceHitTarget(contact,target);
}

void FighterMotion::emitSourceHitTarget(const StrikeContact& contact,FighterMotion* target){
    if(!sourcePhysics_||!target||!sourceHooks_.hitTarget)return;
    const auto state=target->aiState();if(state!=AiState::Hurt&&state!=AiState::Flying)return;
    sourceHooks_.hitTarget(sourceHooks_.context,sourceHooks_.id,target->sourceHooks_.id,contact);
}

void FighterMotion::present(FighterCommand command) {
    const auto clip = asset().command(command);
    if (asset().has(clip)) {
        startAction(clip);
        actionState_ = commandState(command);
    }
}

void FighterMotion::cancelIntro() {
    if (performing(FighterCommand::Intro)) recover();
}

void FighterMotion::clearInput() {
    attackBuffer_ = specialBuffer_ = superBuffer_ = dashBuffer_ = jumpBuffer_ = 0;
    pendingGhostRequests_ = 0;
    pendingHumanPressed_ = 0;
}

bool FighterMotion::hasAction(FighterCommand command) const {
    const auto clip = acting_ ? bindings_[static_cast<std::size_t>(command)] : asset().command(command);
    return !defeated() && asset().has(clip);
}

AiState FighterMotion::aiState() const {
    if(sourcePhysics_&&sourceHurt_.pose()!=SourceHurtTimeline::Pose::None)return sourceHurt_.state();
    if (!sourcePhysics_&&defeated()) return AiState::Dead;
    if (ghostActive_) return ghostActionState_;
    if (transforming_) return AiState::Transform;
    if (hurtEscaping_) return actionState_;
    switch (flight_) {
    case Flight::Start: case Flight::Air: case Flight::Contact: return AiState::Flying;
    case Flight::Down: case Flight::Recover: return AiState::Down;
    case Flight::Bounce: return AiState::Bounce;
    case Flight::None: break;
    }
    if (clip_ == MoveClip::Hurt || clip_ == MoveClip::HurtRecover) return AiState::Hurt;
    if (guardClip() || guardRecovery()) return AiState::Guard;
    if (acting_) return actionState_;
    if (motion_.airborne || clip_ == MoveClip::JumpStart || jumpDelay_) return AiState::Jump;
    return AiState::Normal;
}

MoveClip FighterMotion::aiAction(FighterCommand command) const {
    if (defeated() || injured() || transforming_) return MoveClip::Missing;
    if (acting_) return bindings_[unsigned(command)];
    const auto clip = asset().command(command);
    if (!asset().has(clip)) return MoveClip::Missing;
    const bool air = command == FighterCommand::AirAttack || command == FighterCommand::AirSpecial ||
        command == FighterCommand::SuperAir;
    if (motion_.airborne) {
        return air || command == FighterCommand::Dash || command == FighterCommand::Jump ? clip : MoveClip::Missing;
    }
    if (air) return MoveClip::Missing;
    const bool low = command == FighterCommand::SkillDown || command == FighterCommand::SpecialDown ||
        command == FighterCommand::SuperDown;
    if (clip_ == MoveClip::Idle || (clip_ == MoveClip::Walk && !low) ||
        (guardClip() && !guardHit_ && low)) return clip;
    if (clip_ == MoveClip::Land && phase_ >= 2 && command == FighterCommand::Attack) return clip;
    return MoveClip::Missing;
}

AiHitTraits FighterMotion::aiHitTraits(FighterCommand command) const {
    auto result = asset().unusedAiHits(command);
    const auto strikes = asset().strikes();
    const auto mask = 1U << unsigned(command);
    for (unsigned i = 0; i < strikes.size(); ++i) {
        if (!(FighterAsset::aiHitCommands(strikes[i].key) & mask)) continue;
        const auto* reaction = strikeReaction(i);
        const auto& strike = reaction ? reaction->strike : strikes[i];
        result.breaksGuard = result.breaksGuard || strike.breaksGuard();
        result.knocksDown = result.knocksDown || strike.launches();
    }
    return result;
}

void FighterMotion::useEnergy(double value) {
    energy_ -= value;
    energyDelay_ = 0.8 * AnimationHz;
    if (energy_ < 0) {
        energy_ = 0;
        overloaded_ = true;
    }
}

void FighterMotion::regenerateEnergy() {
    if (energyDelay_) {
        --energyDelay_;
    } else if (energy_ < maxEnergy_) {
        if (overloaded_) {
            energy_ += 0.6;
            if (energy_ > 30) overloaded_ = false;
        } else {
            energy_ += guardClip() || guardRecovery() ? 0.8 : acting_ ? 1.1 : 2.0;
        }
        energy_ = std::min(maxEnergy_, energy_);
    }
}

bool FighterMotion::receiveGuardHit(const StrikeContact& contact) {
    const FighterStrike& strike = contact.strike;
    guardStrike_ = strike.key;
    if(allowLoseHp_)health_ = std::max(0.0, health_ - contact.damage() * 0.05 * defenseRate());
    if(sourcePhysics_&&defeated()){beginSourceHurt(contact);return true;}
    const double cost = strike.breaksGuard() ? 90 : std::min(50, int(contact.damage() * 0.2));
    const bool breaks = energy_ < cost;
    useEnergy(cost);
    if (breaks || defeated()) {
        if(allowLoseHp_)health_ = std::max(0.0, health_ - contact.damage() * 0.1 * defenseRate());
        overloaded_ = false;
        guardReady_ = guardHit_ = false;
        hitGap_ = strike.launches() ? 10 : HitGap;
        hittable_ = false;
        hurtFrames_ = 0.7 * LogicHz + 1;
        motion_.setVelocity({std::clamp(double(strike.hitX), 5.0, 10.0) * contact.facing, 0});
        motion_.setDamping({HurtDampingX, 0});
        if(sourcePhysics_){SourceHurtView view{*this};sourceHurt_.stun(42,false,view);}else enter(MoveClip::Hurt);
        return true;
    }
    guardHit_ = true;
    guardHold_ = strike.launches() ? 10 : std::clamp(int(strike.hurtTimeMs * .001 * (sourcePhysics_?gameHz_:LogicHz) * .2), 5, 10);
    hitGap_ = strike.launches() ? 8 : HitGap;
    hittable_ = false;
    motion_.setVelocity({double(strike.hitX) * contact.facing, 0});
    motion_.setDamping({1, 0});
    return false;
}

std::shared_ptr<EffectTarget> FighterMotion::effectTarget() const {
    effectTarget_->position=motion_.position;effectTarget_->facing=facing_;
    effectTarget_->state=int(aiState());effectTarget_->steel=steel_;effectTarget_->superSteel=superSteel_;
    return effectTarget_;
}

void FighterMotion::contactEffect(const StrikeContact& contact,ContactEffectKind kind) {
    if(contact.overlap.empty())return;
    const CombatEffect* effect=nullptr;
    const bool cached=kind==ContactEffectKind::Hurt||kind==ContactEffectKind::Guard;
    const unsigned slot=kind==ContactEffectKind::Guard?1:0;
    const bool enemy=kind==ContactEffectKind::Hurt&&musouEnemyEffects_;
    if(cached&&contact.runtime) {
        const auto& cache=contact.runtime->effects;
        if(cache.effects[slot]&&cache.enemy[slot]==enemy)effect=cache.effects[slot];
    }
    if(!effect) {
    switch(kind) {
    case ContactEffectKind::Hurt:effect=combatHitEffect(contact.strike.hitType,musouEnemyEffects_);break;
    // FighterMcHurtCtrl calls doDefenseEffect without its optional target.
    // Consequently this path uses normal defense records even for Musou actors.
    case ContactEffectKind::Guard:effect=combatDefenseEffect(contact.strike.hitType,defenseType());break;
    case ContactEffectKind::Steel:effect=combatSteelEffect(contact.strike.hitType);break;
    case ContactEffectKind::GuardBreak:effect=combatEffect("break_def");break;
    }
        if(cached&&contact.runtime) {
            contact.runtime->effects.effects[slot]=effect;
            contact.runtime->effects.enemy[slot]=enemy;
        }
    }
    if(!effect)return;
    const int direction=kind==ContactEffectKind::GuardBreak?facing_:
        effect->followDirection&&contact.hasOwner?contact.facing:1;
    const bool hasTarget=kind==ContactEffectKind::Hurt||kind==ContactEffectKind::Steel;
    effects_.contactEffects.push_back({effect,kind,contact.overlap.center(),direction,cached?contact.runtime:nullptr,
                                      hasTarget?effectTarget():nullptr});
}

bool FighterMotion::receiveStrike(const StrikeContact& contact, FighterMotion* attacker) {
    if (!hittable_) return false;
    if(sourcePhysics_)return receiveSourceStrike(contact,attacker);
    const FighterStrike& strike = contact.strike;
    const int damage = contact.damage();
    addQi(std::min(20.0, contact.power() * 0.08));
    if (hurtClip_ != MoveClip::Missing) {
        target_ = attacker;
        startAction(hurtClip_);
        target_ = nullptr;
        return false;
    }
    effects_.slowTimeMs = std::max(effects_.slowTimeMs, unsigned(strike.slowTimeMs));
    const bool armored = steel_ && (superSteel_ || (!overloaded_ &&
        !(strike.hitType == 11 && strike.breaksGuard()) && !strike.bisha()));
    const bool behind = strike.checksFacing() && contact.hasOwner && contact.facing == facing_ &&
        (motion_.position.x - contact.originX) * facing_ > 5;
    if (!steel_ && guardReady_ && !behind && !(strike.breaksGuard() && strike.hitType == 11)) {
        const bool hurt = receiveGuardHit(contact);
        contactEffect(contact,defeated()?ContactEffectKind::Hurt:ContactEffectKind::Guard);
        if(hurt&&!defeated())contactEffect(contact,ContactEffectKind::GuardBreak);
        if (hurt) rememberHurtHit(contact);
        return hurt;
    }
    hurtStrike_ = strike.key;
    const double damageRate = superSteel_ ? 0.3 : armored ? 0.65 : 1.0;
    if(allowLoseHp_)health_ = std::max(0.0, health_ - damage * damageRate * defenseRate());
    if (armored && !defeated()) {
        contactEffect(contact,ContactEffectKind::Steel);
        useEnergy(damage * (superSteel_ ? 0.2 : strike.breaksGuard() ? 1.0 : 0.4));
        if (!superSteel_) {
            const double x = double(strike.hitX) * contact.facing;
            const double y = strike.hitY;
            const double rate = strike.breaksGuard() ? 2 : 1;
            motion_.setSecondaryVelocity({x * rate, y * rate}, {std::abs(x * 0.1), std::abs(y * 0.1)});
        }
        hitGap_ = strike.launches() ? 10 : HitGap;
        hittable_ = false;
        return false;
    }

    contactEffect(contact,ContactEffectKind::Hurt);
    if(sourcePhysics_){beginSourceHurt(contact);rememberHurtHit(contact);return true;}

    clearActions();
    shadowActive_ = false;
    hurtEscaping_ = justHurtResume_ = false;
    acting_ = false;
    dodging_ = crossBody_ = guardReady_ = guardHit_ = false;
    motion_.applyGravity = true;
    double y = strike.hitY;
    if (motion_.airborne && y <= 0) y -= 3;
    else if (!motion_.airborne && y < 0) y -= 6;
    const double x = double(strike.hitX) * contact.facing;
    motion_.setVelocity({x, y});
    hitGap_ = HitGap;
    hittable_ = false;
    enter(MoveClip::Hurt);

    if (strike.launches() && asset().has(MoveClip::HurtFly) && asset().has(MoveClip::HurtFall) &&
        asset().has(MoveClip::HurtBounce) && asset().has(MoveClip::HurtDown) && asset().has(MoveClip::HurtRecover)) {
        if (x != 0) facing_ = x > 0 ? -1 : 1;
        heavyDown_ = y > 5;
        floorHitY_ = y;
        floorDownPlayed_ = false;
        flightFrames_ = (heavyDown_ || motion_.airborne) ? 0 : 15;
        reactionFrames_ = 2;
        lowestY_ = motion_.position.y;
        flight_ = Flight::Start;
        hurtDownFrame_ = 0;
        motion_.setDamping({0, HurtDampingY});
    } else {
        flight_ = Flight::None;
        hurtFrames_ = strike.hurtFrames + 1;
        motion_.setDamping({HurtDampingX, HurtDampingY});
    }
    rememberHurtHit(contact);
    return true;
}

void FighterMotion::advancePose() {
    const FighterClip& clip = asset().clip(clip_);
    if (phase_ + 1 < clip.count) {
        ++phase_;
        applyFrameEvents();
    } else if (clip.repeat == FinishClip) {
        recover();
    } else {
        const bool looped = phase_ != clip.repeat;
        phase_ = clip.repeat;
        // FighterMcCtrler.loop seeks with gotoAndStop, which executes the
        // destination frame script. A held last frame does not seek again.
        if (looped) applyFrameEvents();
    }
}

void FighterMotion::advanceFlight() {
    switch (flight_) {
    case Flight::Start:
    case Flight::Air:
        lowestY_ = std::min(lowestY_, motion_.position.y);
        --flightFrames_;
        if (flight_ == Flight::Start && --reactionFrames_ == 0) {
            flight_ = Flight::Air;
            enter(MoveClip::HurtFly);
        }
        if (flight_ == Flight::Air && flightFrames_ <= 0 && !motion_.airborne) {
            flight_ = Flight::Contact;
            reactionFrames_ = 2;
            enter(MoveClip::HurtFall);
        } else if (flight_ == Flight::Air) {
            advancePose();
        }
        break;
    case Flight::Contact:
        advancePose();
        if (--reactionFrames_ > 0) break;
        if (heavyDown_) {
            appendLandingImpact(effects_, true, motion_.position, motion_.position.y - lowestY_, floorHitY_, musouEnemyEffects_);
            flight_ = Flight::Down;
            reactionFrames_ = 30;
            motion_.setDamping({4, 0});
            enter(MoveClip::HurtDown);
        } else {
            flight_ = Flight::Bounce;
            reactionFrames_ = 2;
            motion_.velocity.y = appendLandingImpact(effects_, false, motion_.position,
                                                    motion_.position.y - lowestY_, floorHitY_, musouEnemyEffects_);
            enter(MoveClip::HurtBounce);
        }
        break;
    case Flight::Bounce:
        advancePose();
        if (--reactionFrames_ <= 0 && !motion_.airborne) {
            // playHurtDown emits once before its delayed lie transition.
            if (!floorDownPlayed_) appendFloorImpact(effects_, FloorImpact::Normal, motion_.position, 1, musouEnemyEffects_);
            floorDownPlayed_ = false;
            flight_ = Flight::Down;
            reactionFrames_ = 15;
            motion_.setDamping({2, 0});
            enter(MoveClip::HurtDown);
        }
        break;
    case Flight::Down:
        if (!defeated() && --reactionFrames_ <= 0) {
            flight_ = Flight::Recover;
            enter(MoveClip::HurtRecover);
        } else {
            advancePose();
        }
        break;
    case Flight::Recover:
        advancePose();
        break;
    case Flight::None:
        break;
    }
}

void FighterMotion::animateGuard(bool held) {
    // goFrame's delay callback runs in FighterMC before defense control. The
    // completed guard's frame-delay counter cannot postpone this callback.
    if(sourcePhysics_&&guardRecovery()&&sourceGuardResume_){
        if(--sourceGuardResume_>0)return;
        recover();return;
    }
    if (guardHit_) {
        if (guardHold_ > 0) --guardHold_;
        else if (motion_.velocity.x == 0) guardHit_ = false;
        return;
    }
    if (guardDelay_-- > 0) return;
    if (guardClip()) {
        if (held) guardReady_ = true;
        else if (guardDelay_ <= -5) {
            guardDelay_ = 1;
            sourceGuardResume_=sourcePhysics_?2:0;
            enter(asset().command(FighterCommand::GuardRecover));
        }
    } else {
        recover();
    }
}

void FighterMotion::animate(bool holdingGuard) {
    if (normalAirJumpDelay_ && --normalAirJumpDelay_ == 0) normalAirJumpReady_ = true;
    if (justHurtResume_) {
        hittable_ = true;
        justHurtResume_ = false;
    }
    regenerateEnergy();
    if (assistCharge_ < AssistChargeMax) ++assistCharge_;
    // Original renderBeHitGap only restores the shared flag at expiry. An
    // authored action or justHurtResume may restore it before the gap expires.
    if (hitGap_ && --hitGap_ == 0) hittable_ = true;
    if(sourcePhysics_&&sourceHurt_.active()){
        animateSourceHurt();stepCounterAnimate();return;
    }
    if (flight_ != Flight::None) {
        advanceFlight();
        stepCounterAnimate();
        return;
    }

    advancePose();
    if (quickJumpDelay_) --quickJumpDelay_;
    if (guardClip() || guardRecovery()) {
        animateGuard(holdingGuard);
    } else if (jumpDelay_ && --jumpDelay_ == 0) {
        launchJump();
    } else if (clip_ == MoveClip::JumpRise && motion_.velocity.y > -asset().stats().jumpPower * JumpApexThreshold) {
        enter(MoveClip::JumpApex);
        normalAirJumpDelay_ = 5;
    } else if (clip_ == MoveClip::Hurt && hurtFrames_ && --hurtFrames_ == 0) {
        if (!defeated()) {
            recover();
        } else if (asset().has(MoveClip::HurtBounce) && asset().has(MoveClip::HurtDown)) {
            appendFloorImpact(effects_, FloorImpact::Normal, motion_.position, 2, musouEnemyEffects_);
            floorDownPlayed_ = true;
            flight_ = Flight::Bounce;
            reactionFrames_ = 2;
            motion_.setDamping({4, HurtDampingY});
            enter(MoveClip::HurtBounce);
        }
    }
    stepCounterAnimate();
}

unsigned FighterMotion::timelineFrame() const {
    const auto first = asset().clip(clip_).timelineStart;
    const bool mainTimeline = clip_ == MoveClip::Walk || clip_ == MoveClip::Land || clip_ == MoveClip::Hurt ||
        clip_ == MoveClip::HurtFall || clip_ == MoveClip::HurtRecover || guardClip() || acting_;
    return first + (mainTimeline ? phase_ : 0);
}

} // namespace bvn
