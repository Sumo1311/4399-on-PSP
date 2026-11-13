// Adapted from 5DPLAY's FighterAILogic and FighterAILogicBase.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_ai_policy.hpp"

#include <algorithm>
#include <cmath>

namespace bvn {
namespace {

#include "fighter_ai_rates.hpp"

static_assert(SourceGameHz > 0 && SourceAiHz > 0);
bool attacking(AiState state) {
    return state == AiState::Attack || state == AiState::Skill || state == AiState::Super || state == AiState::Ultimate;
}

// Follow AVMPlus tie ordering through Ruffle's MIT-licensed qsort. See licenses/Ruffle-MIT.txt.
template<class Entry>
void sortPriorities(std::span<Entry> values) {
    const auto after = [](const auto& a, const auto& b) { return a.priority < b.priority; };
    if (values.size() < 2) return;
    if (values.size() == 2) {
        if (after(values[0], values[1])) std::swap(values[0], values[1]);
        return;
    }
    if (values.size() == 3) {
        if (after(values[0], values[1])) std::swap(values[0], values[1]);
        if (after(values[1], values[2])) {
            std::swap(values[1], values[2]);
            if (after(values[0], values[1])) std::swap(values[0], values[1]);
        }
        return;
    }
    std::swap(values[0], values[values.size() / 2]);
    std::size_t left = 0, right = values.size();
    for (;;) {
        do { ++left; } while (left < values.size() && !after(values[left], values[0]));
        do { --right; } while (right && !after(values[0], values[right]));
        if (right < left) break;
        std::swap(values[left], values[right]);
    }
    std::swap(values[0], values[right]);
    auto first = values.first(right), second = values.subspan(left);
    if (first.size() >= second.size()) std::swap(first, second);
    sortPriorities(first);
    sortPriorities(second);
}

} // namespace

FighterAiPolicy::FighterAiPolicy(unsigned level, std::uint32_t seed)
    : level_(std::clamp(level, 1U, 6U)), random_(seed ? seed : 1) {
    breakCache_.fill(-1);
}

bool FighterAiPolicy::chance(const Rate& rate) {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return (double(random_) / 4294967296.0) * 10 < rate[level_ - 1];
}

bool FighterAiPolicy::byTarget(const Rate& fallback, std::initializer_list<std::pair<AiState, const Rate*>> rates) {
    const auto state = current_->target.fighter ? current_->target.state : AiState::Unknown;
    for (auto [key, rate] : rates) if (key == state) return chance(*rate);
    return chance(fallback);
}

bool FighterAiPolicy::available(FighterCommand command) const {
    return current_->actions[unsigned(command)].available;
}

bool FighterAiPolicy::inRange(FighterCommand command) const {
    return current_->targetBody && current_->actions[unsigned(command)].inRange;
}

bool FighterAiPolicy::targetHittable() const {
    return current_->target.fighter ? current_->targetHittable : current_->targetBody;
}

bool FighterAiPolicy::breaksGuard(FighterCommand command) {
    auto& cached = breakCache_[unsigned(command)];
    if (cached < 0) cached = current_->actions[unsigned(command)].breaksGuard;
    return cached != 0;
}

bool FighterAiPolicy::knocksDown(FighterCommand command) {
    // The source writes its knockdown lookup into the break-action cache, not the knockdown cache.
    const bool value = current_->actions[unsigned(command)].knocksDown;
    breakCache_[unsigned(command)] = value;
    return value;
}

void FighterAiPolicy::order(AiIntent intent, unsigned priority) {
    if (priorityCount_ < priorities_.size()) priorities_[priorityCount_++] = {intent, priority};
}

void FighterAiPolicy::resolveOrder() {
    sortPriorities(std::span(priorities_).first(priorityCount_));
    for (unsigned i = 1; i < priorityCount_; ++i) flag(priorities_[i].intent) = false;
    priorityCount_ = 0;
}

void FighterAiPolicy::movement() {
    flag(AiIntent::Left) = flag(AiIntent::Right) = false;
    const auto& self = current_->self;
    const auto& target = current_->target;
    if (self.state == AiState::Guard) {
        if (chance({1,2,4,5,6,8})) {
            flag(AiIntent::Left) = self.x > target.x + 20;
            flag(AiIntent::Right) = self.x < target.x - 20;
        }
        return;
    }
    bool moving;
    if (moveFrames_ < current_->gameHz) {
        ++moveFrames_;
        moving = true;
    } else {
        moving = byTarget(R_MV, {{AiState::Attack,&R_MV_ATK}, {AiState::Skill,&R_MV_SK},
            {AiState::Super,&R_MV_BS}, {AiState::Ultimate,&R_MV_BS}});
        if (moving) moveFrames_ = 0;
    }
    if (moving) {
        const bool catching = (intent(AiIntent::CatchAttack) || intent(AiIntent::CatchSpecial)) &&
            std::abs(self.y - target.y) < 2;
        const double keep = catching ? 15 : 40;
        flag(AiIntent::Left) = self.x > target.x + keep;
        flag(AiIntent::Right) = self.x < target.x - keep;
    }
    if (!intent(AiIntent::Left) && !intent(AiIntent::Right)) moveFrames_ = current_->gameHz;
}

void FighterAiPolicy::jump() {
    if (continuing_) flag(AiIntent::Jump) = byTarget(R_JP_C, {{AiState::Hurt,&R_JP_CH}});
    else if (current_->self.y > current_->target.y + 50) {
        flag(AiIntent::Jump) = byTarget(R_JP_UP, {{AiState::Super,&R_MV_BS}, {AiState::Ultimate,&R_MV_BS}});
    } else {
        flag(AiIntent::Jump) = byTarget(R_JP_LO, {{AiState::Super,&R_JP_LB}, {AiState::Ultimate,&R_JP_LB}});
    }
    if (continuing_ && intent(AiIntent::Jump)) order(AiIntent::Jump, 10);
}

void FighterAiPolicy::jumpDown() {
    flag(AiIntent::JumpDown) = current_->self.y < current_->target.y - 50
        ? byTarget(R_JP_UP, {{AiState::Super,&R_MV_BS}, {AiState::Ultimate,&R_MV_BS}})
        : byTarget(R_JP_LO, {{AiState::Super,&R_JP_LB}, {AiState::Ultimate,&R_JP_LB}});
}

void FighterAiPolicy::dash() {
    const auto& self = current_->self;
    const auto& target = current_->target;
    const double distance = std::abs(self.x - target.x);
    const int direction = target.x > self.x ? 1 : -1;
    if (self.energy < 40) {
        flag(AiIntent::Dash) = distance > 200 && self.facing == direction
            ? byTarget(R_DSH1, {{AiState::Attack,&R_DSH2}, {AiState::Skill,&R_DSH2},
                               {AiState::Super,&R0}, {AiState::Ultimate,&R0}})
            : byTarget(R_DSH3, {{AiState::Attack,&R0}, {AiState::Skill,&R0},
                               {AiState::Super,&R0}, {AiState::Ultimate,&R0}});
    } else {
        flag(AiIntent::Dash) = distance > 200 && self.facing != target.facing
            ? byTarget(R_DSH4, {{AiState::Attack,&R_DSH5}, {AiState::Skill,&R_DSH6},
                               {AiState::Super,&R0}, {AiState::Ultimate,&R0}})
            : byTarget(R_DSH7, {{AiState::Attack,&R0}, {AiState::Skill,&R0},
                               {AiState::Super,&R0}, {AiState::Ultimate,&R0}});
    }
}

void FighterAiPolicy::defense() {
    if (intent(AiIntent::Guard)) {
        flag(AiIntent::Guard) = byTarget(R_DEF_C, {{AiState::Freeze,&R_DEF_F}, {AiState::Normal,&R_DEF_N},
            {AiState::Hurt,&R0}, {AiState::Flying,&R0}, {AiState::Down,&R0}});
        if (intent(AiIntent::Guard)) return;
    }
    const auto& self = current_->self;
    const auto& target = current_->target;
    const bool near = std::abs(self.x - target.x) < 100 && std::abs(self.y - target.y) < 100;
    flag(AiIntent::Guard) = byTarget(R0, {{AiState::Attack,near ? &R_DEF_N2 : &R_DEF_F2},
        {AiState::Skill,&R_DEF_SK}, {AiState::Super,&R_DEF_BS}, {AiState::Ultimate,&R_DEF_BS}});
    if (intent(AiIntent::Guard)) return;
    for (const auto& threat : current_->threats) {
        if (!threat.attacking) continue;
        switch (threat.kind) {
        case AiThreatKind::Projectile:
            flag(AiIntent::Guard) = std::abs(self.x - threat.x) < 200 && std::abs(self.y - threat.y) < 150
                ? chance({2,4,5,7,9,10}) : chance({2,4,5,5,2,1});
            break;
        case AiThreatKind::Attacker: flag(AiIntent::Guard) = chance({2,4,5,7,9,10}); break;
        case AiThreatKind::Assister: flag(AiIntent::Guard) = chance({2,4,6,8,10,10}); break;
        case AiThreatKind::Other: break;
        }
        if (intent(AiIntent::Guard)) return;
    }
}

void FighterAiPolicy::attack() {
    flag(AiIntent::Attack) = flag(AiIntent::AirAttack) = false;
    if ((!available(FighterCommand::Attack) && !available(FighterCommand::AirAttack)) || !targetHittable()) return;
    if (!byTarget(continuing_ ? R_ATK_C : R_ATK_D, {{AiState::Guard,&R_ATK_DF}, {AiState::Counter,&R_ATK_HA},
            {AiState::Attack,&R_ATK_A}, {AiState::Skill,&R0}, {AiState::Super,&R0}, {AiState::Ultimate,&R0}})) return;
    unsigned priority;
    if (continuing_) {
        flag(AiIntent::Attack) = true;
        flag(AiIntent::AirAttack) = current_->self.y < current_->target.y;
        priority = current_->basicAttack ? 200 : 10;
    } else {
        flag(AiIntent::Attack) = inRange(FighterCommand::Attack);
        flag(AiIntent::AirAttack) = inRange(FighterCommand::AirAttack) && current_->self.y < current_->target.y;
        priority = 300;
    }
    if (intent(AiIntent::Attack)) order(AiIntent::Attack, priority);
    if (intent(AiIntent::AirAttack)) order(AiIntent::AirAttack, priority);
}

bool FighterAiPolicy::skill(FighterCommand command, AiIntent choice) {
    const bool breaking = breaksGuard(command);
    const Rate& fallback = breaking ? (continuing_ ? R_SK_B1 : R_SK_B2) : (continuing_ ? R_SK_N1 : R_SK_N2);
    const Rate& guard = breaking ? (continuing_ ? R_SK_B3 : R_SK_B4) : R_SK_N3;
    const bool selected = byTarget(fallback, {{AiState::Guard,&guard}, {AiState::Counter,&R_SK_HA},
        {AiState::Attack,&R_SK_AT}, {AiState::Skill,&R0}, {AiState::Super,&R0}, {AiState::Ultimate,&R0}}) &&
        targetHittable() && inRange(command);
    if (selected && continuing_) order(choice, knocksDown(command) ? 10 : 110);
    return selected;
}

void FighterAiPolicy::skills() {
    for (auto [command, choice] : {std::pair{FighterCommand::SkillDown,AiIntent::SkillDown},
             {FighterCommand::SkillUp,AiIntent::SkillUp}, {FighterCommand::Special,AiIntent::Special},
             {FighterCommand::SpecialDown,AiIntent::SpecialDown}, {FighterCommand::SpecialUp,AiIntent::SpecialUp},
             {FighterCommand::AirSpecial,AiIntent::AirSpecial}}) {
        flag(choice) = available(command) && skill(command, choice);
    }
}

bool FighterAiPolicy::super(FighterCommand command, AiIntent choice, unsigned qi, unsigned priority) {
    bool selected = false;
    if (current_->self.qi >= qi) {
        const bool breaking = breaksGuard(command);
        const Rate& fallback = breaking ? (continuing_ ? R_BS_B1 : R_BS_B2) : (continuing_ ? R_BS_N1 : R_BS_N2);
        const Rate& guard = breaking ? (continuing_ ? R_BS_B3 : R_BS_B4) : (continuing_ ? R_BS_D1 : R_BS_D2);
        const Rate& hurt = breaking ? fallback : R_BS_H;
        const Rate& jumping = breaking ? fallback : R_BS_J;
        selected = byTarget(fallback, {{AiState::Guard,&guard}, {AiState::Hurt,&hurt}, {AiState::Jump,&jumping},
            {AiState::Counter,&R_BS_HA}, {AiState::Attack,&R_BS_AT}, {AiState::Skill,&R_BS_SK},
            {AiState::Super,&R_BS_SK}, {AiState::Ultimate,&R_BS_SK}}) && targetHittable() && inRange(command);
    }
    if (selected && continuing_) order(choice, priority);
    return selected;
}

void FighterAiPolicy::supers() {
    flag(AiIntent::Super) = available(FighterCommand::Super) && super(FighterCommand::Super, AiIntent::Super, 100, 100);
    flag(AiIntent::SuperUp) = available(FighterCommand::SuperUp) && super(FighterCommand::SuperUp, AiIntent::SuperUp, 100, 100);
    flag(AiIntent::Ultimate) = available(FighterCommand::SuperDown) && super(FighterCommand::SuperDown, AiIntent::Ultimate, 300, 200);
    flag(AiIntent::AirSuper) = available(FighterCommand::SuperAir) && super(FighterCommand::SuperAir, AiIntent::AirSuper, 100, 210);
}

void FighterAiPolicy::catches() {
    flag(AiIntent::CatchAttack) = flag(AiIntent::CatchSpecial) = false;
    const auto state = current_->target.state;
    if (current_->target.fighter && (state == AiState::Hurt || state == AiState::Flying || state == AiState::Down)) return;
    const bool close = std::abs(current_->self.x - current_->target.x) < 50;
    const Rate& fallback = close ? R_CA_N : R_CA_NF;
    const Rate& guard = close ? R_CA_D : R_CA_DF;
    flag(AiIntent::CatchAttack) = byTarget(fallback, {{AiState::Guard,&guard}}) && targetHittable();
    flag(AiIntent::CatchSpecial) = byTarget(fallback, {{AiState::Guard,&guard}}) && targetHittable();
    if (intent(AiIntent::CatchAttack)) order(AiIntent::CatchAttack, 150);
    if (intent(AiIntent::CatchSpecial)) order(AiIntent::CatchSpecial, 110);
}

void FighterAiPolicy::counter() {
    const auto& self = current_->self;
    if (self.state != AiState::Hurt || self.health > self.maxHealth * 0.6 || self.qi < 150) return;
    flag(AiIntent::Counter) = byTarget(R_BRK, {{AiState::Skill,&R_BRK_S}});
}

void FighterAiPolicy::assist() {
    flag(AiIntent::Assist) = byTarget(R_CALL, {{AiState::Guard,&R_CALL_D}, {AiState::Super,&R0}, {AiState::Ultimate,&R0}});
}

void FighterAiPolicy::ghost() {
    const auto& self = current_->self;
    const auto& target = current_->target;
    if (std::abs(self.x - target.x) > 80 || std::abs(self.y - target.y) > 80 || self.qi < 200 ||
        (self.state != AiState::Attack && self.state != AiState::Skill)) return;
    flag(AiIntent::GhostStep) = byTarget(R0, {{AiState::Hurt,&R_GS}});
    const double dy = target.y - self.y;
    // Preserve the original contradictory vertical guards; these paths are unreachable in that source.
    flag(AiIntent::GhostJump) = dy < -80 && dy > -100 && byTarget(R0, {{AiState::Hurt,&R_GS_J}});
    flag(AiIntent::GhostJumpDown) = dy < 80 && dy > 100 && byTarget(R0, {{AiState::Hurt,&R_GS_D}});
}

std::uint32_t FighterAiPolicy::step(const AiObservation& observation) {
    current_ = &observation;
    continuing_ = attacking(observation.self.state);
    // Base.render resolves action priority before the subclass adds this frame's jump priority.
    dash();
    attack();
    skills();
    supers();
    catches();
    assist();
    resolveOrder();
    movement();
    jump();
    jumpDown();
    flag(AiIntent::DownJump) = byTarget(R_DJ, {{AiState::Skill,&R0}, {AiState::Super,&R0}, {AiState::Ultimate,&R0}});
    defense();
    counter();
    ghost();
    std::uint32_t result = 0;
    for (unsigned index = 0; index < flags_.size(); ++index) result |= std::uint32_t(flags_[index]) << index;
    current_ = nullptr;
    return result;
}

} // namespace bvn
