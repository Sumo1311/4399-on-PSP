// Adapted from 5DPLAY Game Studio's FighterAILogic.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_cpu.hpp"

#include <type_traits>

namespace bvn {
namespace {

bool inRange(const FighterMotion& fighter, FighterCommand command, WorldBox target) {
    const auto& range = fighter.asset().aiRange(command);
    return fighter.hasAction(command) && !range.empty() && !target.empty() &&
        worldBox(range, fighter.motion().position, fighter.facing()).overlaps(target);
}

} // namespace

bool FighterCpu::chance(unsigned perThousand) {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_ % 1000 < perThousand;
}

FighterControls FighterCpu::step(const FightMatch& match, unsigned side) {
    if (observedAnimation_ != match.aiAnimation(side)) update(match, side);
    return controls();
}

FighterControls FighterCpu::controls() const {
    const auto has = [&](AiIntent flag) { return bool(intents_ & (1U << unsigned(flag))); };
    FighterControls input{};
    input.aiControlled = true;
    input.aiIntents = intents_;
    input.horizontal = int(has(AiIntent::Right)) - int(has(AiIntent::Left));
    input.assistHeld = has(AiIntent::Assist);
    return input;
}

AiObservation FighterCpu::observe(const FightMatch& match, unsigned side) {
    return observeActors(match.actor(side),match.actor(1-side),match.objects(),side);
}

AiObservation FighterCpu::observeActors(const FighterMotion& self,const FighterMotion& target,
                                       std::span<const FightObject> objects,unsigned ownId,unsigned partySize){
    const auto state = [](const FighterMotion& fighter) {
        const auto position = fighter.motion().position;
        return AiFighterState{position.x, position.y, fighter.health(), fighter.maxHealth(),
                              fighter.qi(), fighter.energy(), fighter.facing(), fighter.aiState(), true};
    };
    AiObservation observation{};
    observation.self = state(self);
    observation.target = state(target);
    observation.gameHz = gameHz_;
    const auto targetBody = target.body();
    observation.targetBody = !targetBody.empty();
    observation.targetHittable = target.aiHittable();
    const auto attack = self.aiAction(FighterCommand::Attack);
    observation.basicAttack = attack != MoveClip::Missing && attack == self.asset().command(FighterCommand::Attack);
    for (unsigned i = 0; i < observation.actions.size(); ++i) {
        const auto command = FighterCommand(i);
        const auto& range = self.asset().aiRange(command);
        const auto traits = self.aiHitTraits(command);
        observation.actions[i] = {self.aiAction(command) != MoveClip::Missing,
            !range.empty() && !targetBody.empty() && worldBox(range, self.motion().position, self.facing()).overlaps(targetBody),
            traits.breaksGuard, traits.knocksDown};
    }
    unsigned count = 0;
    for (const auto& object : objects) {
        std::visit([&](const auto& member) {
            if ((partySize?member.owner()>=partySize:member.owner()==ownId) || count == threats_.size()) return;
            const auto point = member.position();
            AiThreatKind kind;
            if constexpr (std::is_same_v<std::decay_t<decltype(member)>, FightProjectile>) kind = AiThreatKind::Projectile;
            else kind = member.assister() ? AiThreatKind::Assister : AiThreatKind::Attacker;
            threats_[count++] = {kind, point.x, point.y, member.attacking()};
        }, object);
    }
    observation.threats = std::span(threats_).first(count);
    return observation;
}

void FighterCpu::update(const FightMatch& match, unsigned side) {
    last_ = observe(match, side);
    intents_ = policy_.step(last_);
    observedAnimation_ = match.aiAnimation(side);
    ++decisions_;
}

void FighterCpu::updateSourceBoss(const FighterMotion& self,const FighterMotion& opponent,
                                 std::span<const FightObject> objects,unsigned partySize){
    last_=observeActors(self,opponent,objects,0,partySize);intents_=policy_.step(last_);
    // EnemyBossAICtrl inherits the normal logic/priority calculation, then
    // overrides these input getters to false. Their priority side effects
    // are therefore retained rather than excluding them from the policy.
    for(auto flag:{AiIntent::Ultimate,AiIntent::Assist,AiIntent::GhostStep,AiIntent::GhostJump,
                   AiIntent::GhostJumpDown,AiIntent::CatchAttack,AiIntent::CatchSpecial})
        intents_&=~(1U<<unsigned(flag));
    ++decisions_;
}

FighterControls FighterCpu::stepBoss(const FighterMotion& self, const FighterMotion& opponent,
                                     std::span<const FightObject> objects, unsigned partySize, bool animate) {
    if (animate) bossInput_ = step(self, opponent, objects, partySize, true);
    return bossInput_;
}

FighterControls FighterCpu::step(const FighterMotion& self, const FighterMotion& opponent,
                                 std::span<const FightObject> objects, unsigned side, bool boss) {
    const auto here = self.motion().position;
    const auto there = opponent.motion().position;
    const double dx = there.x - here.x, dy = there.y - here.y;
    const double distance = std::abs(dx);
    const int direction = dx >= 0 ? 1 : -1;
    const bool airborne = self.motion().airborne;
    const bool close = distance < 100 && std::abs(dy) < 100;
    FighterControls input;

    // The default Flash AI walks to within 40 px and sometimes pauses to reassess the target.
    const unsigned moveChance = boss ? (opponent.casting() ? 100 : opponent.acting() ? 400 : 500)
                                     : (opponent.acting() ? 200 : 300);
    if (moveTicks_ >= LogicHz && chance(moveChance)) moveTicks_ = 0;
    if (distance > 40 && moveTicks_ < LogicHz) {
        input.horizontal = direction;
        ++moveTicks_;
    } else {
        moveTicks_ = LogicHz;
    }

    const bool skill = boss && (opponent.performing(FighterCommand::Special) ||
        opponent.performing(FighterCommand::SpecialDown) || opponent.performing(FighterCommand::SpecialUp) ||
        opponent.performing(FighterCommand::AirSpecial) || opponent.performing(FighterCommand::SkillDown) ||
        opponent.performing(FighterCommand::SkillUp));
    const unsigned defenseChance = boss ? (opponent.casting() ? 400 : skill ? 300 : 100) : 50;
    bool threat = opponent.acting() && (defending_ || chance(defenseChance));
    for (const auto& object : objects) {
        const bool near = std::visit([&](const auto& member) {
            const auto point = member.position();
            const bool hostile = boss ? member.owner() < side : member.owner() != side;
            return hostile && std::abs(point.x - here.x) < 200 && std::abs(point.y - here.y) < 150;
        }, object);
        if (near && chance(boss ? 400 : 200)) threat = true;
    }
    defending_ = !airborne && (threat || (defending_ && chance(boss ? 100 : 200)));
    if (defending_) {
        input.vertical = -1;
        input.horizontal = distance > 20 ? direction : 0;
        return input;
    }

    if (dy < -50 && chance(boss ? 300 : 200)) input.jumpPressed = true;
    if (dy > 50 && !airborne && !self.motion().touchingBottom && chance(boss ? 300 : 200)) {
        input.vertical = -1;
        input.jumpPressed = true;
        return input;
    }
    if (distance > 200 && self.energy() >= 40 && self.facing() == direction && chance(boss ? 100 : 50)) {
        input.dashPressed = true;
        return input;
    }

    const WorldBox target = boss ? opponent.body() : opponent.hurtBody();
    if (airborne) {
        input.attackPressed = dy > 0 && inRange(self, FighterCommand::AirAttack, target) && chance(boss ? 100 : 50);
        if (boss && !input.attackPressed) {
            input.specialPressed = inRange(self, FighterCommand::AirSpecial, target) && chance(50);
            input.superPressed = !input.specialPressed && self.qi() >= 100 &&
                inRange(self, FighterCommand::SuperAir, target) && chance(50);
        }
        return input;
    }
    const bool continuing = self.acting() && close;
    if (self.hasAction(FighterCommand::Attack) &&
        (continuing || inRange(self, FighterCommand::Attack, target)) &&
        chance(boss ? (continuing ? 200 : 100) : (continuing ? 100 : 50))) {
        input.attackPressed = true;
        return input;
    }
    if ((boss || opponent.guarding()) && inRange(self, FighterCommand::SkillDown, target) && chance(50)) {
        input.vertical = -1;
        input.attackPressed = true;
        return input;
    }
    if (inRange(self, FighterCommand::SkillUp, target) && chance(boss ? 50 : 30)) {
        input.vertical = 1;
        input.attackPressed = true;
        return input;
    }
    if (inRange(self, FighterCommand::Special, target) && chance(boss ? 50 : 35)) {
        input.specialPressed = true;
    } else if (inRange(self, FighterCommand::SpecialDown, target) && chance(boss ? 50 : 30)) {
        input.vertical = -1;
        input.specialPressed = true;
    } else if (inRange(self, FighterCommand::SpecialUp, target) && chance(boss ? 50 : 20)) {
        input.vertical = 1;
        input.specialPressed = true;
    } else if (boss && self.qi() >= 100) {
        // EnemyBossAICtrl disables the 300-qi super and retains the two ordinary finishers.
        if (inRange(self, FighterCommand::Super, target) && chance(50)) input.superPressed = true;
        else if (inRange(self, FighterCommand::SuperUp, target) && chance(50)) {
            input.vertical = 1;
            input.superPressed = true;
        }
    }
    return input;
}

} // namespace bvn
