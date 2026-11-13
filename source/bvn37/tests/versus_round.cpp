#include "fighter_cpu.hpp"
#include "versus_round.hpp"

#include <cassert>
#include <cstdio>

using namespace bvn;

namespace {

void reach(VersusRound& game, RoundPhase next, unsigned limit = 2000) {
    for (unsigned i = 0; i < limit && game.phase() != next; ++i) game.step({});
    assert(game.phase() == next);
}

void testDraw(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second) {
    VersusRound game(stage, first, second, 1);
    game.step({}, true);
    reach(game, RoundPhase::Playing);
    for (unsigned round = 1; round <= 4; ++round) {
        const unsigned open = game.ticks();
        reach(game, RoundPhase::TimeOver);
        assert(game.ticks() / 2 - open / 2 == 31);
        assert(game.seconds() == 0 && game.winner() == -1);
        assert(game.wins(0) == 0 && game.wins(1) == 0);
        reach(game, RoundPhase::Draw);
        assert(game.bannerFrame() == 266);
        reach(game, RoundPhase::Ready);
        assert(game.round() == round + 1 && game.seconds() == 1);
        if (round < 4) reach(game, RoundPhase::Playing);
    }
    assert(game.bannerFrame() == 94);
    for (unsigned i = 0; i < 90; ++i) game.step({});
    assert(game.bannerFrame() == 139);
    game.step({});
    game.step({});
    assert(game.bannerFrame() == 54);
    reach(game, RoundPhase::Playing);
}

void testTimeWinner(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second) {
    VersusRound game(stage, first, second, 2);
    game.step({}, true);
    reach(game, RoundPhase::Playing);
    std::array<FighterControls, 2> input{};
    for (unsigned i = 0; i < 61; ++i) {
        const auto& match = game.match();
        input[0].horizontal = match.actor(1).motion().position.x - match.actor(0).motion().position.x > 35;
        game.step(input);
    }
    input[0].horizontal = 0;
    input[0].attackPressed = true;
    game.step(input);
    reach(game, RoundPhase::TimeOver);
    assert(game.match().actor(0).health() == FighterMaxHealth);
    assert(game.match().actor(1).health() > 0 && game.match().actor(1).health() < FighterMaxHealth);
    assert(game.winner() == 0 && game.wins(0) == 1 && game.wins(1) == 0);
    reach(game, RoundPhase::Victory);
    assert(game.match().actor(0).performing(FighterCommand::Victory));
    const auto qi = game.match().actor(0).qi();
    game.step({}, true);
    assert(game.phase() == RoundPhase::Ready && game.round() == 2);
    assert(game.match().actor(0).qi() == qi && game.match().actor(1).health() == FighterMaxHealth);
}

void testKnockouts(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second, unsigned winner) {
    VersusRound game(stage, first, second);
    FighterCpu driver(460223);
    reach(game, RoundPhase::Playing);
    unsigned clockFrames = 0;
    unsigned timerRemainder = 0;
    unsigned knockouts = 0;
    bool measuredNextClock = false;
    for (unsigned i = 0; i < 15000 && game.phase() != RoundPhase::Finished; ++i) {
        const auto before = game.phase();
        // Supers and assists now freeze the clock; count the same eligible animation frames as Flash.
        if (before == RoundPhase::Playing && game.match().roundClockTick((game.ticks() + 1) % (LogicHz / AnimationHz) == 0)) {
            ++clockFrames;
        }
        std::array<FighterControls, 2> input{};
        if (before == RoundPhase::Playing) input[winner] = driver.step(game.match(), winner);
        game.step(input, before == RoundPhase::Victory && game.phaseTicks() > 60);
        if (before == RoundPhase::Playing && game.phase() == RoundPhase::KnockoutFlash) {
            ++knockouts;
            timerRemainder = clockFrames % 31;
            assert(game.winner() == int(winner) && game.match().actor(1 - winner).health() == 0);
        }
        if (before == RoundPhase::Ready && game.phase() == RoundPhase::Playing) clockFrames = 0;
        if (game.round() == 2 && game.phase() == RoundPhase::Playing && game.seconds() == 59 && !measuredNextClock) {
            assert(clockFrames == 31 - timerRemainder);
            measuredNextClock = true;
        }
    }
    assert(game.phase() == RoundPhase::Finished && game.winner() == int(winner));
    assert(game.wins(1 - winner) == 0 && game.wins(winner) == 2 && knockouts == 2 && measuredNextClock);
    std::printf("Side %u completed two knockouts in %u logic ticks\n", winner + 1, game.ticks());
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    StageAsset stage;
    FighterAsset first, second;
    assert(stage.open(argv[1]) && first.open(argv[2]) && second.open(argv[3]));
    assert(!first.aiRange(FighterCommand::Attack).empty() && !second.aiRange(FighterCommand::Attack).empty());
    testDraw(stage, first, second);
    testTimeWinner(stage, first, second);
    testKnockouts(stage, first, second, 0);
    testKnockouts(stage, first, second, 1);
    std::puts("PASS: time-over, four draws, final-round transition, gauges, and two wins");
}
