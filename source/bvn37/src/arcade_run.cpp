// SPDX-License-Identifier: GPL-3.0-or-later
#include "arcade_run.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

namespace bvn {

static_assert(sizeof(ArcadeStage) == 16);

std::array<unsigned, TeamSize> ArcadeStage::lineup(unsigned seed, bool team) const {
    if (!team) return {opponents[seed % count], MissingFighter, MissingFighter};
    std::array<unsigned, TeamSize> order{opponents[0], opponents[1], opponents[2]};
    for (unsigned remaining = count; remaining > 1; --remaining) {
        std::swap(order[remaining - 1], order[seed % remaining]);
        seed /= remaining;
    }
    return order;
}

bool ArcadeRun::open(const char* path, const RosterAsset& roster, const RosterAsset& assists, unsigned maps) {
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    char magic[8]{};
    if (!file || std::fread(magic, 1, sizeof(magic), file.get()) != sizeof(magic) ||
        std::memcmp(magic, "BVNARC01", sizeof(magic))) return false;
    decltype(routes_) routes;
    for (auto& route : routes) {
        if (std::fread(route.data(), sizeof(ArcadeStage), route.size(), file.get()) != route.size()) return false;
        for (const auto& stage : route) {
            if (!stage.count || stage.count > TeamSize || stage.assister >= assists.size() ||
                assists.fighter(stage.assister).random || stage.map >= maps ||
                !stage.healthPercent || stage.healthPercent > 1000 ||
                !stage.enemyAttackPercent || stage.enemyAttackPercent > 1000) return false;
            for (unsigned i = 0; i < TeamSize; ++i) {
                const unsigned id = stage.opponents[i];
                if (i < stage.count ? id >= roster.size() || roster.fighter(id).random : id != MissingFighter) return false;
            }
        }
    }
    if (std::fgetc(file.get()) != EOF || std::ferror(file.get())) return false;
    routes_ = routes;
    reset();
    return true;
}

const ArcadeStage& ArcadeRun::stage(unsigned faction, bool team) const {
    return routes_[2 * faction + !team][stage_];
}

void ArcadeRun::reset() {
    stage_ = score_ = 0;
}

void ArcadeRun::addScore(unsigned points) {
    score_ += std::min(points, std::numeric_limits<unsigned>::max() - score_);
}

void ArcadeRun::advance(bool firstTeamMember) {
    if (lastStage()) return;
    ++stage_;
    addScore(FightMatch::modeScore(firstTeamMember ? 15000 : 5000,classicScore_));
}

void ArcadeRun::retry() {
    score_ -= std::min(score_, 10000U);
}

} // namespace bvn
