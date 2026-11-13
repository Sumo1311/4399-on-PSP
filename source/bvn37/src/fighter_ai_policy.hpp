// Adapted from 5DPLAY's FighterAILogic and FighterAILogicBase.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fighter_asset.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>

namespace bvn {

enum class AiState : int {
    Unknown = -1, Normal = 0, Attack = 10, Skill = 11, Super = 12, Ultimate = 13,
    Jump = 14, Dash = 15, Counter = 16, Guard = 20, Hurt = 21, Flying = 22,
    Down = 23, Bounce = 24, Dead = 30, Freeze = 40, Transform = 50, Intro = 60, Win = 61, Lose = 62
};

enum class AiIntent : unsigned {
    Left, Right, Jump, JumpDown, Dash, DownJump, Guard, Attack, AirAttack, AirSpecial,
    AirSuper, SkillDown, SkillUp, Special, SpecialDown, SpecialUp, CatchAttack, CatchSpecial,
    Super, SuperUp, Ultimate, Assist, Counter, GhostStep, GhostJump, GhostJumpDown, Count
};

struct AiFighterState {
    double x = 0, y = 0, health = 1000, maxHealth = 1000, qi = 0, energy = 100;
    int facing = 1;
    AiState state = AiState::Normal;
    bool fighter = true;
};

struct AiActionState {
    bool available = false, inRange = false, breaksGuard = false, knocksDown = false;
};

enum class AiThreatKind { Other, Projectile, Attacker, Assister };
struct AiThreat {
    AiThreatKind kind = AiThreatKind::Other;
    double x = 0, y = 0;
    bool attacking = false;
};

struct AiObservation {
    AiFighterState self, target;
    std::array<AiActionState, static_cast<unsigned>(FighterCommand::Count)> actions{};
    std::span<const AiThreat> threats{};
    bool targetHittable = true, targetBody = true, basicAttack = false;
    unsigned gameHz = 60; // GameConfig.FPS_GAME after applying the source quality setting.
};

class FighterAiPolicy {
public:
    // Update at original renderAnimate cadence; movement's hold counter still uses FPS_GAME.
    explicit FighterAiPolicy(unsigned level = 1, std::uint32_t seed = 1);
    std::uint32_t step(const AiObservation& observation);
    std::uint32_t randomState() const { return random_; }
    unsigned movementFrames() const { return moveFrames_; }
    unsigned level()const{return level_;}
    bool intent(AiIntent value) const { return flags_[static_cast<unsigned>(value)]; }

private:
    using Rate = std::array<double, 6>;
    bool chance(const Rate& rate);
    bool byTarget(const Rate& fallback, std::initializer_list<std::pair<AiState, const Rate*>> rates);
    bool& flag(AiIntent intent) { return flags_[static_cast<unsigned>(intent)]; }
    bool available(FighterCommand command) const;
    bool inRange(FighterCommand command) const;
    bool breaksGuard(FighterCommand command);
    bool knocksDown(FighterCommand command);
    bool targetHittable() const;
    void order(AiIntent intent, unsigned priority);
    void resolveOrder();
    void movement();
    void jump();
    void jumpDown();
    void dash();
    void defense();
    void attack();
    void skills();
    void supers();
    bool skill(FighterCommand command, AiIntent intent);
    bool super(FighterCommand command, AiIntent intent, unsigned qi, unsigned priority);
    void catches();
    void counter();
    void assist();
    void ghost();

    struct Priority { AiIntent intent; unsigned priority; };
    std::array<Priority, 32> priorities_{};
    unsigned priorityCount_ = 0, level_, moveFrames_ = 0;
    std::uint32_t random_;
    bool continuing_ = false;
    std::array<bool, static_cast<unsigned>(AiIntent::Count)> flags_{};
    std::array<signed char, static_cast<unsigned>(FighterCommand::Count)> breakCache_{};
    const AiObservation* current_ = nullptr;
};

} // namespace bvn
