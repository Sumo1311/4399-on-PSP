// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fighter_motion.hpp"
#include <cmath>

namespace bvn::recovery_qa {

struct Checks {
    unsigned error = 0, cases = 0, escapes = 0;
    StageGeometry stage{0, 2000, 800, 500, {}};
    unsigned tick = 0;

#define RECOVERY_REQUIRE(value) do { if (!(value)) { error = __LINE__; return false; } } while (false)

    void step(FighterMotion& actor, FighterControls input = {}) {
        actor.step(stage, input, tick++ % 2 == 0);
    }

    bool bounce(FighterMotion& actor, int direction = 1, unsigned damage = 40, bool heavy = false) {
        FighterStrike hit{};
        hit.key = 17; hit.damage = damage; hit.flags = 1;
        hit.hitX = 12; hit.hitY = heavy ? 8 : -8; hit.hurtFrames = 12;
        RECOVERY_REQUIRE(actor.receiveStrike({hit, direction, 100}));
        for (unsigned i = 0; i < 400 && actor.aiState() != AiState::Bounce; ++i) step(actor);
        RECOVERY_REQUIRE(actor.aiState() == AiState::Bounce);
        return true;
    }

    bool escape(const FighterAsset& asset, unsigned hz, int direction, bool cpu,
                std::span<const FighterAsset> recoveryForms = {}) {
        FighterMotion actor(recoveryForms.empty() ? std::span(&asset, 1) : recoveryForms,
            recoveryForms.empty() ? 0 : unsigned(recoveryForms.size() - 1));
        actor.reset({900, 500}, -direction);
        actor.startTraining();
        actor.setGameHz(hz);
        tick = 0;
        RECOVERY_REQUIRE(bounce(actor, direction));
        const auto state = actor.aiState();
        const auto vx = actor.motion().velocity.x;
        actor.setEnergy(100);
        FighterControls request{};
        if (cpu) {
            request.aiControlled = true;
            request.aiIntents = 1U << unsigned(AiIntent::DownJump);
        } else request.dashPressed = true;
        step(actor, request);
        if (!actor.hurtEscaping()) step(actor);
        RECOVERY_REQUIRE(actor.hurtEscaping());
        RECOVERY_REQUIRE(actor.aiState() == state);
        RECOVERY_REQUIRE(actor.energy() >= 70 && actor.energy() < 71);
        RECOVERY_REQUIRE(actor.hurtBody().empty());
        RECOVERY_REQUIRE(std::abs(actor.motion().velocity.x - vx) < 1e-8);
        RECOVERY_REQUIRE(std::abs(actor.motion().damping.x - vx * .1) < 1e-8);
        const auto history = actor.takeTrainingInput();
        RECOVERY_REQUIRE(history && history->command == FighterCommand::Dash && history->highlight);
        const auto before = actor.energy();
        step(actor, request);
        RECOVERY_REQUIRE(actor.energy() == before);
        FighterStrike interruption{}; interruption.damage = 200;
        RECOVERY_REQUIRE(!actor.receiveStrike({interruption, direction, 100}));
        RECOVERY_REQUIRE(actor.currentHurtDamage() == 40);
        for (unsigned i = 0; i < 400 && actor.hurtEscaping(); ++i) step(actor);
        RECOVERY_REQUIRE(!actor.hurtEscaping());
        RECOVERY_REQUIRE(actor.currentHurtDamage() == 0);
        RECOVERY_REQUIRE(!actor.aiHittable());
        actor.step(stage, {}, false);
        RECOVERY_REQUIRE(!actor.aiHittable());
        actor.step(stage, {}, true);
        RECOVERY_REQUIRE(actor.aiHittable());
        ++cases; ++escapes;
        return true;
    }

    bool limits(const FighterAsset& asset) {
        for (unsigned damage : {240U, 241U}) for (double energy : {29.99, 30.0}) {
            FighterMotion actor(asset);
            actor.reset({900, 500}, -1);
            tick = 0;
            RECOVERY_REQUIRE(bounce(actor, 1, damage));
            actor.setEnergy(energy);
            actor.step(stage, {.dashPressed = true}, false);
            const bool allowed = damage <= 240 && energy >= 30;
            RECOVERY_REQUIRE(actor.hurtEscaping() == allowed);
            RECOVERY_REQUIRE(std::abs(actor.energy() - energy + (allowed ? 30 : 0)) < 1e-8);
            ++cases;
        }
        for (unsigned kind = 0; kind < 3; ++kind) {
            FighterMotion actor(asset);
            actor.reset({900, 500}, -1);
            tick = 0;
            RECOVERY_REQUIRE(bounce(actor));
            FighterControls request{};
            if (kind == 0) request.jumpPressed = true;
            else if (kind == 1) {
                request.aiControlled = true; request.aiIntents = 1U << unsigned(AiIntent::Dash);
            }
            actor.step(stage, request, false);
            RECOVERY_REQUIRE(!actor.hurtEscaping() && actor.energy() == 100);
            ++cases;
        }
        FighterMotion early(asset);
        early.reset({900, 500}, -1);
        FighterStrike hit{}; hit.damage = 40; hit.flags = 1; hit.hitY = -8;
        RECOVERY_REQUIRE(early.receiveStrike({hit, 1, 100}));
        early.step(stage, {.dashPressed = true}, false);
        RECOVERY_REQUIRE(!early.hurtEscaping() && early.energy() == 100);
        ++cases;
        FighterMotion heavy(asset);
        heavy.reset({900, 500}, -1);
        hit.hitY = 8;
        RECOVERY_REQUIRE(heavy.receiveStrike({hit, 1, 100}));
        for (unsigned i = 0; i < 120 && heavy.aiState() != AiState::Normal; ++i) {
            step(heavy, {.dashPressed = true});
            RECOVERY_REQUIRE(!heavy.hurtEscaping());
        }
        RECOVERY_REQUIRE(!heavy.hurtEscaping() && heavy.energy() == 100);
        ++cases;
        return true;
    }

    bool mutableHits(const FighterAsset& asset) {
        FighterMotion owner(asset), target(asset);
        owner.reset({900, 500}, 1); target.reset({1000, 500}, -1);
        owner.present(FighterCommand::Attack);
        std::optional<StrikeContact> hit;
        for (unsigned i = 0; i < 100 && !hit; ++i) {
            hit = owner.strikeAgainst({900, 500}, {-10000, -10000, 10000, 10000});
            if (!hit) step(owner);
        }
        RECOVERY_REQUIRE(hit && hit->runtime && hit->power() > 0);
        const auto power = hit->power();
        RECOVERY_REQUIRE(target.receiveStrike(*hit, &owner));
        RECOVERY_REQUIRE(target.currentHurtDamage() == int(power));
        owner.applyBuff(FighterBuff::Attack, .5, 10);
        RECOVERY_REQUIRE(target.currentHurtDamage() == int(power * 1.5));
        const auto again = owner.strikeAgainst({900, 500}, {-10000, -10000, 10000, 10000});
        RECOVERY_REQUIRE(again && again->runtime == hit->runtime);
        // Let the original four-animation hit gap expire before a second contact.
        for (unsigned frame = 0; frame < 4; ++frame) target.step(stage, {}, true);
        RECOVERY_REQUIRE(target.receiveStrike(*again, &owner));
        RECOVERY_REQUIRE(target.currentHurtDamage() == 2 * int(power * 1.5));
        owner.applyBuff(FighterBuff::Attack, 0, 0);
        owner.setRoundStats(1000, 2);
        RECOVERY_REQUIRE(target.currentHurtDamage() == 2 * int(power * 2));
        owner.reset({900, 500}, 1);
        RECOVERY_REQUIRE(target.currentHurtDamage() == 2 * int(power * 2));
        target.reset({1000, 500}, -1);
        RECOVERY_REQUIRE(target.currentHurtDamage() == 0);
        ++cases;
        return true;
    }

#undef RECOVERY_REQUIRE
};
} // namespace bvn::recovery_qa
