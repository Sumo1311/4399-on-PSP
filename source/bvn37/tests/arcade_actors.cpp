// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_match.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace bvn;

namespace {

FighterAsset loadFighter(const std::filesystem::path& path) {
    FighterAsset asset;
    assert(asset.open(path.string().c_str()));
    return asset;
}

bool detachedAt(const FightMatch& match, unsigned phase) {
    for (const auto& object : match.objects()) {
        const auto* attacker = std::get_if<FightAttacker>(&object);
        if (attacker && attacker->owner() == 0 && attacker->phase() == phase) return true;
    }
    return false;
}

void checkByakuya(const StageAsset& stage, const FighterAsset& byakuya, const FighterAsset& opponent) {
    const auto floor = stage.geometry();
    FightMatch match(byakuya, opponent);
    match.reset(stage, {330, floor.playerBottom}, {600, floor.playerBottom});
    match.actor(0).setQi(FighterMaxQi);
    unsigned tick = 0;
    const auto step = [&](FighterControls first = {}, FighterControls second = {}) {
        match.step(floor, {first, second}, ++tick % 2 == 0, Contacts::Locked);
    };
    step({.superPressed = true});
    while (!detachedAt(match, 6) && tick < 300) step();
    assert(detachedAt(match, 6));
    const double cap = match.actor(0).qi();
    assert(cap < FighterMaxQi - 10);
    match.actor(0).setQi(cap - 5);
    step();
    assert(match.actor(0).qi() == cap - 5);
    match.actor(0).setQi(cap + 5);
    step();
    assert(match.actor(0).qi() == cap);

    step({}, {.vertical = -1});
    assert(match.actor(1).guardClip());
    match.actor(0).setQi(cap + 7);
    step({}, {.vertical = -1});
    assert(match.actor(0).qi() == cap + 7);
    while (match.actor(1).guardClip() && tick < 340) step();
    assert(!match.actor(1).guardClip() && match.actor(0).qi() == cap);
    while (!detachedAt(match, 33) && tick < 360) step();
    assert(detachedAt(match, 33));
    match.actor(0).setQi(cap + 9);
    step();
    assert(match.actor(0).qi() == cap + 9);
    std::puts("PASS: Byakuya's shijie holds gains at the captured Qi, skips guarding targets and releases on frame 34");
}

void checkKillerBee(const StageAsset& stage, const FighterAsset& bee, const FighterAsset& opponent) {
    const auto floor = stage.geometry();
    FightMatch match(bee, opponent);
    match.reset(stage, {330, floor.playerBottom}, {600, floor.playerBottom});
    match.actor(0).setQi(FighterMaxQi);
    match.step(floor, {{FighterControls{.vertical = -1, .superPressed = true}, {}}}, true, Contacts::Locked);
    bool first = false, second = false, ended = false;
    for (unsigned tick = 0; tick < 500; ++tick) {
        match.step(floor, {}, tick % 2 == 0, Contacts::Locked);
        const auto cutin = match.actor(0).cutin();
        if (match.actor(0).timelineFrame() == 768 && cutin.kind == CutinKind::Ultimate) {
            assert(cutin.face != MoveClip::Missing);
            first = true;
        }
        if (match.actor(0).timelineFrame() == 825 && cutin.kind == CutinKind::Super) {
            assert(cutin.face == MoveClip::Missing);
            second = true;
        }
        if (second && match.actor(0).timelineFrame() == 900 && cutin.kind == CutinKind::None) ended = true;
    }
    assert(first && second && ended);
    std::puts("PASS: Killer Bee's ultimate uses its registered portrait, then runs the second cut-in without a portrait and resumes");
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path package = argv[1];
    StageAsset stage;
    assert(stage.open((package / "LIUHUNJIE.BVN").string().c_str()));
    const auto byakuya = loadFighter(package / "BYAKUYA.BVF");
    const auto bee = loadFighter(package / "KILLERBE.BVF");
    const auto ichigo = loadFighter(package / "ICHIGO.BVF");
    checkByakuya(stage, byakuya, ichigo);
    checkKillerBee(stage, bee, ichigo);
}
