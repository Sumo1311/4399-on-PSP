#include "fighter_cpu.hpp"
#include "versus_round.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace bvn;

namespace {

FighterAsset load(const std::filesystem::path& path) {
    FighterAsset asset;
    if (!asset.open(path.string().c_str())) {
        std::fprintf(stderr, "Cannot load %s\n", path.string().c_str());
        std::abort();
    }
    return asset;
}

void check(const StageAsset& stage, const std::filesystem::path& path, bool team) {
    std::array<FighterAsset, 3> first{load(path/"ICHIGO.BVF"), load(path/"KENPACHI.BVF"), load(path/"RUKIA.BVF")};
    std::array<FighterAsset, 3> second{load(path/"NARUTO.BVF"), load(path/"SASUKE.BVF"), load(path/"LEE.BVF")};
    const auto gin = load(path/"AGIN.BVS"), naruto = load(path/"ANARUTO.BVS");
    const auto mode = team ? BattleMode::TeamWatch : BattleMode::Watch;
    assert(isWatchMode(mode) && isTeamMode(mode) == team && !isArcadeMode(mode));
    TeamFactions factions{{{0,0,0},{1,1,1}}};
    VersusRound game(stage, std::span(&first[0],1), std::span(&second[0],1), 60, {}, mode,
                     {{{&gin,0},{&naruto,1}}}, factions);
    game.seedObjects(764231);
    std::array<FighterCpu,2> cpu{FighterCpu(7311),FighterCpu(8843)};
    game.bindCpu(0, &cpu[0]);
    game.bindCpu(1, &cpu[1]);
    std::array<unsigned,2> attacks{}, moves{};
    unsigned ticks=0, replacements=0;
    for (; ticks < LogicHz * 1800 && game.phase() != RoundPhase::Finished; ++ticks) {
        if (game.phase() == RoundPhase::TeamChange) {
            assert(team);
            std::array<TeamEntrant,2> entrants{};
            for (unsigned side=0; side<2; ++side) {
                if (const auto member=game.entering(side)) {
                    const auto& actor = side ? second[*member] : first[*member];
                    entrants[side] = {std::span(&actor,1),0};
                    cpu[side]=FighterCpu(7311 + ticks + side);
                    ++replacements;
                }
            }
            assert(game.enterTeamMembers(entrants));
            continue;
        }
        game.step({});
        for (unsigned side=0; side<2; ++side) {
            const auto state = game.match().actor(side).aiState();
            attacks[side] += state == AiState::Attack || state == AiState::Skill ||
                state == AiState::Super || state == AiState::Ultimate;
            moves[side] += cpu[side].controls().horizontal != 0;
        }
    }
    if(game.phase()!=RoundPhase::Finished) {
        std::fprintf(stderr,"Unfinished watch team=%d ticks=%u rounds=%u hp=%.1f,%.1f\n",team,ticks,game.round(),
                     game.match().actor(0).health(),game.match().actor(1).health());
        std::abort();
    }
    assert(attacks[0]>0&&attacks[1]>0&&moves[0]>0&&moves[1]>0);
    const auto winner=unsigned(game.winner());
    assert(winner<2&&game.wins(winner)==(team?3:2)&&game.wins(1-winner)<game.wins(winner));
    if(team) assert(replacements>=2);
    std::printf("PASS %s watch finished autonomously, rounds=%u ticks=%u wins=%u,%u replacements=%u attacks=%u,%u\n",
                team?"team":"single",game.round(),ticks,game.wins(0),game.wins(1),replacements,attacks[0],attacks[1]);
}

}

int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const std::filesystem::path path(argv[1]);
    StageAsset stage;
    assert(stage.open((path/"JIANCHANG.BVN").string().c_str()));
    check(stage,path,false);
    check(stage,path,true);
}
