// Adapted from 5DPLAY Game Studio's Musou model and controller.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "musou_campaign.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace bvn {
namespace {

struct Header {
    char magic[8], world[16];
    std::uint16_t areas, missions, waves, enemies;
};

static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(Header) == 32 && sizeof(MusouArea) == 36 && sizeof(MusouMission) == 40 &&
              sizeof(MusouWave) == 14 && sizeof(MusouEnemy) == 44);

template<std::size_t N>
bool hasLabel(const char (&label)[N]) {
    return label[0] && std::memchr(label, 0, N);
}

template<class T>
bool readTable(FILE* file, std::vector<T>& out, unsigned count) {
    out.resize(count);
    return std::fread(out.data(), sizeof(T), count, file) == count;
}

bool takeRange(unsigned& next, unsigned first, unsigned count, unsigned size) {
    if (first != next || count > size - next) return false;
    next += count;
    return true;
}

constexpr unsigned NoMission = std::numeric_limits<unsigned>::max();

} // namespace

bool MusouCampaign::open(const char* path) {
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    Header header{};
    if (!file || std::fread(&header, sizeof(header), 1, file.get()) != 1 ||
        std::memcmp(header.magic, "BVNMUS01", sizeof(header.magic)) || !hasLabel(header.world) ||
        !header.areas || header.areas > 32 || !header.missions || !header.waves || !header.enemies) return false;

    MusouCampaign next;
    std::memcpy(next.world_, header.world, sizeof(header.world));
    if (!readTable(file.get(), next.areas_, header.areas) || !readTable(file.get(), next.missions_, header.missions) ||
        !readTable(file.get(), next.waves_, header.waves) || !readTable(file.get(), next.enemies_, header.enemies) ||
        std::fgetc(file.get()) != EOF || std::ferror(file.get())) return false;

    unsigned nextMission = 0, nextWave = 0, nextEnemy = 0;
    for (unsigned i = 0; i < header.areas; ++i) {
        const auto& area = next.areas_[i];
        const auto prior = (std::uint32_t{1} << i) - 1;
        if (!hasLabel(area.id) || !hasLabel(area.name) || bool(area.prerequisites) != bool(i) ||
            (area.prerequisites & ~prior) || !area.missionCount ||
            !takeRange(nextMission, area.firstMission, area.missionCount, header.missions)) return false;
        for (const auto& earlier : next.areas().first(i)) {
            if (std::string_view(earlier.id) == area.id) return false;
        }
        const auto missions = next.missions().subspan(area.firstMission, area.missionCount);
        for (unsigned j = 0; j < missions.size(); ++j) {
            const auto& mission = missions[j];
            if (!hasLabel(mission.id) || !hasLabel(mission.map) || !mission.seconds || !mission.enemyLevel ||
                !mission.waveCount || !takeRange(nextWave, mission.firstWave, mission.waveCount, header.waves)) return false;
            for (const auto& earlier : missions.first(j)) {
                if (std::string_view(earlier.id) == mission.id) return false;
            }
        }
    }
    for (const auto& wave : next.waves_) {
        if (!wave.enemyCount || wave.repeat > MusouRepeat::ReplaceDead ||
            bool(wave.repeatCount) != (wave.repeat != MusouRepeat::None) ||
            (wave.repeat == MusouRepeat::None && wave.repeatHold) ||
            !takeRange(nextEnemy, wave.firstEnemy, wave.enemyCount, header.enemies) ||
            !takeRange(nextEnemy, wave.firstRepeat, wave.repeatCount, header.enemies)) return false;
    }
    for (const auto& enemy : next.enemies_) {
        if (!hasLabel(enemy.fighter) || !enemy.amount() || enemy.hp > 0x7fffffff ||
            !enemy.experience || !enemy.money) return false;
    }
    if (nextMission != header.missions || nextWave != header.waves || nextEnemy != header.enemies) return false;
    *this = std::move(next);
    return true;
}

MusouProgress::MusouProgress(const MusouCampaign& campaign)
    : campaign_(campaign), stars_(campaign.missions().size()), lastPassed_(campaign.areas().size(), NoMission) {}

bool MusouProgress::areaOpen(unsigned area) const {
    if (area >= campaign_.areas().size()) return false;
    auto parents = campaign_.areas()[area].prerequisites;
    while (parents) {
        const auto index = std::countr_zero(parents);
        const auto& parent = campaign_.areas()[index];
        const auto missions = stars().subspan(parent.firstMission, parent.missionCount);
        const auto passed = std::ranges::count(missions, std::uint8_t{1});
        if (passed * 5 <= parent.missionCount * 3) return false;
        parents &= parents - 1;
    }
    return true;
}

std::optional<unsigned> MusouProgress::nextMission(unsigned area) const {
    if (!areaOpen(area)) return {};
    const auto& entry = campaign_.areas()[area];
    const auto last = lastPassed_[area];
    if (last == NoMission) return entry.firstMission;
    return std::min(last + 1, unsigned(entry.firstMission + entry.missionCount - 1));
}

bool MusouProgress::passMission(unsigned area, unsigned mission) {
    if (!areaOpen(area)) return false;
    const auto& entry = campaign_.areas()[area];
    if (mission < entry.firstMission || mission >= unsigned(entry.firstMission + entry.missionCount)) return false;
    const bool firstClear = !stars_[mission];
    const unsigned reward = firstClear ? 2000 : 500;
    earn(reward);
    if (firstClear) lastPassed_[area] = mission;
    stars_[mission] = 1;
    return true;
}

bool MusouProgress::restore(unsigned money, std::span<const std::uint8_t> stars) {
    if (stars.size() != stars_.size() || std::ranges::any_of(stars, [](auto star) { return star > 1; })) return false;
    money_ = money;
    std::ranges::copy(stars, stars_.begin());
    std::ranges::fill(lastPassed_, NoMission);
    for (unsigned i = 0; i < campaign_.areas().size(); ++i) {
        const auto& area = campaign_.areas()[i];
        for (unsigned mission = area.firstMission; mission < unsigned(area.firstMission + area.missionCount); ++mission) {
            if (stars_[mission]) lastPassed_[i] = mission;
        }
    }
    return true;
}

void MusouProgress::earn(unsigned amount) {
    money_ = std::min(money_, std::numeric_limits<unsigned>::max() - amount) + amount;
}

bool MusouProgress::spend(unsigned amount) {
    if (money_ < amount) return false;
    money_ -= amount;
    return true;
}

bool MusouWaveRun::start(const MusouCampaign& campaign, unsigned mission) {
    if (mission >= campaign.missions().size()) return false;
    const auto& entry = campaign.missions()[mission];
    campaign_ = &campaign;
    firstWave_ = entry.firstWave;
    endWave_ = firstWave_ + entry.waveCount;
    currentWave_ = repeatTimer_ = 0;
    waveTimer_ = 0;
    timeLeft_ = entry.seconds * TicksPerSecond;
    result_ = MusouResult::Playing;
    sourceResult_.reset();
    pending_.clear();
    return true;
}

void MusouWaveRun::enqueue(std::uint16_t first, std::uint16_t count, MusouRepeat repeat,
                           std::span<const MusouEnemyState> enemies) {
    const auto before = pending_.size();
    for (unsigned i = first; i < unsigned(first) + count; ++i) {
        for (unsigned member = 0; member < campaign_->enemies()[i].amount(); ++member) {
            const MusouSpawn spawn{std::uint16_t(i), std::uint16_t(member), repeat != MusouRepeat::None};
            if (repeat == MusouRepeat::ReplaceDead) {
                const auto match = std::ranges::find(enemies, spawn, &MusouEnemyState::spawn);
                if (match != enemies.end() && match->alive && match->active) continue;
            }
            pending_.push_back(spawn);
        }
    }
    // The Flash creator spends a tick removing each exhausted queue.
    if (pending_.size() > before) pending_.push_back(std::nullopt);
}

std::optional<MusouSpawn> MusouWaveRun::step(std::span<const MusouEnemyState> enemies) {
    if (result() != MusouResult::Playing) return {};
    if (firstWave_ + currentWave_ < endWave_ && waveTimer_-- < 0) {
        const auto& wave = campaign_->waves()[firstWave_ + currentWave_++];
        waveTimer_ = wave.hold * TicksPerSecond;
        repeatTimer_ = 0;
        enqueue(wave.firstEnemy, wave.enemyCount, MusouRepeat::None, {});
    }
    if (currentWave_) {
        const auto& wave = campaign_->waves()[firstWave_ + currentWave_ - 1];
        if (repeatTimer_) --repeatTimer_;
        else if (wave.repeat != MusouRepeat::None) {
            repeatTimer_ = wave.repeatHold * TicksPerSecond;
            enqueue(wave.firstRepeat, wave.repeatCount, wave.repeat, enemies);
        }
    }
    std::optional<MusouSpawn> spawn;
    if (!pending_.empty()) {
        spawn = pending_.front();
        pending_.pop_front();
    }
    if (!--timeLeft_) {
        if(sourceResult_)sourceResult_->timeOver();
        else result_ = MusouResult::TimeOver;
    }
    return spawn;
}

bool MusouWaveRun::enemyRemoved(std::span<const MusouEnemyState> enemies, bool bossDefeated) {
    if (!sourceResult_ && result_ != MusouResult::Playing) return false;
    if (firstWave_ + currentWave_ < endWave_) {
        if (std::ranges::none_of(enemies, [](const auto& enemy) { return enemy.alive && !enemy.spawn.repeat; })) {
            waveTimer_ = std::min(waveTimer_, 2 * int(TicksPerSecond));
        }
        return false;
    }
    if (std::ranges::none_of(enemies, [&](const auto& enemy) {
        return enemy.alive && (bossDefeated ? campaign_->enemies()[enemy.spawn.enemy].boss() : !enemy.spawn.repeat);
    })) {
        if(sourceResult_)sourceResult_->complete();
        else result_ = MusouResult::Won;
        return true;
    }
    return false;
}

void MusouWaveRun::playerDefeated() {
    if(sourceResult_)sourceResult_->defeat();
    else if (result_ == MusouResult::Playing) result_ = MusouResult::Defeated;
}

} // namespace bvn
