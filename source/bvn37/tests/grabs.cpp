// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_match.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace bvn;

namespace {

struct Trial {
    Trial(const StageAsset& arena, const FighterAsset& attacker, const FighterAsset& rival, unsigned side, double gap)
        : stage(arena.geometry()), match(side ? rival : attacker, side ? attacker : rival),
          player(match.actor(side)), target(match.actor(1 - side)), side(side) {
        match.reset(arena, {350, stage.playerBottom}, {350 + gap, stage.playerBottom});
        FighterStrike charge{};
        charge.damage = 100;
        for (int hit = 0; hit < 20; ++hit) player.scoreStrike({charge, 1, 350});
    }

    void step(FighterControls input = {}) {
        std::array<FighterControls, 2> inputs{};
        inputs[side] = input;
        match.step(stage, inputs, tick++ % 2 == 0);
    }

    StageGeometry stage;
    FightMatch match;
    FighterMotion& player;
    FighterMotion& target;
    unsigned side, tick = 0;
};

bool near(double x, double y) {
    return std::abs(x - y) < 0.001;
}

void checkLee(const StageAsset& arena, const FighterAsset& lee, const FighterAsset& rival, unsigned side) {
    Trial trial(arena, lee, rival, side, 64);
    bool grabbed = false, followed = false, landed = false, shown = false, charged = false;
    for (unsigned tick = 0; tick < 330; ++tick) {
        const auto before = trial.player.timelineFrame();
        trial.step({.superPressed = tick == 3});
        switch (trial.player.timelineFrame()) {
        case 633:
            assert(trial.player.movingTarget() && trial.player.hidesTarget());
            assert(trial.target.health() == 920);
            grabbed = true;
            if (before == 633) {
                const auto a = trial.player.motion().position;
                const auto b = trial.target.motion().position;
                // The authored move_mc origin is (0.6, 0) on frame 633.
                assert(near(b.x - a.x, 0.6 * trial.player.facing()));
                followed = true;
            }
            break;
        case 662:
            assert(!trial.player.movingTarget() && trial.player.hidesTarget() && !trial.match.objects().empty());
            landed = true;
            break;
        case 663:
            assert(!trial.player.movingTarget() && !trial.player.hidesTarget());
            shown = true;
            break;
        case 664:
            assert(trial.player.health() == 900);
            charged = true;
            break;
        }
    }
    assert(grabbed && followed && landed && shown && charged);
    assert(trial.target.health() == 620 && !trial.player.hidesTarget());
}

void checkMiss(const StageAsset& arena, const FighterAsset& lee, const FighterAsset& rival) {
    Trial trial(arena, lee, rival, 0, 700);
    bool cast = false;
    for (unsigned tick = 0; tick < 280; ++tick) {
        trial.step({.superPressed = tick == 3});
        cast |= trial.player.casting();
        assert(!trial.player.movingTarget() && !trial.player.hidesTarget());
    }
    assert(cast && trial.target.health() == 1000 && trial.player.health() == 1000);
}

void checkInterrupted(const StageAsset& arena, const FighterAsset& lee, const FighterAsset& rival) {
    Trial trial(arena, lee, rival, 0, 64);
    for (unsigned tick = 0; tick < 220 && !trial.player.movingTarget(); ++tick) trial.step({.superPressed = tick == 3});
    assert(trial.player.movingTarget() && trial.player.hidesTarget());
    FighterStrike interrupt{};
    interrupt.damage = 4000;
    trial.player.receiveStrike({interrupt, -1, trial.target.motion().position.x}, &trial.target);
    assert(trial.player.health() == 0 && !trial.player.movingTarget() && !trial.player.hidesTarget());
}

void checkPull(const StageAsset& arena, const FighterAsset& actor, const FighterAsset& rival, unsigned side, bool gin) {
    Trial trial(arena, actor, rival, side, 96);
    const unsigned firstAnchor = gin ? 416 : 257;
    const unsigned lastAnchor = gin ? 420 : 265;
    bool moving = false, missing = !gin;
    unsigned steps = 0;
    for (unsigned tick = 0; tick < 160; ++tick) {
        const auto frame = trial.player.timelineFrame();
        const auto x = trial.target.motion().position.x;
        const bool pulling = trial.player.movingTarget();
        trial.step(tick == 3 ? FighterControls{.vertical = gin ? 0 : -1, .attackPressed = !gin, .specialPressed = gin}
                             : FighterControls{});
        if (pulling && frame == firstAnchor - 1 && near(trial.target.motion().position.x, x)) missing = true;
        if (pulling && frame >= firstAnchor && frame <= lastAnchor &&
            near((trial.target.motion().position.x - x) * (side ? 1 : -1), 14)) ++steps;
        moving |= trial.player.movingTarget();
        assert(!trial.player.hidesTarget());
    }
    // Both scripts specify x speed 28 and a null y speed. The default game speed is 30/60.
    assert(moving && missing && steps >= 3 && !trial.player.movingTarget());
    assert(trial.target.health() == (gin ? 940 : 920));
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path package = argv[1];
    StageAsset arena;
    FighterAsset lee, gin, sasuke, rival;
    assert(arena.open((package / "JIANCHANG.BVN").c_str()));
    assert(lee.open((package / "LEE.BVF").c_str()) && gin.open((package / "GIN.BVF").c_str()) &&
           sasuke.open((package / "SASUKEM.BVF").c_str()) && rival.open((package / "ICHIGO.BVF").c_str()));
    for (unsigned side = 0; side < 2; ++side) {
        checkLee(arena, lee, rival, side);
        checkPull(arena, gin, rival, side, true);
        checkPull(arena, sasuke, rival, side, false);
    }
    checkMiss(arena, lee, rival);
    checkInterrupted(arena, lee, rival);
    std::puts("PASS: Lee follow, hide, release, health and impact; both directions; miss and interruption; Gin and Sasuke pull");
}
