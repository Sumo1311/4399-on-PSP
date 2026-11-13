// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_match.hpp"
#include "roster_asset.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace bvn;

namespace {

void checkSelection(const RosterAsset& roster) {
    assert(roster.cells().size() == 70);
    const auto cells = roster.cells();
    assert(cells[0].x == 15 && cells[0].y == 100 && cells[0].fighter == 0);
    assert(cells[9].x == 735 && cells[9].y == 100 && cells[9].fighter == 1);
    assert(cells[3].x == 250 && cells[3].y == 120);
    assert(cells[59].x == 775 && cells[59].fighter == MissingFighter);
    assert(roster.move(0, -1, 0) == 9 && roster.move(0, 0, 1) == 10);
    assert(!roster.resolve(cells[5].fighter, 0));

    const auto rukia = cells[1].fighter;
    const auto shikai = cells[1].choice(1);
    assert(cells[4].fighter == shikai && roster.fighter(shikai).form == 1);
    assert((roster.forms(shikai) == std::vector<unsigned>{rukia, shikai}));
    for (unsigned seed = 0; seed < 80; ++seed) {
        const auto bleach = roster.resolve(cells[63].fighter, seed);
        const auto naruto = roster.resolve(cells[65].fighter, seed);
        assert(bleach && roster.fighter(*bleach).faction == 0 && roster.fighter(*bleach).playable());
        assert(naruto && roster.fighter(*naruto).faction == 1 && roster.fighter(*naruto).playable());
    }
}

const FightAttacker* firstAttacker(const FightMatch& match) {
    for (const auto& object : match.objects()) {
        if (const auto* attacker = std::get_if<FightAttacker>(&object)) return attacker;
    }
    return nullptr;
}

void checkStone(const StageAsset& stage, const FighterAsset& sakura, const FighterAsset& rival) {
    FightMatch match(sakura, rival);
    const auto floor = stage.geometry();
    match.reset(stage, {350, floor.playerBottom}, {700, floor.playerBottom});
    bool sawSpawn = false, sawImpact = false, sawHit = false;
    unsigned waitAtFrameTen = 0;
    for (unsigned tick = 0; tick < 200; ++tick) {
        match.step(floor, {{{.vertical = -1, .attackPressed = tick == 0}, {}}}, tick % 2 == 0, Contacts::Locked);
        const auto* stone = firstAttacker(match);
        if (!stone) continue;
        sawSpawn = true;
        if (stone->phase() == 9 && stone->position().y < floor.playerBottom - 5) ++waitAtFrameTen;
        if (stone->phase() >= 14) {
            sawImpact = true;
            assert(std::abs(stone->position().y - floor.playerBottom) < 0.001);
            assert(stone->velocity().x == 0 && stone->velocity().y == 0);
            const auto p = stone->position();
            const WorldBox dummy{p.x - 12, p.y - 80, p.x + 12, p.y + 10};
            if (stone->strikeAgainst(p, dummy)) {
                assert(stone->phase() >= 16 && stone->phase() <= 18);
                sawHit = true;
            }
        }
    }
    assert(sawSpawn && waitAtFrameTen > 2 && sawImpact && sawHit && !firstAttacker(match));
    std::printf("Sakura stone held source frame 10 for %u ticks, then reached the landing and hit frames\n", waitAtFrameTen);
}

void checkLightning(const StageAsset& stage, const FighterAsset& sasuke, const FighterAsset& rival) {
    FightMatch match(sasuke, rival);
    const auto floor = stage.geometry();
    match.reset(stage, {350, floor.playerBottom}, {700, floor.playerBottom});
    FighterStrike charge{};
    charge.damage = 100;
    for (unsigned i = 0; i < 20; ++i) match.actor(0).scoreStrike({charge, 1, 350});
    bool sawLightning = false;
    for (unsigned tick = 0; tick < 300; ++tick) {
        match.step(floor, {{{.vertical = -1, .superPressed = tick == 0}, {}}}, tick % 2 == 0);
        if (const auto* lightning = firstAttacker(match)) {
            if (!sawLightning) {
                assert(match.actor(0).timelineFrame() == 834);
                assert(std::abs(lightning->position().x - match.actor(1).motion().position.x) < 0.001);
            }
            sawLightning = true;
        }
    }
    assert(sawLightning && match.actor(1).health() < FighterMaxHealth && !firstAttacker(match));
    std::printf("Sasuke lightning spawned at source frame 834 and left %.0f HP\n", match.actor(1).health());
}

void checkFollowingSword(const StageGeometry& floor, const FighterAsset& renji) {
    Point target{550, floor.playerBottom - 35};
    FightAttacker sword(renji, 0, {FighterOp::Summon, 1, {350, floor.playerBottom}, 1, &renji}, target);
    FighterMotion opponent(renji);
    std::vector<ObjectSpawn> shots;
    assert(sword.position().x == target.x && sword.position().y == target.y - 30);
    for (unsigned tick = 0; tick < 18; ++tick) {
        target = {target.x + 2, target.y + 1};
        opponent.reset(target, -1);
        sword.step(floor, tick % 2 == 0, {}, opponent, 1, shots);
        assert(sword.active() && sword.position().x == target.x && sword.position().y == target.y - 30);
    }
    assert(shots.empty());
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path path(argv[1]);
    RosterAsset roster;
    assert(roster.open((path / "ROSTER.BVR").string().c_str()));
    checkSelection(roster);
    StageAsset stage;
    FighterAsset sakura, renji, sasuke, naruto;
    assert(stage.open((path / "JIANCHANG.BVN").string().c_str()));
    assert(sakura.open((path / "SAKURA.BVF").string().c_str()));
    assert(renji.open((path / "RENJI.BVF").string().c_str()));
    assert(sasuke.open((path / "SASUKE.BVF").string().c_str()));
    assert(naruto.open((path / "NARUTO.BVF").string().c_str()));
    checkStone(stage, sakura, naruto);
    checkLightning(stage, sasuke, naruto);
    checkFollowingSword(stage.geometry(), renji);
    std::puts("PASS: selection grid, faction random, grounded impact, lightning and following sword");
}
