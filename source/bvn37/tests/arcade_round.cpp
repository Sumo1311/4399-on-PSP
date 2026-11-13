// SPDX-License-Identifier: GPL-3.0-or-later
#include "arcade_run.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

using namespace bvn;

namespace {

void reach(VersusRound& round, RoundPhase phase) {
    for (unsigned tick = 0; tick < 1600 && round.phase() != phase; ++tick) round.step({});
    assert(round.phase() == phase);
}

FighterAsset load(const std::filesystem::path& path) {
    FighterAsset asset;
    assert(asset.open(path.string().c_str()));
    return asset;
}

void checkArcadeTable(const std::filesystem::path& root) {
    RosterAsset fighters, assists;
    assert(fighters.open((root / "ROSTER.BVR").string().c_str()));
    assert(assists.open((root / "ASSISTS.BVR").string().c_str(), true));
    ArcadeRun arcade;
    assert(arcade.open((root / "ARCADE.BVM").string().c_str(), fighters, assists, 20));
    for (unsigned faction = 0; faction < 2; ++faction) {
        for (bool team : {false, true}) {
            arcade.reset();
            assert(arcade.stage(faction, team).map == (faction ? 1 : 6));
            const auto& opening = arcade.stage(faction, team);
            assert(std::string(assists.fighter(opening.assister).file) == (faction ? "AKON.BVS" : "ASAI.BVS"));
            assert(opening.healthPercent == 100 && opening.enemyAttackPercent == 100);
            for (unsigned stage = 0; stage < ArcadeRun::StageCount; ++stage) {
                assert(arcade.stageIndex() == stage && arcade.lastStage() == (stage == 7));
                const auto& encounter = arcade.stage(faction, team);
                const auto source = std::span(encounter.opponents).first(encounter.count);
                std::set<std::array<unsigned, TeamSize>> orders;
                for (unsigned seed = 0; seed < 6; ++seed) {
                    const auto order = encounter.lineup(seed, team);
                    if (team) assert(std::ranges::is_permutation(source, std::span(order).first(encounter.count)));
                    else {
                        assert(std::ranges::find(source, order[0]) != source.end());
                        assert(order[1] == MissingFighter && order[2] == MissingFighter);
                    }
                    orders.insert(order);
                }
                assert(orders.size() == (team && encounter.count == 3 ? 6 : encounter.count));
                const unsigned before = arcade.score();
                arcade.advance(team);
                assert(arcade.score() == before + (stage == 7 ? 0 : team ? 12000 : 4000));
            }
            const unsigned before = arcade.score();
            arcade.retry();
            assert(arcade.lastStage() && arcade.score() == before - 10000);
            for (unsigned n = 0; n < 9; ++n) arcade.retry();
            assert(arcade.lastStage() && arcade.score() == 0);
        }
    }
    arcade.reset();
    for (unsigned i = 0; i < 4; ++i) arcade.advance(true);
    const auto& boss = arcade.stage(0, true);
    assert(boss.count == 1 && boss.healthPercent == 250 && boss.enemyAttackPercent == 200);
    assert(fighters.fighter(boss.opponents[0]).faction == 1);
    std::puts("PASS: 32 loaded mission rows, permutations, solo picks, progression, retry and boss rates");
}

void checkHandicap(const StageAsset& stage, const FighterAsset& fighter) {
    const auto forms = std::span(&fighter, 1);
    VersusRound boss(stage, forms, forms, 1, {}, BattleMode::TeamArcade, {}, {}, {{3, 1}, 2500, 2});
    boss.step({}, true);
    reach(boss, RoundPhase::Playing);
    const_cast<FighterMotion&>(boss.match().actor(1)).setHealth(1800);
    assert(boss.match().actor(0).health() == 2500 && boss.match().actor(1).maxHealth() == 2500);
    reach(boss, RoundPhase::Victory);
    boss.step({}, true);
    assert(boss.phase() == RoundPhase::Finished && boss.winner() == 0 && boss.wins(0) == 1);
    assert(!boss.entering(1));

    FightMatch match(fighter, fighter);
    const auto floor = stage.geometry();
    const auto damage = [&](unsigned side, double rate) {
        for (unsigned i = 0; i < 2; ++i) match.actor(i).setRoundStats(2500, i == side ? rate : 1);
        match.reset(stage, {350, floor.playerBottom}, {405, floor.playerBottom});
        for (unsigned tick = 0; tick < 90; ++tick) {
            std::array<FighterControls, 2> keys{};
            keys[side].attackPressed = tick == 4;
            match.step(floor, keys, tick % 2 == 0, Contacts::Round);
            const double lost = 2500 - match.actor(1 - side).health();
            if (!lost) continue;
            if (!side) assert(match.pointsThisStep() == FightMatch::classicScore(unsigned(lost)));
            return lost;
        }
        return 0.0;
    };
    const double ordinary = damage(1, 1);
    assert(ordinary > 0 && damage(1, 2) == ordinary * 2);
    assert(damage(0, 1) == ordinary);
    std::puts("PASS: 2500 HP boss rules, first-opponent damage multiplier, score and single-member knockout bracket");
}

void checkSelfHeal(const StageAsset& stage, const FighterAsset& orihime, const FighterAsset& target) {
    const auto floor = stage.geometry();
    FightMatch match(orihime, target);
    match.actor(0).setRoundStats(2500, 1);
    match.reset(stage, {350, floor.playerBottom}, {610, floor.playerBottom});
    match.actor(0).setQi(300);
    match.actor(0).setHealth(500);
    unsigned pulses = 0;
    for (unsigned tick = 0; tick < 310; ++tick) {
        const double before = match.actor(0).health();
        std::array<FighterControls, 2> keys{{{.vertical = 1, .superPressed = tick == 6}, {}}};
        match.step(floor, keys, tick % 2 == 0, Contacts::Round);
        if (match.actor(0).health() != before) {
            assert(match.actor(0).health() - before == 12.5);
            ++pulses;
        }
    }
    assert(pulses == 16 && match.actor(0).health() == 700);
    std::puts("PASS: Orihime heals sixteen times for 0.5% of stage-scaled HP");
}

void checkKakashi(const StageAsset& stage, const FighterAsset& kakashi, const FighterAsset& target) {
    const auto floor = stage.geometry();
    unsigned caught = 0, missed = 0;
    for (bool guard : {false, true}) {
        FightMatch match(kakashi, target);
        match.actor(1).setRoundStats(3500, 1);
        match.reset(stage, {200, floor.playerBottom}, {420, floor.playerBottom});
        match.actor(0).setQi(300);
        bool seen = false, returned = false, appeared = false;
        for (unsigned tick = 0; tick < 360; ++tick) {
            std::array<FighterControls, 2> keys{{{.vertical = -1, .superPressed = tick == 6}, {.vertical = guard ? -1 : 0}}};
            match.step(floor, keys, tick % 2 == 0, Contacts::Round);
            appeared |= !match.objects().empty();
            if (match.hidesTarget(0)) seen = true;
            else if (seen) returned = true;
        }
        assert(appeared && !match.hidesTarget(0));
        if (seen) {
            assert(returned);
            ++caught;
        } else ++missed;
    }
    assert(caught && missed);
    std::puts("PASS: Kamui captures and releases on hit, removes itself on guard");
}

void checkNamedAttackers(const StageAsset& stage, const FighterAsset& uryuu, const FighterAsset& target) {
    const auto floor = stage.geometry();
    FightMatch match(uryuu, target);
    match.reset(stage, {220, floor.playerBottom}, {645, floor.playerBottom});
    for (unsigned tick = 0; tick < 350; ++tick) {
        // Keep the target clear of both automatic hit probes so only the action script can jump the arrows.
        for (double safe : {430., 605., 800.}) {
            const bool clear = std::ranges::none_of(match.objects(), [&](const auto& object) {
                const auto* arrow = std::get_if<FightAttacker>(&object);
                return arrow && std::abs(arrow->position().x - safe) < 115;
            });
            if (clear) {
                match.actor(1).reset({safe, floor.playerBottom}, -1, true);
                break;
            }
        }
        const bool cast = tick >= 5 && (tick - 5) % 4 == 0;
        match.step(floor, {{{.vertical = -1, .specialPressed = cast}, {.vertical = -1}}},
                   tick % 2 == 0, Contacts::Round);
        if (match.actor(0).timelineFrame() != 1141) continue;
        assert(match.objects().size() == 2);
        std::array<unsigned, 2> phases{
            std::get<FightAttacker>(match.objects()[0]).phase(),
            std::get<FightAttacker>(match.objects()[1]).phase()};
        std::ranges::sort(phases);
        assert((phases == std::array{201U, 202U}));
        assert(match.actor(1).health() == FighterMaxHealth);
        std::puts("PASS: Uryuu frame 1141 jumps both named arrows to their separate hit labels");
        return;
    }
    assert(false && "Uryuu never reached the arrow-control frame");
}

void checkSupport(const std::filesystem::path& root, const StageAsset& stage, const FighterAsset& fighter) {
    const auto floor = stage.geometry();
    const auto actors = std::span(&fighter, 1);
    const auto sai = load(root / "ASAI.BVS");
    const auto tsunade = load(root / "ATSUNATE.BVS");
    for (const auto* assist : {&sai, &tsunade}) {
        FightMatch match(actors, actors, {}, {{{assist, 1}, {}}});
        match.reset(stage, {350, floor.playerBottom}, {590, floor.playerBottom});
        match.actor(0).setHealth(500);
        unsigned spedUp = 0, pulses = 0;
        for (unsigned tick = 0; tick < 620; ++tick) {
            const double before = match.actor(0).health();
            const bool hitFrozen = match.hitFreeze();
            match.step(floor, {{{.assistHeld = tick == 6}, {}}}, tick % 2 == 0, Contacts::Round);
            if (match.actor(0).health() > before) {
                assert(match.actor(0).health() - before == 7.5);
                ++pulses;
            }
            const auto speed = match.actor(0).speed();
            if (speed > fighter.stats().speed) {
                assert(speed == fighter.stats().speed + 2.5);
                if (!hitFrozen) ++spedUp;
            }
        }
        if (assist == &sai) assert(spedUp == 360 && pulses == 0);
        else assert(!spedUp && pulses == 8 && match.actor(0).health() == 560);
    }
    std::puts("PASS: Sai's six-second movement buff and Tsunade's eight healing pulses");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path root(argv[1]);
    StageAsset stage;
    assert(stage.open((root / "JIANCHANG.BVN").string().c_str()));
    const auto ichigo = load(root / "ICHIGO.BVF");
    checkArcadeTable(root);
    checkHandicap(stage, ichigo);
    checkSelfHeal(stage, load(root / "ORIHIME.BVF"), ichigo);
    checkKakashi(stage, load(root / "KAKASHI.BVF"), ichigo);
    checkNamedAttackers(stage, load(root / "URYUU.BVF"), ichigo);
    checkSupport(root, stage, ichigo);
}
