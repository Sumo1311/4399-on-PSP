#include "fight_match.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace bvn;

namespace {

struct Trial {
    Trial(const StageAsset& stage, std::span<const FighterAsset> first, std::span<const FighterAsset> second,
          double distance = 200, std::array<unsigned, 2> startingForms = {})
        : arena(stage), floor(stage.geometry()), match(first, second, startingForms), rivalX(350 + distance) {
        reset(false);
    }

    void reset(bool keepGauges) {
        match.reset(arena, {350, floor.playerBottom}, {rivalX, floor.playerBottom}, keepGauges);
    }

    void step(FighterControls input = {}) {
        match.step(floor, {input, {}}, tick++ % 2 == 0);
    }

    void charge(unsigned hits = 20) {
        FighterStrike strike{};
        strike.damage = 100;
        for (unsigned i = 0; i < hits; ++i) match.actor(0).scoreStrike({strike, 1, 350});
    }

    void transform(int direction, unsigned to, bool portraitExpected = true) {
        auto& actor = match.actor(0);
        const auto& rival = match.actor(1);
        const auto* before = &actor.asset();
        const auto hp = actor.health();
        charge();
        step({.vertical = direction, .attackPressed = true, .attackHeld = true, .transformHeld = true});
        assert(actor.transforming() && actor.qi() == 0 && actor.hurtBody().empty());

        bool switched = false, portrait = false, cinematic = false;
        for (unsigned i = 0; i < 600 && actor.transforming(); ++i) {
            const bool frozen = match.cinematic();
            const auto rivalPos = rival.motion().position;
            const auto rivalHp = rival.health();
            if (frozen) {
                cinematic = true;
                const auto scene = match.scene();
                assert(scene.hideMap && actor.cutin().kind == CutinKind::Transformation && !match.roundClockTick(true));
                assert(!scene.portrait || scene.portraitAsset == before);
                portrait |= scene.portrait != nullptr;
            }
            step();
            if (frozen) {
                assert(rival.motion().position.x == rivalPos.x && rival.motion().position.y == rivalPos.y);
                assert(rival.health() == rivalHp);
            }
            if (!switched && &actor.asset() != before) {
                switched = true;
                assert(actor.form() == to && actor.performing(FighterCommand::Intro));
                assert(actor.health() == hp && actor.qi() == 0);
            }
        }
        assert(cinematic && portrait == portraitExpected && switched);
        assert(actor.form() == to && !actor.transforming() && !actor.acting() && !match.cinematic());
        assert(actor.health() == hp);
    }

    void checkMovement() {
        auto& actor = match.actor(0);
        step({.horizontal = -1});
        assert(actor.motion().velocity.x == -actor.asset().stats().speed);
        step();
    }

    bool reachFrame(unsigned frame, unsigned limit = 420) {
        for (unsigned i = 0; i < limit && match.actor(0).timelineFrame() != frame; ++i) step();
        return match.actor(0).timelineFrame() == frame;
    }

    void jump() {
        step({.jumpPressed = true});
        for (unsigned i = 0; i < 20 && !match.actor(0).motion().airborne; ++i) step();
        assert(match.actor(0).motion().airborne);
    }

    const StageAsset& arena;
    StageGeometry floor;
    FightMatch match;
    double rivalX;
    unsigned tick = 0;
};

template<std::size_t N>
void load(const std::filesystem::path& directory, std::array<FighterAsset, N>& assets,
          const std::array<const char*, N>& files) {
    for (std::size_t i = 0; i < N; ++i) assert(assets[i].open((directory / files[i]).string().c_str()));
}

void testTransformedActions(const StageAsset& stage, std::span<const FighterAsset, 4> ichigo,
                            std::span<const FighterAsset, 3> naruto) {
    Trial half(stage, ichigo.subspan(2, 1), naruto);
    half.step({.vertical = -1, .specialPressed = true});
    assert(half.reachFrame(397));
    FighterStrike attack{};
    attack.damage = 80;
    auto& mask = half.match.actor(0);
    mask.receiveStrike({attack, -1, 550}, &half.match.actor(1));
    assert(mask.timelineFrame() == 418 && mask.health() == FighterMaxHealth);
    assert(half.reachFrame(1));
    half.jump();
    half.charge(7);
    const auto before = mask.qi();
    half.step({.superPressed = true});
    assert(mask.casting() && mask.performing(FighterCommand::SuperAir) && mask.qi() == before - 100);
    assert(half.reachFrame(1));

    Trial vizored(stage, ichigo.subspan(3, 1), naruto);
    vizored.jump();
    for (unsigned i = 0; i < 18; ++i) vizored.step();
    vizored.step({.specialPressed = true});
    assert(vizored.reachFrame(102));
    assert(vizored.reachFrame(104));

    Trial hit(stage, naruto.subspan(1, 1), ichigo, 45);
    hit.step({.vertical = 1, .attackPressed = true});
    assert(hit.reachFrame(287) && hit.match.actor(1).health() < FighterMaxHealth);
    Trial miss(stage, naruto.subspan(1, 1), ichigo, 350);
    miss.step({.vertical = 1, .attackPressed = true});
    assert(miss.reachFrame(272) && !miss.reachFrame(287));
    assert(miss.match.actor(1).health() == FighterMaxHealth);

    Trial four(stage, naruto.subspan(2, 1), ichigo, 160);
    four.step({.vertical = 1, .attackPressed = true});
    assert(four.reachFrame(497) && !four.match.objects().empty());
    const auto* claw = std::get_if<FightAttacker>(&four.match.objects().front());
    assert(claw && &claw->asset() == &naruto[2]);
    assert(std::abs(claw->position().x - four.match.actor(1).motion().position.x) < 12);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path directory(argv[1]);
    StageAsset stage;
    assert(stage.open((directory / "JIANCHANG.BVN").string().c_str()));
    std::array<FighterAsset, 4> ichigo;
    std::array<FighterAsset, 3> naruto;
    load(directory, ichigo, {"ICHIGO.BVF", "ICHIGOB.BVF", "ICHIGOH.BVF", "ICHIGOV.BVF"});
    load(directory, naruto, {"NARUTO.BVF", "NARUTO1.BVF", "NARUTO4.BVF"});
    std::array<FighterAsset, 2> rukia;
    load(directory, rukia, {"RUKIA.BVF", "RUKIAS.BVF"});

    Trial blade(stage, ichigo, naruto);
    blade.charge(19);
    blade.step({.attackPressed = true, .attackHeld = true, .transformHeld = true});
    assert(!blade.match.actor(0).acting() && blade.match.actor(0).qi() == 285);
    blade.transform(0, 1);
    blade.checkMovement();
    // The bankai script requests a portrait registered only after entering full vizored.
    blade.transform(0, 3, false);
    blade.charge();
    blade.reset(true);
    assert(blade.match.actor(0).form() == 3 && blade.match.actor(0).qi() == 300);
    blade.step({.transformHeld = true});
    assert(!blade.match.actor(0).transforming() && blade.match.actor(0).qi() == 300);
    blade.reset(false);
    assert(blade.match.actor(0).form() == 0 && blade.match.actor(0).qi() == 0);

    Trial mask(stage, ichigo, naruto);
    FighterStrike bruise{};
    bruise.damage = 100;
    bruise.hurtFrames = 6;
    mask.match.actor(0).receiveStrike({bruise, -1, 550}, &mask.match.actor(1));
    for (unsigned i = 0; i < 24; ++i) mask.step();
    assert(mask.match.actor(0).health() == 900);
    mask.transform(-1, 2);
    assert(ichigo[2].has(ichigo[2].command(FighterCommand::SuperAir)));
    bool tiled = false;
    const auto ultimate = ichigo[2].command(FighterCommand::SuperDown);
    for (unsigned i = 0; i < ichigo[2].clip(ultimate).count; ++i) {
        const auto& pose = ichigo[2].frame(ultimate, i).pose;
        if (!pose.tiled()) continue;
        tiled = true;
        const auto pieces = ichigo[2].poseTiles(pose);
        assert(pieces.size() >= 2);
        for (const auto& tile : pieces) {
            std::vector<std::byte> pixels(FighterAsset::textureSize(tile));
            assert(ichigo[2].decodePixels(tile, pixels));
        }
    }
    assert(tiled);
    mask.transform(0, 3);

    Trial selectedMask(stage, ichigo, naruto, 200, {2, 0});
    assert(selectedMask.match.actor(0).form() == 2);
    selectedMask.transform(0, 3, false);
    selectedMask.reset(true);
    assert(selectedMask.match.actor(0).form() == 3);
    selectedMask.reset(false);
    assert(selectedMask.match.actor(0).form() == 2);

    Trial tails(stage, naruto, ichigo);
    tails.transform(0, 1);
    tails.checkMovement();
    tails.transform(0, 2);
    tails.checkMovement();
    Trial ice(stage, rukia, naruto);
    ice.transform(0, 1);
    ice.reset(true);
    assert(ice.match.actor(0).form() == 1);
    testTransformedActions(stage, ichigo, naruto);
    std::puts("PASS: seven transformation routes, direct form selection, cut-in portraits, charge, health, pose tiles, and round persistence");
}
