// Adapted from 5DPLAY Game Studio's MessionModel.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "roster_asset.hpp"
#include "versus_round.hpp"

namespace bvn {

struct ArcadeStage {
    std::array<std::uint16_t, TeamSize> opponents;
    std::uint16_t assister, map, count;
    std::uint16_t healthPercent, enemyAttackPercent;

    std::array<unsigned, TeamSize> lineup(unsigned seed, bool team) const;
};

class ArcadeRun {
public:
    static constexpr unsigned StageCount = 8;
    bool open(const char* path, const RosterAsset& roster, const RosterAsset& assists, unsigned maps);
    const ArcadeStage& stage(unsigned faction, bool team) const;
    unsigned stageIndex() const { return stage_; }
    unsigned score() const { return score_; }
    bool lastStage() const { return stage_ + 1 == StageCount; }

    void reset();
    void addScore(unsigned points);
    void advance(bool firstTeamMember);
    void retry();
    void setClassicScore(bool value){classicScore_=value;}

private:
    std::array<std::array<ArcadeStage, StageCount>, 4> routes_{};
    unsigned stage_ = 0, score_ = 0;
    bool classicScore_=true;
};

} // namespace bvn
