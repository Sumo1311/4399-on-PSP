// Adapted from 5DPLAY Game Studio's Musou model and controller.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_musou_result.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bvn {

struct MusouArea {
    char id[12], name[16];
    std::uint32_t prerequisites;
    std::uint16_t firstMission, missionCount;
};

struct MusouMission {
    char id[8], map[24];
    std::uint16_t seconds, enemyLevel, firstWave, waveCount;
};

enum class MusouRepeat : std::uint16_t { None, Timed, ReplaceDead };

struct MusouWave {
    std::uint16_t hold, firstEnemy, enemyCount, firstRepeat, repeatCount, repeatHold;
    MusouRepeat repeat;
};

struct MusouEnemy {
    char fighter[32];
    std::uint32_t hp;
    std::uint16_t attack, experience, money, amountAndBoss;

    unsigned amount() const { return amountAndBoss >> 1; }
    bool boss() const { return amountAndBoss & 1; }
};

class MusouCampaign {
public:
    bool open(const char* path);
    std::string_view world() const { return world_; }
    std::span<const MusouArea> areas() const { return areas_; }
    std::span<const MusouMission> missions() const { return missions_; }
    std::span<const MusouWave> waves() const { return waves_; }
    std::span<const MusouEnemy> enemies() const { return enemies_; }

private:
    char world_[16]{};
    std::vector<MusouArea> areas_;
    std::vector<MusouMission> missions_;
    std::vector<MusouWave> waves_;
    std::vector<MusouEnemy> enemies_;
};

class MusouProgress {
public:
    explicit MusouProgress(const MusouCampaign& campaign);
    bool areaOpen(unsigned area) const;
    std::optional<unsigned> nextMission(unsigned area) const;
    bool passMission(unsigned area, unsigned mission);
    bool restore(unsigned money, std::span<const std::uint8_t> stars);
    void earn(unsigned amount);
    bool spend(unsigned amount);
    unsigned money() const { return money_; }
    std::span<const std::uint8_t> stars() const { return stars_; }

private:
    const MusouCampaign& campaign_;
    std::vector<std::uint8_t> stars_;
    std::vector<unsigned> lastPassed_;
    unsigned money_ = 3000;
};

struct MusouSpawn {
    std::uint16_t enemy, member; // Each expanded Flash enemy has its own identity.
    bool repeat;
    bool operator==(const MusouSpawn&) const = default;
};

struct MusouEnemyState {
    MusouSpawn spawn;
    bool alive, active;
};

class MusouWaveRun {
public:
    static constexpr unsigned TicksPerSecond = 30;
    bool start(const MusouCampaign& campaign, unsigned mission);
    std::optional<MusouSpawn> step(std::span<const MusouEnemyState> enemies);
    bool enemyRemoved(std::span<const MusouEnemyState> enemies, bool bossDefeated = false);
    void playerDefeated();
    unsigned currentWave() const { return currentWave_; }
    unsigned timeLeft() const { return timeLeft_; }
    MusouResult result() const { return sourceResult_?sourceResult_->result():result_; }
    void sourceResultEvents(){sourceResult_.emplace();}
    const std::optional<SourceMusouResult>& sourceResult()const{return sourceResult_;}

private:
    void enqueue(std::uint16_t first, std::uint16_t count, MusouRepeat repeat,
                 std::span<const MusouEnemyState> enemies);

    const MusouCampaign* campaign_ = nullptr;
    unsigned firstWave_ = 0, endWave_ = 0, currentWave_ = 0, timeLeft_ = 0;
    unsigned repeatTimer_ = 0;
    int waveTimer_ = 0;
    MusouResult result_ = MusouResult::Idle;
    std::optional<SourceMusouResult> sourceResult_;
    std::deque<std::optional<MusouSpawn>> pending_;
};

} // namespace bvn
