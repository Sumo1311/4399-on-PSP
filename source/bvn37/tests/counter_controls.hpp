// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include <cmath>

namespace bvn::counter_qa {
struct Checks {
    unsigned cases = 0, starts = 0, error = 0;
    StageGeometry stage{0, 4000, 800, 500, {}};
#define COUNTER_REQUIRE(x) do { if (!(x)) { error = __LINE__; return false; } } while (false)
    void hurt(FighterMotion& actor, unsigned damage = 40, unsigned flags = 0) {
        FighterStrike hit{}; hit.damage = damage; hit.hurtFrames = 40; hit.flags = flags; hit.key = 73;
        actor.receiveStrike({hit, -1, 1100});
    }
    bool start(const FighterAsset& asset, unsigned faction, unsigned hz, int facing, bool cpu, double energy) {
        FighterMotion actor(asset), target(asset);
        actor.reset({900, 500}, facing);target.reset({1100, 500}, -facing);
        actor.setFaction(faction);actor.setGameHz(hz);actor.startTraining();
        hurt(actor);actor.setQi(100);actor.setEnergy(energy);
        FighterControls input{};
        if (cpu) { input.aiControlled = true; input.aiIntents = 1U << unsigned(AiIntent::Counter); }
        else input.assistHeld = true;
        actor.step(stage, input, false, {}, &target);
        if (!actor.counterPaused()) actor.step(stage, input, false, {}, &target);
        COUNTER_REQUIRE(actor.counterPaused() && actor.counterStarts() == 1);
        COUNTER_REQUIRE(actor.counterKind() == (faction == 1 ? CounterKind::Replace : CounterKind::Explode));
        COUNTER_REQUIRE(actor.qi() == 0 && actor.energy() == 0 && actor.overloaded() == (energy < 100));
        const auto history = actor.takeTrainingInput();
        COUNTER_REQUIRE(history && history->assist && history->highlight);
        const auto first = actor.takeEffects();
        COUNTER_REQUIRE(first.reaction && first.reaction->cue == (faction == 1 ? ReactionCue::Replace : ReactionCue::ExplodeCharge));
        COUNTER_REQUIRE(first.counterSounds == (faction == 1 ? 1U : 2U));
        const double expectedX = faction == 1 ? 900 + 250 * facing * (30.0 / hz) : 900;
        COUNTER_REQUIRE(std::abs(actor.motion().position.x - expectedX) < 1e-8);
        if (faction == 1) {
            // The explicit immediate renderAnimate consumes justHurtResume in the original method.
            COUNTER_REQUIRE(actor.aiHittable() && !actor.hurtBody().empty() && actor.currentHurtDamage() == 0 && actor.counterReplaceFrames() == 30);
            COUNTER_REQUIRE(actor.facing() == (expectedX < 1100 ? 1 : -1));
        } else COUNTER_REQUIRE(!actor.aiHittable() && actor.steel() && actor.counterHitFrames() == 10);
        unsigned ticks = 0;
        bool smoke = false, burst = false;
        do {
            ++ticks;
            const bool paused = actor.stepCounterPause();
            const auto effect = actor.takeEffects();
            if (effect.reaction && effect.reaction->cue == ReactionCue::ReplaceSmoke) smoke = true;
            if (effect.reaction && effect.reaction->cue == ReactionCue::ExplodeBurst) {
                burst = true;COUNTER_REQUIRE(effect.counterSounds == 4);
            }
            COUNTER_REQUIRE(actor.energy() == 0 && actor.qi() == 0);
            if (faction != 1) COUNTER_REQUIRE(actor.counterHitFrames() == 10 && actor.counterSteelFrames() == 60);
            COUNTER_REQUIRE(ticks < 100);
            if (!paused) break;
        } while (true);
        COUNTER_REQUIRE(ticks == (faction == 1 ? (hz + 1) * 60 / hz : 42));
        COUNTER_REQUIRE(smoke == (faction == 1) && burst == (faction != 1));
        if (faction == 1) {
            for (unsigned frame = 1; frame <= 30; ++frame) {
                actor.step(stage, {}, true);
                COUNTER_REQUIRE(actor.counterReplaceFrames() == 30 - frame);
            }
        } else {
            actor.setRoundStats(1000, 5);
            actor.step(stage, {}, true);
            COUNTER_REQUIRE(actor.counterHitFrames() == 9 && !actor.aiHittable());
            actor.step(stage, {}, true);
            COUNTER_REQUIRE(actor.counterHitFrames() == 8 && !actor.aiHittable() && actor.currentHurtDamage() == 0);
            actor.step(stage, {}, true);
            COUNTER_REQUIRE(actor.counterHitFrames() == 7 && actor.aiHittable() && !actor.hurtBody().empty());
            actor.place({1200, 500});actor.setFacing(-facing);
            const auto hit = actor.strikeAgainst({900, 500}, {895, 450, 905, 500});
            COUNTER_REQUIRE(hit && hit->damage() == 50 && hit->strike.hitX == 15 * facing && !hit->hasOwner);
            COUNTER_REQUIRE(!hit->strike.bisha() && !hit->strike.breaksGuard() && hit->strike.launches());
            COUNTER_REQUIRE(!actor.strikeAgainst({1200, 500}, {1195, 450, 1205, 500}));
            for (unsigned frame = 4; frame <= 60; ++frame) {
                actor.step(stage, {}, true);
                COUNTER_REQUIRE(actor.counterSteelFrames() == 60 - frame);
                COUNTER_REQUIRE(actor.steel() == (frame < 60));
            }
            COUNTER_REQUIRE(!actor.strikeAgainst({900, 500}, {895, 450, 905, 500}));
        }
        ++cases;++starts;return true;
    }
    bool gates(const FighterAsset& asset) {
        for (unsigned mask = 0; mask < 64; ++mask) {
            FighterMotion actor(asset);actor.reset({900, 500}, 1);
            const bool energy = mask & 1, qi = mask & 2, bisha = mask & 4, breaks = mask & 8, damage = mask & 16, flying = mask & 32;
            hurt(actor, damage ? 211 : 210, (bisha ? 16 : 0) | (breaks ? 2 : 0) | (flying ? 1 : 0));
            actor.setEnergy(energy ? 49.99 : 50);actor.setQi(qi ? 99.99 : 100);
            actor.step(stage, {.assistHeld = true}, false);
            COUNTER_REQUIRE(actor.counterPaused() == (mask == 0));
            COUNTER_REQUIRE(actor.counterStarts() == unsigned(mask == 0));
            ++cases;
        }
        // Only the last hit's bisha flag blocks a counter; a break-def flag anywhere in the chain blocks it.
        for (unsigned mode = 0; mode < 2; ++mode) {
            FighterMotion actor(asset);actor.reset({900, 500}, 1);
            hurt(actor, 40, mode ? 2 : 16);
            for (unsigned frame = 0; frame < 4; ++frame) actor.step(stage, {}, true);
            hurt(actor, 40);
            actor.setQi(100);actor.setEnergy(100);
            actor.step(stage, {.assistHeld = true}, false);
            COUNTER_REQUIRE(actor.counterPaused() == (mode == 0));++cases;
        }
        FighterMotion dead(asset);dead.reset({900,500},1);hurt(dead);dead.setHealth(0);dead.setQi(100);
        dead.step(stage,{.assistHeld=true},false);COUNTER_REQUIRE(!dead.counterPaused());++cases;
        return true;
    }
#undef COUNTER_REQUIRE
};
} // namespace bvn::counter_qa
