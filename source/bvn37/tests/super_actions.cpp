#include "fight_match.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace bvn;

namespace {

struct Trial {
    Trial(const StageAsset& arena, const FighterAsset& first, const FighterAsset& second, int distance)
        : stage(arena.geometry()), match(first, second) {
        match.reset(arena, {350, stage.playerBottom}, {350.0 + distance, stage.playerBottom});
    }

    void charge(unsigned hits) {
        FighterStrike strike{};
        strike.damage = 100;
        for (unsigned i = 0; i < hits; ++i) match.actor(0).scoreStrike({strike, 1, 350});
    }

    void step(FighterControls input = {}) {
        const bool animate = tick++ % 2 == 0;
        const auto& defender = match.actor(1);
        const auto position = defender.motion().position;
        const double health = defender.health();
        const bool frozen = match.cinematic();
        if (frozen) {
            const auto view = match.scene();
            assert(view.hideMap && view.portrait && view.portraitAsset);
            assert(!match.roundClockTick(animate));
            ++cutinTicks;
        }
        const unsigned before = frame();
        const auto objectCount = match.objects().size();
        match.step(stage, {input, {}}, animate);
        if (frozen) {
            assert(defender.motion().position.x == position.x && defender.motion().position.y == position.y);
            assert(defender.health() == health && match.objects().size() == objectCount);
        }
        if (frame() != before) frames.push_back(frame());
        if (match.objects().size() > objectCount) {
            spawns.push_back(frame());
            lastSpawnX = std::visit([](const auto& actor) { return actor.position().x; }, match.objects().back());
        }
    }

    void cast(int direction) {
        const double before = match.actor(0).qi();
        step({.vertical = direction, .superPressed = true});
        assert(match.actor(0).casting());
        assert(match.actor(0).qi() == before - (direction < 0 ? 300 : 100));
    }

    void finish(unsigned ticks = 420) {
        for (unsigned active = 0, wall = 0; active < ticks; ++wall) {
            assert(wall < ticks * 20);
            if (!match.hitFreeze()) ++active;
            step();
        }
        assert(!match.cinematic() && !match.actor(0).casting() && match.objects().empty());
    }

    unsigned frame() const { return match.actor(0).timelineFrame(); }
    bool saw(unsigned authoredFrame) const { return std::ranges::find(frames, authoredFrame) != frames.end(); }
    double rivalHp() const { return match.actor(1).health(); }

    StageGeometry stage;
    FightMatch match;
    unsigned tick = 0, cutinTicks = 0;
    std::vector<unsigned> frames, spawns;
    double lastSpawnX = 0;
};

void testIchigo(const StageAsset& stage, const FighterAsset& ichigo, const FighterAsset& naruto) {
    Trial normal(stage, ichigo, naruto, 125);
    normal.step({.superPressed = true});
    assert(normal.frame() == 1 && !normal.match.cinematic());
    normal.charge(7);
    normal.cast(0);
    normal.finish();
    assert(normal.cutinTicks == 56 && normal.rivalHp() == 600);
    assert((normal.spawns == std::vector<unsigned>{518, 529, 550}));

    Trial upward(stage, ichigo, naruto, 50);
    upward.charge(7);
    upward.cast(1);
    upward.finish();
    assert(upward.cutinTicks == 24 && upward.saw(636) && upward.rivalHp() == 700);
    assert(upward.match.actor(0).qi() == 5);

    Trial ultimate(stage, ichigo, naruto, 80);
    ultimate.charge(20);
    ultimate.cast(-1);
    ultimate.finish();
    assert(ultimate.cutinTicks == 32 && ultimate.saw(694) && ultimate.rivalHp() == 450);
    assert(ultimate.match.actor(0).qi() == 0);
}

void testNaruto(const StageAsset& stage, const FighterAsset& ichigo, const FighterAsset& naruto) {
    Trial close(stage, naruto, ichigo, 50);
    close.charge(7);
    close.cast(0);
    close.finish();
    assert(close.cutinTicks == 46 && close.saw(640) && close.saw(669) && close.rivalHp() < 980);
    assert(close.match.actor(0).qi() == 5);

    Trial distant(stage, naruto, ichigo, 380);
    distant.charge(7);
    distant.cast(0);
    distant.finish();
    assert(distant.saw(683) && !distant.saw(640) && distant.rivalHp() == 1000);

    Trial clone(stage, naruto, ichigo, 220);
    clone.charge(7);
    clone.cast(1);
    clone.finish();
    assert((clone.spawns == std::vector<unsigned>{780}));
    assert(clone.cutinTicks == 138 && clone.lastSpawnX == 570 && clone.rivalHp() < 1000);

    Trial ultimate(stage, naruto, ichigo, 125);
    ultimate.charge(20);
    ultimate.cast(-1);
    ultimate.finish();
    assert((ultimate.spawns == std::vector<unsigned>{980}));
    assert(ultimate.cutinTicks == 136 && ultimate.saw(978) && ultimate.saw(1035));
    assert(ultimate.rivalHp() < 900 && ultimate.match.actor(0).health() == 1000);
}

void testAirAndRush(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second,
                    bool naruto) {
    Trial air(stage, first, second, 140);
    air.step({.jumpPressed = true});
    for (unsigned i = 0; i < 20 && !air.match.actor(0).motion().airborne; ++i) air.step();
    assert(air.match.actor(0).motion().airborne);
    air.step({.specialPressed = true});
    air.finish(180);
    if (naruto) assert(air.saw(121));
    else assert((air.spawns == std::vector<unsigned>{109}));
    assert(!air.match.actor(0).motion().airborne);

    Trial rush(stage, first, second, 30);
    rush.step({.vertical = -1, .attackPressed = naruto, .specialPressed = !naruto});
    rush.finish(180);
    const auto landing = std::ranges::find(rush.frames, naruto ? 245U : 319U);
    assert(landing != rush.frames.end() && landing != rush.frames.begin());
    assert(*(landing - 1) < *landing - 1);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    StageAsset stage;
    FighterAsset ichigo, naruto;
    assert(stage.open(argv[1]) && ichigo.open(argv[2]) && naruto.open(argv[3]));
    testIchigo(stage, ichigo, naruto);
    testNaruto(stage, ichigo, naruto);
    testAirAndRush(stage, ichigo, naruto, false);
    testAirAndRush(stage, naruto, ichigo, true);
    std::puts("PASS: six supers, charge, cut-in clock, hit branches, targeted attacker, landing, air skills, and rush contact");
}
