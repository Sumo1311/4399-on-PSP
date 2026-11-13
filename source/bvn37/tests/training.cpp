// SPDX-License-Identifier: GPL-3.0-or-later
#include "versus_round.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace bvn;

namespace {

void checkRecovery(const StageGeometry& stage, const FighterAsset& asset) {
    FighterMotion fighter(asset);
    fighter.reset({350, stage.playerBottom}, 1);
    fighter.startTraining();
    FighterStrike hit{};
    hit.damage = FighterMaxHealth;
    hit.hurtFrames = 6;
    fighter.receiveStrike({hit, -1, 420});
    assert(fighter.health() == 0 && !fighter.knockedDown());
    for (unsigned tick = 0; !fighter.standing(); ++tick) {
        assert(tick < 60);
        fighter.step(stage, {}, tick % 2 == 0);
        fighter.updateTraining();
    }
    // FighterMcHurtCtrl.onIdleEnter keeps the actor invulnerable until the next animation prelude.
    assert(fighter.health() == 0 && fighter.hurtBody().empty());
    for (unsigned tick = 0; tick < LogicHz - 2; ++tick) {
        fighter.step(stage, {.horizontal = -1}, tick % 2 == 0);
        fighter.updateTraining();
        assert(fighter.health() == 0);
        if (tick == 0) assert(!fighter.hurtBody().empty());
    }
    assert(fighter.motion().position.x < 345);
    fighter.updateTraining();
    assert(fighter.health() == FighterMaxHealth);
}

void checkPractice(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second) {
    VersusRound round(stage, std::span(&first, 1), std::span(&second, 1), 60, {}, BattleMode::Training);
    assert(round.training() && round.phase() == RoundPhase::Playing && round.seconds() == -1 && !round.bannerFrame());
    const auto& player = round.match().actor(0);
    assert(player.qi() == FighterMaxQi && round.match().actor(1).qi() == FighterMaxQi);
    for (unsigned i = 0; i < 40; ++i) round.step({});

    for (unsigned i = 0; i < 12; ++i) round.step({{{.horizontal = 1}, {}}});
    auto history = round.trainingHistory(0);
    assert(history.size() == 1 && history.front().count == 1);
    assert(history.front().commands[0].command == FighterCommand::Count && history.front().commands[0].horizontal == 1);
    for (unsigned i = 0; i < LogicHz; ++i) round.step({});
    assert(round.trainingHistory(0).empty() && round.trainingHistory(1).empty());

    round.step({{{.superPressed = true}, {}}});
    assert(player.qi() == FighterMaxQi - 100);
    history = round.trainingHistory(0);
    assert(history.size() == 1 && history.back().commands[0].command == FighterCommand::Super);
    for (unsigned active = 1, ticks = 0; active < LogicHz * 2; ++ticks) {
        assert(ticks < 3000);
        if (!round.match().cinematic() && !round.match().hitFreeze()) ++active;
        round.step({});
    }
    assert(player.qi() < FighterMaxQi);
    round.step({});
    assert(player.qi() == FighterMaxQi);
    for (unsigned i = 0; i < 250; ++i) round.step({});
    assert(round.phase() == RoundPhase::Playing && round.seconds() == -1 && round.wins(0) == 0 && round.wins(1) == 0);

    bool combo = false;
    for (unsigned i = 0; i < 100; ++i) {
        round.step({{{.attackPressed = i % 4 == 0, .attackHeld = true}, {}}});
        const auto actions = round.trainingHistory(0);
        if (!actions.empty() && actions.back().count > 1) {
            combo = true;
            break;
        }
    }
    assert(combo);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path directory(argv[1]);
    StageAsset stage;
    FighterAsset ichigo, naruto;
    assert(stage.open((directory / "JIANCHANG.BVN").string().c_str()));
    assert(ichigo.open((directory / "ICHIGO.BVF").string().c_str()));
    assert(naruto.open((directory / "NARUTO.BVF").string().c_str()));
    checkRecovery(stage.geometry(), ichigo);
    checkPractice(stage, ichigo, naruto);
    std::puts("PASS: training entry, no knockout at zero HP, delayed HP and Qi recovery, and accepted input history");
}
