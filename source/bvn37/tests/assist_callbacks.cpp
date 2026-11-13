#include "fight_match.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string_view>

using namespace bvn;

namespace {

FighterAsset load(const std::filesystem::path& path) {
    FighterAsset asset;
    if (!asset.open(path.string().c_str())) {
        std::fprintf(stderr, "Cannot open %s\n", path.string().c_str());
        std::abort();
    }
    return asset;
}

const FightAttacker* root(const FightMatch& match, unsigned side) {
    for (const auto& object : match.objects()) {
        if (const auto* attacker = std::get_if<FightAttacker>(&object);
            attacker && attacker->assister() && attacker->owner() == side && attacker->active()) return attacker;
    }
    return nullptr;
}

struct Trial {
    FightMatch match;
    const StageAsset& stage;
    unsigned tick = 0, side;
    FighterControls opponent{};

    Trial(const StageAsset& stage, const FighterAsset& fighter, const FighterAsset& assist,
          unsigned side = 0, double gap = 260)
        : match(std::span(&fighter, 1), std::span(&fighter, 1), {},
                side ? std::array<AssisterChoice,2>{{{}, {&assist, 0}}}
                     : std::array<AssisterChoice,2>{{{&assist, 0}, {}}}),
          stage(stage), side(side) {
        match.reset(stage, {350, stage.geometry().playerBottom}, {350 + gap, stage.geometry().playerBottom});
    }

    void step(Contacts contacts = Contacts::Training) {
        std::array<FighterControls, 2> input{};
        input[1-side] = opponent;
        input[side].assistHeld = tick == 6;
        match.step(stage.geometry(), input, ++tick % 2 == 0, contacts);
    }
};

void checkSupport(const StageAsset& stage, const FighterAsset& fighter, const std::filesystem::path& path) {
    const auto yoruichi = load(path / "AYORUIC.BVS");
    const auto temari = load(path / "ATEMARI.BVS");
    const auto swap = load(path / "AYLS.BVS");
    for (unsigned side = 0; side < 2; ++side) {
        Trial speed(stage, fighter, yoruichi, side);
        const auto start = speed.match.actor(side).motion().position;
        while (!root(speed.match, side)) { assert(speed.tick < 15); speed.step(); }
        assert(speed.match.actor(side).motion().position.x == start.x + (side ? -120 : 120));
        assert(speed.match.actor(side).speed() == fighter.stats().speed + 2.5);
        for (unsigned i = 0; i < 750; ++i) speed.step();
        assert(speed.match.actor(side).speed() == fighter.stats().speed);

        Trial wind(stage, fighter, temari, side);
        bool blew = false;
        while (wind.tick < 200 && !blew) {
            const double x = wind.match.actor(1-side).motion().position.x;
            wind.step();
            const auto* assist = root(wind.match, side);
            if (!assist || assist->phase() + 1 != 35) continue;
            assert(fighter.stats().speed == 8 && fighter.stats().weight == 3);
            // The source's weight-3 branch writes an 8 * .5 displacement twice.
            const double expected = side ? -8 : 8;
            assert(std::abs(wind.match.actor(1-side).motion().position.x - x - expected) < 1e-6);
            assert(wind.match.actor(side).speed() == fighter.stats().speed + 2);
            blew = true;
        }
        assert(blew);

        Trial exchange(stage, fighter, swap, side);
        Point self{}, target{};
        bool remembered = false, exchanged = false;
        for (unsigned i = 0; i < 60 && !exchanged; ++i) {
            exchange.opponent.jumpPressed = exchange.tick == 10;
            exchange.step();
            const auto* assist = root(exchange.match, side);
            if (!assist) continue;
            if (assist->phase() + 1 == 9 && !remembered) {
                self = exchange.match.actor(side).motion().position;
                target = exchange.match.actor(1-side).motion().position;
                remembered = true;
            }
            if (assist->phase() + 1 == 10) {
                assert(remembered);
                assert(exchange.match.actor(side).motion().position.x == target.x);
                assert(exchange.match.actor(side).motion().position.y == target.y);
                assert(exchange.match.actor(1-side).motion().position.x == self.x);
                assert(exchange.match.actor(1-side).motion().position.y == self.y);
                assert(assist->position().x == target.x);
                assert(target.y < self.y);
                exchanged = true;
            }
        }
        assert(exchanged);
    }
    std::puts("PASS Yoruichi teleport and timed buff, Temari double wind displacement, and YLS saved-position swap on both sides");
}

void checkFollowUps(const StageAsset& stage, const FighterAsset& fighter, const std::filesystem::path& path) {
    for (const auto [file, frame] : {std::pair{"AMINATO.BVS", 62U}, {"AIKKAKU.BVS", 58U}, {"AKABUTO.BVS", 61U}}) {
        auto asset = load(path / file);
        for (unsigned side = 0; side < 2; ++side) {
            bool reached = false;
            unsigned gap = 0;
            for (gap = 20; gap <= 300 && !reached; gap += 10) {
                Trial game(stage, fighter, asset, side, gap);
                for (unsigned tick = 0; tick < 450; ++tick) {
                    game.step();
                    const auto* assist = root(game.match, side);
                    reached |= assist && assist->phase() + 1 == frame;
                }
            }
            assert(reached);
            Trial blocked(stage, fighter, asset, side, gap - 10);
            for (unsigned tick = 0; tick < 8; ++tick) blocked.step();
            assert(root(blocked.match, side));
            for (unsigned tick = 0; tick < 450; ++tick) {
                blocked.step(Contacts::Locked);
                const auto* assist = root(blocked.match, side);
                assert(!assist || assist->phase() + 1 != frame);
            }
        }
        std::printf("PASS %s source hit-confirm follow-up and miss branch on both sides\n", file);
    }
}

void checkBinding(const StageAsset& stage, const FighterAsset& fighter, const std::filesystem::path& path) {
    auto asset = load(path / "ABYAKUY.BVS");
    for (unsigned side = 0; side < 2; ++side) {
        bool bound = false, released = false;
        for (unsigned gap = 20; gap < 320 && !bound; gap += 10) {
            Trial game(stage, fighter, asset, side, gap);
            for (unsigned tick = 0; tick < 600; ++tick) {
                game.step();
                if (!game.match.actor(1-side).motion().applyGravity) bound = true;
                if (bound && game.match.actor(1-side).motion().applyGravity && game.match.objects().empty()) released = true;
            }
        }
        assert(bound && released);
    }
    std::puts("PASS Byakuya hurt-state binding, detached cage release and gravity restoration on both sides");
}

void checkHoming(const StageAsset& stage, const FighterAsset& fighter, const std::filesystem::path& path) {
    auto asset = load(path / "ARENJI.BVS");
    for (unsigned side = 0; side < 2; ++side) {
        Trial game(stage, fighter, asset, side);
        bool moved = false, attacked = false;
        for (unsigned tick = 0; tick < 700; ++tick) {
            game.step();
            for (const auto& object : game.match.objects()) {
                const auto* attacker = std::get_if<FightAttacker>(&object);
                if (!attacker || attacker->assister()) continue;
                moved |= std::abs(attacker->velocity().x) == 5;
                attacked |= attacker->phase() >= 149 && attacker->velocity().x == 0;
            }
        }
        assert(moved && attacked && game.match.objects().empty());
    }
    std::puts("PASS Renji delayed homing, arrival attack and detached lifetime on both sides");
}

void checkClones(const StageAsset& stage, const FighterAsset& fighter, const std::filesystem::path& path) {
    auto asset = load(path / "ANARUTO.BVS");
    Trial first(stage, fighter, asset), second(stage, fighter, asset);
    first.match.seedObjects(764231);
    second.match.seedObjects(764231);
    std::set<int> vertical;
    unsigned largest = 0;
    for (unsigned tick = 0; tick < 700; ++tick) {
        first.step();
        second.step();
        const auto a = first.match.objects(), b = second.match.objects();
        assert(a.size() == b.size());
        largest = std::max(largest, unsigned(a.size()));
        for (unsigned i = 0; i < a.size(); ++i) {
            const auto* left = std::get_if<FightAttacker>(&a[i]);
            const auto* right = std::get_if<FightAttacker>(&b[i]);
            if (!left || !right) continue;
            assert(left->phase() == right->phase() && left->position().x == right->position().x &&
                   left->position().y == right->position().y && left->velocity().x == right->velocity().x);
            if (!left->assister()) vertical.insert(int(left->velocity().y));
        }
    }
    assert(largest >= 4 && vertical.contains(-4) && vertical.contains(0) && vertical.contains(4));
    assert(first.match.objects().empty());
    std::puts("PASS Naruto random clone directions, deterministic shared seed, wall removal and full cleanup");
}

void checkFire(const StageAsset& stage, const FighterAsset& fighter, const std::filesystem::path& path) {
    const auto asset = load(path / "AYAMAMOT.BVS");
    for (unsigned side = 0; side < 2; ++side) {
        Trial normal(stage, fighter, asset, side), protectedOwner(stage, fighter, asset, side);
        protectedOwner.match.actor(side).applyBuff(FighterBuff::Defense, .5, 1000);
        for (unsigned tick = 0; tick < 400; ++tick) {
            normal.step();
            protectedOwner.step();
        }
        assert(normal.match.actor(side).health() == 950);
        assert(protectedOwner.match.actor(side).health() == 975);
        assert(normal.match.actor(1-side).health() <= 850);
    }
    std::puts("PASS Yamamoto five authored self-damage frames, owner defense and opposing damage on both sides");
}

void checkMadara(const StageAsset& stage, const FighterAsset& fighter, const std::filesystem::path& path) {
    const auto asset = load(path / "AMADARA.BVS");
    for (unsigned side = 0; side < 2; ++side) {
        Trial hit(stage, fighter, asset, side, 55), miss(stage, fighter, asset, side, 55);
        bool missed = false;
        for (unsigned tick = 0; tick < 400; ++tick) {
            hit.step();
            miss.step(tick < 8 ? Contacts::Training : Contacts::Locked);
            for (const auto& object : miss.match.objects()) {
                if (const auto* attacker = std::get_if<FightAttacker>(&object);
                    attacker && !attacker->assister() && attacker->phase() == 38) missed = true;
            }
        }
        assert(missed && hit.match.actor(1-side).health() <= 850 && miss.match.actor(1-side).health() == 1000);
    }
    std::puts("PASS Madara detached hit and explicit miss timeline on both sides");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path path(argv[1]);
    StageAsset stage;
    assert(stage.open((path / "JIANCHANG.BVN").string().c_str()));
    const auto fighter = load(path / "ICHIGO.BVF");
    checkSupport(stage, fighter, path);
    checkFollowUps(stage, fighter, path);
    checkBinding(stage, fighter, path);
    checkHoming(stage, fighter, path);
    checkClones(stage, fighter, path);
    checkFire(stage, fighter, path);
    checkMadara(stage, fighter, path);
}
