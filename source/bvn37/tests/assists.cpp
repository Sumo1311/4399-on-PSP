// SPDX-License-Identifier: GPL-3.0-or-later
#include "roster_asset.hpp"
#include "versus_round.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string_view>

using namespace bvn;

namespace {

const FightAttacker* summoned(const FightMatch& match, unsigned side) {
    for (const auto& object : match.objects()) {
        const auto* attacker = std::get_if<FightAttacker>(&object);
        if (attacker && attacker->assister() && attacker->owner() == side) return attacker;
    }
    return nullptr;
}

struct Trial {
    unsigned cutinTicks = 0, chargeTicks = 0, maxShots = 0;
    std::vector<unsigned> launches;
    double damage = 0, qi = 0;
    bool appeared = false, left = false;
};

Trial play(const StageAsset& stage, const FighterAsset& fighter, const FighterAsset& assist,
           unsigned side, unsigned gap, unsigned faction, bool verifyCharge = false, bool recordVolley = false) {
    const auto floor = stage.geometry();
    std::array<AssisterChoice, 2> assists{};
    assists[side] = {&assist, faction};
    const auto actors = std::span(&fighter, 1);
    FightMatch match(actors, actors, {}, assists);
    match.reset(stage, {350, floor.playerBottom}, {350.0 + gap, floor.playerBottom});
    Trial trace;
    for (unsigned tick = 0; tick < (verifyCharge ? 1300U : 360U); ++tick) {
        std::array<FighterControls, 2> keys{};
        keys[side].assistHeld = tick == 5;
        const bool frozen = match.cinematic();
        const bool hitFrozen = match.hitFreeze();
        const auto before = match.actor(side).motion().position;
        const auto charge = match.actor(side).assistCharge();
        if (frozen) {
            assert(!match.roundClockTick(true));
            keys[side].horizontal = side ? -1 : 1;
        }
        match.step(floor, keys, tick % 2 == 0);
        if (frozen) {
            ++trace.cutinTicks;
            assert(match.actor(side).motion().position.x == before.x);
            assert(match.actor(side).assistCharge() == charge);
        }
        const auto* root = summoned(match, side);
        if (root && !trace.appeared) {
            assert(tick == 5 && match.actor(side).assistCharge() == 0);
            assert(match.assistSounds() == (faction == 1 ? 2U : 1U));
            trace.appeared = true;
        }
        if (!root && trace.appeared) trace.left = true;
        unsigned shots = 0;
        for (const auto& object : match.objects()) {
            if (const auto* shot = std::get_if<FightProjectile>(&object); shot && shot->owner() == side) {
                ++shots;
                if (recordVolley && root && !hitFrozen) {
                    const unsigned frame = root->phase() + 1;
                    const double firstStepX = frame == 78 ? 21.45 : 22.5;
                    const double offset = std::abs(shot->position().x - root->position().x);
                    if (std::abs(offset - firstStepX) < .001) trace.launches.push_back(frame);
                }
            }
        }
        trace.maxShots = std::max(trace.maxShots, shots);
        const auto after = match.actor(side).assistCharge();
        if (after > charge) ++trace.chargeTicks;
        if (verifyCharge && tick > 5) {
            assert(after == std::min(AssistChargeMax, charge + unsigned(!frozen && !hitFrozen && tick % 2 == 0)));
        }
    }
    assert(trace.appeared && trace.left);
    trace.damage = FighterMaxHealth - match.actor(1 - side).health();
    trace.qi = match.actor(side).qi();
    return trace;
}

void checkTraining(const StageAsset& stage, const FighterAsset& fighter, const FighterAsset& gin) {
    const auto fighters = std::span(&fighter, 1);
    VersusRound training(stage, fighters, fighters, 60, {}, BattleMode::Training, {{{&gin, 0}, {}}});
    for (unsigned tick = 0; tick < 7; ++tick) training.step({});
    training.step({{{.assistHeld = true}, {}}});
    assert(summoned(training.match(), 0));
    assert(training.match().actor(0).assistCharge() < 3);
    auto history = training.trainingHistory(0);
    assert(history.size() == 1 && history[0].count == 1 && history[0].commands[0].assist);
    training.step({{{.assistHeld = true}, {}}});
    assert(training.trainingHistory(0).size() == 1 && training.trainingHistory(0)[0].count == 1);
    for (unsigned active = 1, ticks = 0; active < LogicHz * 2; ++ticks) {
        assert(ticks < 3000);
        if (!training.match().cinematic() && !training.match().hitFreeze()) ++active;
        training.step({});
        if (active < LogicHz * 2) assert(training.match().actor(0).assistCharge() < AssistChargeMax);
    }
    assert(training.match().actor(0).assistCharge() == AssistChargeMax);
}

void checkTwoSides(const StageAsset& stage, const FighterAsset& fighter, const FighterAsset& gin,
                   const FighterAsset& tenten) {
    const auto actors = std::span(&fighter, 1);
    FightMatch match(actors, actors, {}, {{{&gin, 0}, {&tenten, 1}}});
    const auto floor = stage.geometry();
    const Point left{350, floor.playerBottom}, right{610, floor.playerBottom};
    match.reset(stage, left, right);
    for (unsigned tick = 0; tick < 7; ++tick) match.step(floor, {}, tick % 2 == 0);
    match.step(floor, {{{.jumpPressed = true}, {}}}, true);
    for (unsigned tick = 0; !match.actor(0).motion().airborne; ++tick) {
        assert(tick < 30);
        match.step(floor, {}, tick % 2 == 0);
    }
    match.step(floor, {{{.assistHeld = true}, {}}}, false);
    assert(!summoned(match, 0) && match.actor(0).assistCharge() == AssistChargeMax);

    match.reset(stage, left, right);
    for (unsigned tick = 0; tick < 7; ++tick) match.step(floor, {}, tick % 2 == 0);
    match.step(floor, {{{.assistHeld = true}, {.assistHeld = true}}}, false);
    const auto* first = summoned(match, 0);
    const auto* second = summoned(match, 1);
    assert(first && second && first->position().x == left.x - 30 && second->position().x == right.x + 30);
    assert(match.actor(0).assistCharge() == 0 && match.actor(1).assistCharge() == 0);
    assert(match.assistSounds() == 3);
    for (unsigned tick = 0; tick < 430; ++tick) match.step(floor, {}, tick % 2 == 0);
    assert(!match.cinematic() && !summoned(match, 0) && !summoned(match, 1));
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path path(argv[1]);
    StageAsset stage;
    FighterAsset fighter, gin, tenten;
    RosterAsset roster;
    assert(stage.open((path / "JIANCHANG.BVN").string().c_str()));
    assert(fighter.open((path / "ICHIGO.BVF").string().c_str()) && !fighter.assister());
    assert(gin.open((path / "AGIN.BVS").string().c_str()) && gin.assister());
    assert(tenten.open((path / "ATENTEN.BVS").string().c_str()) && tenten.assister());
    assert(roster.open((path / "ASSISTS.BVR").string().c_str(), true));
    assert(roster.cells().size() == 70);
    assert(roster.firstCell(0) == 23 && roster.firstCell(1) == 19);
    std::set<unsigned> played;
    for (const auto& cell : roster.cells()) {
        if (cell.fighter == MissingFighter) continue;
        const auto id = cell.fighter;
        const auto& entry = roster.fighter(id);
        if (entry.random) {
            for (unsigned seed = 0; seed < 60; ++seed) {
                const auto resolved = roster.resolve(id, seed);
                assert(resolved && roster.fighter(*resolved).playable());
                assert(entry.random == 1 || roster.fighter(*resolved).faction == entry.random - 2);
            }
        }
        if (!entry.playable() || !played.insert(id).second) continue;
        FighterAsset assist;
        assert(assist.open((path / entry.file).string().c_str()) && assist.assister());
        double damage = 0;
        for (unsigned gap : {55U, 90U, 130U, 180U, 260U, 380U}) {
            damage = std::max(damage, play(stage, fighter, assist, 0, gap, entry.faction).damage);
        }
        std::printf("%s summoned, departed and dealt %.0f HP\n", entry.file, damage);
        std::fflush(stdout);
        const std::string_view name(entry.file);
        const bool support = name == "AKON.BVS" || name == "AORIHIME.BVS" || name == "ATSUNATE.BVS" ||
            name == "AYORUIC.BVS" || name == "ATEMARI.BVS" || name == "AYLS.BVS";
        if (!support) assert(damage > 0);
    }
    assert(played.size() == 42);
    for (unsigned side = 0; side < 2; ++side) {
        const auto strike = play(stage, fighter, gin, side, 260, 0, true);
        assert(strike.cutinTicks && strike.damage > 0 && strike.chargeTicks == AssistChargeMax);
        assert(std::abs(strike.qi - strike.damage * 0.15) < .001);
        const auto volley = play(stage, fighter, tenten, side, 260, 1, false, true);
        assert((volley.launches == std::vector<unsigned>{40, 46, 52, 58, 64, 78}));
        assert(volley.maxShots == 3 && volley.damage == 100);
    }
    checkTraining(stage, fighter, gin);
    checkTwoSides(stage, fighter, gin, tenten);
    std::puts("PASS: assist roster, mirrored summons, flight, damage, cut-ins, normal gauge and training recovery");
}
