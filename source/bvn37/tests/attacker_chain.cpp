// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_match.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace bvn;

namespace {

constexpr unsigned ActionTick = 3;

const FightAttacker* firstAttacker(const FightMatch& match) {
    for (const auto& object : match.objects()) {
        if (const auto* attacker = std::get_if<FightAttacker>(&object)) return attacker;
    }
    return nullptr;
}

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.001;
}

template<class Observe>
double play(const StageAsset& stage, const FighterAsset& lead, const FighterAsset& rival,
            unsigned side, int gap, FighterControls action, bool targetJumps, Observe observe) {
    FightMatch match(side ? rival : lead, side ? lead : rival);
    const auto floor = stage.geometry();
    match.reset(stage, {350, floor.playerBottom}, {350.0 + gap, floor.playerBottom});
    auto& actor = match.actor(side);
    auto& target = match.actor(1 - side);
    FighterStrike charge{};
    charge.damage = 100;
    for (unsigned i = 0; i < 20; ++i) actor.scoreStrike({charge, 1, 350});
    for (unsigned tick = 0, active = 0; active < 320; ++tick) {
        assert(tick < 6400);
        std::array<FighterControls, 2> controls{};
        const bool frozen = match.hitFreeze();
        if (active == ActionTick && !frozen) {
            controls[side] = action;
            controls[1 - side].jumpPressed = targetJumps;
        }
        const double previous = target.health();
        match.step(floor, controls, tick % 2 == 0);
        observe(match, actor, target, previous - target.health());
        if (!frozen) ++active;
    }
    assert(match.objects().empty());
    return target.health();
}

void checkVolley(const StageAsset& stage, const FighterAsset& pain, const FighterAsset& rival, unsigned side) {
    constexpr unsigned Frames[]{13, 17, 21};
    constexpr Point Offsets[]{{56, -31.1}, {55, -41.1}, {56, -26.1}};
    constexpr double Damage[]{30, 30, 40};
    unsigned launched = 0, hit = 0;
    const double health = play(stage, pain, rival, side, 220, {.specialPressed = true}, false,
        [&](const FightMatch& match, const FighterMotion&, const FighterMotion&, double damage) {
            const auto* clone = firstAttacker(match);
            if (clone && match.objects().size() > launched + 1) {
                assert(launched < std::size(Frames));
                const auto& bullet = std::get<FightProjectile>(match.objects().back());
                const int facing = clone->facing();
                assert(clone->phase() + 1 == Frames[launched]);
                assert(bullet.owner() == side && bullet.facing() == facing);
                assert(near(bullet.velocity().x, facing * 18));
                assert(near(bullet.position().x, clone->position().x + facing * (Offsets[launched].x + 9)));
                assert(near(bullet.position().y, clone->position().y + Offsets[launched].y));
                ++launched;
            }
            if (damage) {
                assert(hit < std::size(Damage) && damage == Damage[hit]);
                ++hit;
            }
        });
    assert(launched == 3 && hit == 3 && health == 900);
}

void checkProbe(const StageAsset& stage, const FighterAsset& pain, const FighterAsset& rival,
                unsigned side, int gap, bool air, bool expectedHit) {
    bool reachedHitClip = false;
    const double health = play(stage, pain, rival, side, gap,
        {.vertical = air ? 1 : -1, .specialPressed = true}, air,
        [&](const FightMatch& match, const FighterMotion&, const FighterMotion&, double) {
            const auto* attacker = firstAttacker(match);
            if (attacker && attacker->phase() + 1 >= (air ? 43U : 39U)) reachedHitClip = true;
        });
    assert(reachedHitClip == expectedHit);
    assert(health == (expectedHit ? (air ? 910 : 900) : FighterMaxHealth));
}

void checkIce(const StageAsset& stage, const FighterAsset& ice, const FighterAsset& rival,
              unsigned side, int gap) {
    bool spawned = false;
    const double health = play(stage, ice, rival, side, gap, {.vertical = -1, .superPressed = true}, false,
        [&](const FightMatch& match, const FighterMotion& actor, const FighterMotion& target, double) {
            const auto* attacker = firstAttacker(match);
            if (!attacker || spawned) return;
            const auto point = attacker->position();
            const auto rival = target.motion().position;
            const int facing = actor.facing();
            const double expectedX = gap > 400 ? actor.motion().position.x + facing * 400 : rival.x + facing * 10;
            assert(actor.timelineFrame() == 704);
            assert(near(point.x, expectedX) && near(point.y, rival.y - 30));
            spawned = true;
        });
    assert(spawned && health == (gap > 400 ? FighterMaxHealth : 540));
}

void checkLanding(const StageAsset& stage, const FighterAsset& sakura, const FighterAsset& rival, unsigned side) {
    bool spawned = false, looped = false, landed = false;
    unsigned previous = 0;
    const double floor = stage.geometry().playerBottom;
    const double health = play(stage, sakura, rival, side, 220, {.vertical = 1, .specialPressed = true}, true,
        [&](const FightMatch& match, const FighterMotion& actor, const FighterMotion& target, double damage) {
            const auto* summon = firstAttacker(match);
            if (!summon) return;
            const auto point = summon->position();
            if (!spawned) {
                assert(actor.timelineFrame() == 356);
                assert(near(point.x, target.motion().position.x));
                // The target moves on its own tick, so the first snapshot is within one jump step.
                assert(std::abs(point.y - target.motion().position.y + 150) < 7);
                spawned = true;
            }
            const unsigned phase = summon->phase();
            if (previous == 9 && phase == 6) looped = true;
            if (previous == 6 && phase >= 11 && point.y >= floor) landed = true;
            if (damage) assert(landed && damage == 130);
            previous = phase;
        });
    assert(spawned && looped && landed && health == 870);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path path(argv[1]);
    StageAsset stage;
    FighterAsset pain, ice, sakura, rival;
    assert(stage.open((path / "JIANCHANG.BVN").string().c_str()));
    assert(pain.open((path / "PAIN.BVF").string().c_str()));
    assert(ice.open((path / "TOSHIRO.BVF").string().c_str()));
    assert(sakura.open((path / "SAKURASR.BVF").string().c_str()));
    assert(rival.open((path / "ICHIGO.BVF").string().c_str()));
    for (unsigned side = 0; side < 2; ++side) {
        checkVolley(stage, pain, rival, side);
        checkProbe(stage, pain, rival, side, 165, false, true);
        checkProbe(stage, pain, rival, side, 650, false, false);
        checkProbe(stage, pain, rival, side, 220, true, true);
        checkIce(stage, ice, rival, side, 260);
        checkIce(stage, ice, rival, side, 650);
        checkLanding(stage, sakura, rival, side);
    }
    std::puts("PASS: three child projectiles, grounded and airborne contact branches, target range and landing loop in both directions");
}
