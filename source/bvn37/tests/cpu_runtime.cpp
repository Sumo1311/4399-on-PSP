// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_cpu.hpp"
#include "versus_round.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace bvn;
namespace {

FighterAsset load(const std::filesystem::path& path) {
    FighterAsset value;
    if (!value.open(path.string().c_str())) {
        std::fprintf(stderr, "Cannot load %s\n", path.string().c_str());
        std::abort();
    }
    return value;
}

void traits(const std::filesystem::path& package, const std::filesystem::path& fixture) {
    std::ifstream rows(fixture);
    assert(rows);
    std::string file, previous;
    unsigned command, breaks, down, count = 0;
    FighterAsset asset;
    while (rows >> file >> command >> breaks >> down) {
        if (file != previous) { asset = load(package / file); previous = file; }
        FighterMotion actor(asset);
        actor.reset({400, 300}, 1);
        const auto actual = actor.aiHitTraits(FighterCommand(command));
        if (actual.breaksGuard != bool(breaks) || actual.knocksDown != bool(down)) {
            std::fprintf(stderr, "Trait mismatch %s command=%u actual=%d,%d expected=%u,%u\n",
                file.c_str(), command, actual.breaksGuard, actual.knocksDown, breaks, down);
            std::abort();
        }
        ++count;
    }
    assert(rows.eof() && count == 780);
    std::printf("PASS %u substring queries over all 78 normal fighter banks, including unused original hits\n", count);
}

FighterControls intent(std::initializer_list<AiIntent> flags) {
    FighterControls input{};
    input.aiControlled = true;
    for (const auto flag : flags) input.aiIntents |= 1U << unsigned(flag);
    return input;
}

void commands(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second) {
    const auto spawn = stage.spawn(0).value_or(Point{400, stage.geometry().playerBottom});
    const auto check = [&](std::initializer_list<AiIntent> flags, FighterCommand expected, unsigned qi = 300) {
        FightMatch match(first, second);
        match.reset(stage, spawn, {spawn.x + 150, spawn.y});
        match.actor(0).setQi(qi);
        match.step(stage.geometry(), {intent(flags), {}}, false, Contacts::Round);
        if (!match.actor(0).performing(expected)) {
            std::fprintf(stderr, "Wrong selected command %u at frame %u\n", unsigned(expected), match.actor(0).timelineFrame());
            std::abort();
        }
    };
    check({AiIntent::Attack}, FighterCommand::Attack);
    check({AiIntent::SkillDown}, FighterCommand::SkillDown);
    check({AiIntent::SkillUp}, FighterCommand::SkillUp);
    check({AiIntent::Special}, FighterCommand::Special);
    check({AiIntent::SpecialDown}, FighterCommand::SpecialDown);
    check({AiIntent::SpecialUp}, FighterCommand::SpecialUp);
    check({AiIntent::Super}, FighterCommand::Super);
    check({AiIntent::SuperUp}, FighterCommand::SuperUp);
    check({AiIntent::Ultimate}, FighterCommand::SuperDown);
    check({AiIntent::SkillDown, AiIntent::SkillUp, AiIntent::Attack}, FighterCommand::SkillUp);
    check({AiIntent::Ultimate, AiIntent::SuperUp, AiIntent::Super}, FighterCommand::SuperUp, 200);
    check({AiIntent::CatchAttack, AiIntent::Attack}, FighterCommand::Attack);
    FightMatch guard(first, second);
    guard.reset(stage, spawn, {spawn.x + 150, spawn.y});
    guard.step(stage.geometry(), {intent({AiIntent::Guard}), {}}, false, Contacts::Round);
    assert(guard.actor(0).guardClip());
    auto turn = intent({AiIntent::Guard, AiIntent::Left}); turn.horizontal = -1;
    guard.step(stage.geometry(), {turn, {}}, false, Contacts::Round);
    assert(guard.actor(0).guardClip() && guard.actor(0).facing() == -1);
    turn = intent({AiIntent::Guard, AiIntent::Right}); turn.horizontal = 1;
    guard.step(stage.geometry(), {turn, {}}, false, Contacts::Round);
    assert(guard.actor(0).guardClip() && guard.actor(0).facing() == 1);
    std::puts("PASS direct CPU ground commands, simultaneous intent ordering and resource fallback");
}

void fighterRow(std::FILE* out, const AiFighterState& value) {
    std::fprintf(out, " %.17g %.17g %.17g %.17g %.17g %.17g %d %d %u", value.x, value.y,
        value.health, value.maxHealth, value.qi, value.energy, value.facing, int(value.state), unsigned(value.fighter));
}

void record(std::FILE* input, std::FILE* output, const FighterCpu& cpu, unsigned& row) {
    const auto& view = cpu.lastObservation();
    std::fputc('F', input);
    fighterRow(input, view.self); fighterRow(input, view.target);
    std::fprintf(input, " %u %u %u %u", unsigned(view.targetBody), unsigned(view.targetHittable),
        unsigned(view.basicAttack), view.gameHz);
    for (const auto& action : view.actions) std::fprintf(input, " %u", unsigned(action.available) |
        (unsigned(action.inRange) << 1) | (unsigned(action.breaksGuard) << 2) | (unsigned(action.knocksDown) << 3));
    std::fprintf(input, " %u", unsigned(view.threats.size()));
    for (const auto& threat : view.threats) std::fprintf(input, " %u %.17g %.17g %u", unsigned(threat.kind),
        threat.x, threat.y, unsigned(threat.attacking));
    std::fputc('\n', input);
    std::fprintf(output, "%u %u %u %u\n", row++, cpu.intents(), cpu.policyRandomState(), cpu.movementFrames());
}

void battles(const StageAsset& stage, const FighterAsset& first, const FighterAsset& second,
             const std::filesystem::path& folder) {
    unsigned total = 0, totalEscapes = 0, totalCounters = 0, totalGhosts = 0;
    for (const unsigned hz : {30U, 60U}) for (unsigned level = 1; level <= 6; ++level) {
        const std::uint32_t seed = 7311 + 101 * level + hz;
        FighterCpu cpu(seed, level, hz), opponent(seed ^ 0x85ebca6bU, level, hz);
        VersusRound game(stage, std::span(&first, 1), std::span(&second, 1), 60, {},
            BattleMode::Versus, {}, TeamFactions{{{{0, 0, 0}}, {{1, 1, 1}}}});
        game.setGameHz(hz);
        game.bindCpu(0, &cpu); game.bindCpu(1, &opponent);
        std::array<std::FILE*, 2> inputs{}, outputs{};
        std::array<unsigned, 2> rows{}, oldDecisions{}, attacks{}, skills{}, supers{}, guards{};
        std::array<unsigned, 2> escapes{};
        std::array<bool, 2> wasEscaping{};
        std::array<unsigned, 2> counters{};
        std::array<bool, 2> wasCounterPaused{};
        std::array<unsigned, 2> ghosts{};
        std::array<bool, 2> wasGhostActive{};
        for (unsigned side = 0; side < 2; ++side) {
            const auto prefix = folder / ("level-" + std::to_string(level) + "-" + std::to_string(hz) + "-" + std::to_string(side));
            inputs[side] = std::fopen((prefix.string() + ".in").c_str(), "w");
            outputs[side] = std::fopen((prefix.string() + ".out").c_str(), "w");
            assert(inputs[side] && outputs[side]);
            std::fprintf(inputs[side], "R %u %u\n", level, side ? seed ^ 0x85ebca6bU : seed);
        }
        unsigned ticks = 0;
        for (; ticks < LogicHz * 400 && game.phase() != RoundPhase::Finished; ++ticks) {
            game.step({});
            for (unsigned side = 0; side < 2; ++side) {
                const auto& controller = side ? opponent : cpu;
                if (oldDecisions[side] != controller.decisions()) {
                    assert(controller.decisions() == oldDecisions[side] + 1);
                    oldDecisions[side] = controller.decisions();
                    assert(controller.decisions() == game.match().aiAnimation(side));
                    record(inputs[side], outputs[side], controller, rows[side]);
                }
                const auto state = game.match().actor(side).aiState();
                attacks[side] += state == AiState::Attack;
                skills[side] += state == AiState::Skill;
                supers[side] += state == AiState::Super || state == AiState::Ultimate;
                guards[side] += state == AiState::Guard;
                const bool escaping = game.match().actor(side).hurtEscaping();
                escapes[side] += escaping && !wasEscaping[side];
                wasEscaping[side] = escaping;
                const auto& actor = game.match().actor(side);
                if (actor.counterPaused() && !wasCounterPaused[side]) {
                    assert(actor.counterKind() == (side ? CounterKind::Replace : CounterKind::Explode));
                    ++counters[side];
                }
                wasCounterPaused[side] = actor.counterPaused();
                if (actor.ghostActive() && !wasGhostActive[side]) {
                    assert(actor.ghostKind() == GhostKind::Step);
                    ++ghosts[side];
                }
                wasGhostActive[side] = actor.ghostActive();
            }
        }
        for (unsigned side = 0; side < 2; ++side) {
            assert(std::fclose(inputs[side]) == 0 && std::fclose(outputs[side]) == 0);
            // Original probabilities do not require a basic attack in every seeded match.
            assert(attacks[side] + skills[side] + supers[side] > 0 && rows[side] > 100);
            total += rows[side];
        }
        if (game.phase() != RoundPhase::Finished) {
            std::fprintf(stderr, "Unfinished level=%u hz=%u ticks=%u round=%u\n", level, hz, ticks, game.round());
            std::abort();
        }
        totalEscapes += escapes[0] + escapes[1];
        totalCounters += counters[0] + counters[1];
        totalGhosts += ghosts[0] + ghosts[1];
        std::printf("PASS level=%u gameHz=%u autonomous rounds=%u ticks=%u wins=%u,%u decisions=%u,%u attacks=%u,%u skills=%u,%u supers=%u,%u guards=%u,%u escapes=%u,%u\n",
            level, hz, game.round(), ticks, game.wins(0), game.wins(1), rows[0], rows[1], attacks[0], attacks[1],
            skills[0], skills[1], supers[0], supers[1], guards[0], guards[1], escapes[0], escapes[1]);
        std::printf("Counter actions explode=%u replace=%u\n", counters[0], counters[1]);
        std::printf("Ghost actions=%u,%u\n", ghosts[0], ghosts[1]);
    }
    std::printf("Recorded %u decisions from actual native fights for the original AS3 policy reference\n", total);
    assert(totalEscapes > 0);
    std::printf("PASS %u source-policy recovery actions executed in autonomous fights\n", totalEscapes);
    assert(totalCounters > 0);
    std::printf("PASS %u source-policy counter actions executed in autonomous fights\n", totalCounters);
    std::printf("Observed %u source-policy ghost actions executed in autonomous fights\n", totalGhosts);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const std::filesystem::path package(argv[1]), folder(argv[3]);
    std::filesystem::create_directory(folder);
    traits(package, argv[2]);
    StageAsset stage;
    assert(stage.open((package / "JIANCHANG.BVN").string().c_str()));
    const auto first = load(package / "ICHIGO.BVF"), second = load(package / "NARUTO.BVF");
    commands(stage, first, second);
    battles(stage, first, second, folder);
}
