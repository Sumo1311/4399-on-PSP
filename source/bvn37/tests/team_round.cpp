// SPDX-License-Identifier: GPL-3.0-or-later
#include "roster_asset.hpp"
#include "versus_round.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace bvn;

namespace {

void reach(VersusRound& game, RoundPhase phase, unsigned limit = 1600) {
    for (unsigned i = 0; i < limit && game.phase() != phase; ++i) game.step({});
    assert(game.phase() == phase);
}

void ready(VersusRound& game) {
    game.step({}, true);
    reach(game, RoundPhase::Playing);
}

FighterMotion& actor(VersusRound& game, unsigned side) {
    return const_cast<FighterMotion&>(game.match().actor(side));
}

TeamEntrant entry(const FighterAsset& fighter) {
    return {std::span(&fighter, 1), 0};
}

void replace(VersusRound& game, const FighterAsset* first, const FighterAsset* second) {
    assert(game.phase() == RoundPhase::TeamChange);
    assert(game.entering(0).has_value() == bool(first));
    assert(game.entering(1).has_value() == bool(second));
    assert(!game.enterTeamMembers({}));
    const std::array<TeamEntrant, 2> incoming{{first ? entry(*first) : TeamEntrant{},
                                                second ? entry(*second) : TeamEntrant{}}};
    assert(game.enterTeamMembers(incoming) && game.phase() == RoundPhase::IntroFocus);
    if (first) assert(&game.match().actor(0).asset() == first);
    if (second) assert(&game.match().actor(1).asset() == second);
}

void checkTimeoutCarry(const StageAsset& stage, const FighterAsset& ichigo, const FighterAsset& naruto,
                       const FighterAsset& lee, const FighterAsset& rukia, unsigned survivor) {
    const unsigned loser = 1 - survivor;
    TeamFactions factions{};
    factions[survivor] = {0, 0, 0};
    factions[loser] = {1, 1, 0};
    const auto left = std::span(survivor ? &naruto : &ichigo, 1);
    const auto right = std::span(survivor ? &ichigo : &naruto, 1);
    VersusRound game(stage, left, right, 1, {}, BattleMode::TeamVersus, {}, factions);
    ready(game);
    const FighterAsset* reserve[]{&lee, &rukia};
    for (unsigned round = 1; round <= 3; ++round) {
        actor(game, survivor).setHealth(600);
        actor(game, survivor).setQi(90);
        actor(game, loser).setHealth(540);
        actor(game, loser).setQi(round == 1 ? 241.75 : 175.5);
        reach(game, RoundPhase::TimeOver);
        assert(game.seconds() == 0 && game.winner() == int(survivor));
        assert(game.wins(survivor) == round && game.wins(loser) == 0 && !game.perfect());
        reach(game, RoundPhase::Victory);
        assert(actor(game, survivor).health() == 650);
        game.step({}, true);
        if (round == 3) {
            assert(game.phase() == RoundPhase::Finished && game.winner() == int(survivor));
            break;
        }
        const auto* next = reserve[round - 1];
        replace(game, loser == 0 ? next : nullptr, loser == 1 ? next : nullptr);
        assert(game.member(loser) == round && game.member(survivor) == 0);
        assert(game.round() == round + 1 && game.seconds() == 1);
        assert(actor(game, survivor).health() == 650 && actor(game, survivor).qi() == 90);
        assert(actor(game, loser).health() == FighterMaxHealth);
        assert(actor(game, loser).qi() == (round == 1 ? FighterMaxQi : 0));
        reach(game, RoundPhase::IntroPose);
        assert(actor(game, loser).performing(FighterCommand::Intro));
        assert(!actor(game, survivor).performing(FighterCommand::Intro));
        ready(game);
    }
}

void checkKnockoutHeal(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second) {
    const auto left = std::span(&first, 1), right = std::span(&second, 1);
    VersusRound game(stage, left, right, 60, {}, BattleMode::TeamVersus);
    ready(game);
    for (unsigned tick = 0; game.seconds() > 37; ++tick) {
        assert(tick < 1500);
        game.step({});
    }
    actor(game, 0).setHealth(410);
    FighterStrike strike{};
    strike.damage = FighterMaxHealth;
    strike.hurtFrames = 12;
    actor(game, 1).receiveStrike({strike, 1, actor(game, 0).motion().position.x});
    game.step({});
    assert(game.phase() == RoundPhase::KnockoutFlash && game.seconds() == 37);
    reach(game, RoundPhase::Victory);
    assert(actor(game, 0).health() == 533);
    game.step({}, true);
    replace(game, nullptr, &first);
    assert(actor(game, 0).health() == 533);
}

void checkDraws(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second) {
    const auto left = std::span(&first, 1), right = std::span(&second, 1);
    {
        VersusRound game(stage, left, right, 1, {}, BattleMode::TeamVersus);
        ready(game);
        for (unsigned round = 1; round <= 2; ++round) {
            reach(game, RoundPhase::TeamChange);
            replace(game, &second, &first);
            assert(game.member(0) == round && game.member(1) == round);
            assert(game.wins(0) == 0 && game.wins(1) == 0);
            ready(game);
        }
        reach(game, RoundPhase::IntroFocus);
        assert(game.round() == 4 && game.member(0) == 2 && game.member(1) == 2);
        assert(game.winner() == -1 && game.seconds() == 1);
    }
    {
        VersusRound game(stage, left, right, 1, {}, BattleMode::TeamVersus);
        ready(game);
        reach(game, RoundPhase::TeamChange);
        replace(game, &second, &first);
        ready(game);
        actor(game, 0).setHealth(950);
        reach(game, RoundPhase::Victory);
        game.step({}, true);
        replace(game, &first, nullptr);
        ready(game);
        reach(game, RoundPhase::Finished);
        assert(game.winner() == 1 && game.member(0) == 2 && game.member(1) == 1);
        assert(game.wins(0) == 0 && game.wins(1) == 1);
    }
}

void checkRandom(const RosterAsset& roster) {
    const std::array<unsigned, 2> alreadyChosen{0, 1};
    assert(!roster.resolve(0, 0, alreadyChosen));
    for (const auto& cell : roster.cells()) {
        if (cell.fighter == MissingFighter || !roster.fighter(cell.fighter).random) continue;
        for (unsigned seed = 0; seed < 60; ++seed) {
            const auto id = roster.resolve(cell.fighter, seed, alreadyChosen);
            assert(id && *id != 0 && *id != 1);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path path(argv[1]);
    StageAsset stage;
    FighterAsset ichigo, naruto, lee, rukia;
    RosterAsset roster;
    assert(stage.open((path / "JIANCHANG.BVN").string().c_str()));
    assert(ichigo.open((path / "ICHIGO.BVF").string().c_str()));
    assert(naruto.open((path / "NARUTO.BVF").string().c_str()));
    assert(lee.open((path / "LEE.BVF").string().c_str()));
    assert(rukia.open((path / "RUKIA.BVF").string().c_str()));
    assert(roster.open((path / "ROSTER.BVR").string().c_str()));
    checkTimeoutCarry(stage, ichigo, naruto, lee, rukia, 0);
    checkTimeoutCarry(stage, ichigo, naruto, lee, rukia, 1);
    checkKnockoutHeal(stage, ichigo, naruto);
    checkDraws(stage, ichigo, naruto);
    checkRandom(roster);
    std::puts("PASS: three-member elimination on either side, healing, faction Qi, entrant intro and three draw routes");
}
