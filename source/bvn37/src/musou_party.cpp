// Adapted from 5DPLAY Game Studio's MusouPlayerData and MusouFighterModel.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "musou_party.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <zlib.h>

namespace bvn {
namespace {

using FighterId = std::array<char, 32>;
constexpr std::array<std::string_view, 3> Starters{"ichigo", "naruto", "sakura"};
constexpr std::array LegacyMagic{'B','V','N','M','S','V','0','1'};
constexpr std::array SaveMagic{'B','V','N','M','S','V','0','2'};
struct ShopHeader {
    char magic[8];
    std::uint16_t count, reserved;
};
struct SaveHeader {
    std::array<char, 8> magic;
    std::uint32_t serial, layout, money, area;
};
static_assert(sizeof(ShopHeader) == 12 && sizeof(MusouRecruit) == 40);
static_assert(sizeof(SaveHeader) == 24 && sizeof(MusouMember) == 8);

bool validId(const char* id) {
    const auto* end = static_cast<const char*>(std::memchr(id, 0, sizeof(FighterId)));
    if (!end || end == id) return false;
    return std::all_of(id, end, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool validMember(const MusouMember& member) {
    if (!member.level || member.level > 80) return false;
    // Flash grants one level per reward, so a large reward can leave enough EXP for a later level.
    return member.level < 80 || member.experience <= MusouMember::levelUpExp(member.level);
}

std::uint32_t missionLayout(const MusouCampaign& campaign) {
    const auto world = campaign.world();
    auto crc = crc32(0, reinterpret_cast<const Bytef*>(world.data()), world.size());
    for (const auto& area : campaign.areas()) {
        crc = crc32(crc, reinterpret_cast<const Bytef*>(area.id), sizeof(area.id));
        for (const auto& mission : campaign.missions().subspan(area.firstMission, area.missionCount))
            crc = crc32(crc, reinterpret_cast<const Bytef*>(mission.id), sizeof(mission.id));
    }
    return crc;
}

} // namespace

bool MusouParty::open(const char* path, const RosterAsset& roster) {
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    ShopHeader header{};
    if (!file || std::fread(&header, sizeof(header), 1, file.get()) != 1 ||
        std::memcmp(header.magic, "BVNSHP01", 8) || header.reserved ||
        header.count < Starters.size() || header.count > roster.size()) return false;

    MusouParty next;
    next.roster_ = &roster;
    next.recruits_.reserve(header.count);
    for (unsigned i = 0; i < header.count; ++i) {
        MusouRecruit recruit{};
        if (std::fread(&recruit, sizeof(recruit), 1, file.get()) != 1 || !validId(recruit.id) ||
            !recruit.price || recruit.reserved || recruit.roster >= roster.size() ||
            roster.fighter(recruit.roster).internal() || roster.fighter(recruit.roster).random ||
            next.find(recruit.id) ||
            std::ranges::find(next.recruits_, recruit.roster, &MusouRecruit::roster) != next.recruits_.end())
            return false;
        next.recruits_.push_back(recruit);
    }
    if (std::fgetc(file.get()) != EOF || std::ferror(file.get())) return false;
    next.owned_.assign(next.recruits_.size(), {0, 0});
    for (unsigned i = 0; i < Starters.size(); ++i) {
        const auto recruit = next.find(Starters[i]);
        if (!recruit || !next.acquire(*recruit)) return false;
        next.team_[i] = *recruit;
    }
    *this = std::move(next);
    return true;
}

std::optional<unsigned> MusouParty::find(std::string_view id) const {
    const auto it = std::ranges::find_if(recruits_, [&](const auto& entry) { return id == entry.id; });
    if (it == recruits_.end()) return {};
    return unsigned(it - recruits_.begin());
}

const MusouMember* MusouParty::member(unsigned recruit) const {
    return recruit < owned_.size() && owned_[recruit].level ? &owned_[recruit] : nullptr;
}

bool MusouParty::available(unsigned recruit) const {
    return recruit < recruits_.size() && roster_->fighter(recruits_[recruit].roster).selectable();
}

bool MusouParty::acquire(unsigned recruit) {
    if (!available(recruit) || member(recruit)) return false;
    owned_[recruit] = MusouMember{};
    return true;
}

bool MusouParty::replace(unsigned slot, unsigned recruit) {
    if (slot >= team_.size() || !member(recruit) || !available(recruit) ||
        std::ranges::find(team_, recruit) != team_.end()) return false;
    team_[slot] = recruit;
    return true;
}

bool MusouParty::setLeader(unsigned slot) {
    if (!slot || slot >= team_.size()) return false;
    std::ranges::rotate(team_, team_.begin() + slot);
    return true;
}

std::array<MusouMember, 3> MusouParty::battleMembers() const {
    return {owned_[team_[0]], owned_[team_[1]], owned_[team_[2]]};
}

void MusouParty::storeBattleMembers(const std::array<MusouMember, 3>& members) {
    for (unsigned i = 0; i < team_.size(); ++i) owned_[team_[i]] = members[i];
}

bool MusouParty::loadSave(const char* path, const MusouCampaign& campaign, MusouProgress& progress,
                         unsigned slot, std::uint32_t& serial, unsigned& area) {
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    if (!file) return false;
    auto crc = crc32(0, nullptr, 0);
    const auto read = [&](void* data, std::size_t bytes) {
        if (std::fread(data, 1, bytes, file.get()) != bytes) return false;
        crc = crc32(crc, static_cast<const Bytef*>(data), bytes);
        return true;
    };
    SaveHeader header{};
    if (!read(&header, sizeof(header)) || header.layout != missionLayout(campaign) ||
        header.serial <= serial || (header.serial & 1) != slot || header.area >= campaign.areas().size())
        return false;

    auto next = *this;
    next.owned_.assign(recruits_.size(), {0, 0});
    if (header.magic == LegacyMagic) {
        std::array<MusouMember, 3> members{};
        if (!read(members.data(), sizeof(members)) || !std::ranges::all_of(members, validMember)) return false;
        for (unsigned i = 0; i < Starters.size(); ++i) next.team_[i] = *find(Starters[i]);
        next.storeBattleMembers(members);
    } else if (header.magic == SaveMagic) {
        std::array<FighterId, 3> team{};
        std::uint32_t count = 0;
        if (!read(team.data(), sizeof(team)) || !read(&count, sizeof(count)) ||
            count < Starters.size() || count > recruits_.size()) return false;
        for (unsigned i = 0; i < count; ++i) {
            FighterId id{};
            MusouMember member;
            if (!read(id.data(), id.size()) || !validId(id.data()) ||
                !read(&member, sizeof(member)) || !validMember(member)) return false;
            const auto recruit = find(id.data());
            if (!recruit || next.member(*recruit)) return false;
            next.owned_[*recruit] = member;
        }
        for (const auto id : Starters) if (!next.member(*find(id))) return false;
        for (unsigned i = 0; i < team.size(); ++i) {
            if (!validId(team[i].data())) return false;
            const auto recruit = find(team[i].data());
            const auto previous = std::span(next.team_).first(i);
            if (!recruit || !next.member(*recruit) || !next.available(*recruit) ||
                std::ranges::find(previous, *recruit) != previous.end()) return false;
            next.team_[i] = *recruit;
        }
    } else {
        return false;
    }
    std::vector<std::uint8_t> stars(campaign.missions().size());
    std::uint32_t checksum = 0;
    if (!read(stars.data(), stars.size()) || std::fread(&checksum, sizeof(checksum), 1, file.get()) != 1 ||
        std::fgetc(file.get()) != EOF || std::ferror(file.get()) || checksum != crc ||
        !progress.restore(header.money, stars)) return false;

    *this = std::move(next);
    serial = header.serial;
    area = header.area;
    return true;
}

bool MusouParty::writeSave(const char* path, const MusouCampaign& campaign, const MusouProgress& progress,
                          unsigned money, std::uint32_t serial, unsigned area) const {
    auto* file = std::fopen(path, "wb");
    if (!file) return false;
    auto crc = crc32(0, nullptr, 0);
    const auto write = [&](const void* data, std::size_t bytes) {
        crc = crc32(crc, static_cast<const Bytef*>(data), bytes);
        return std::fwrite(data, 1, bytes, file) == bytes;
    };
    const SaveHeader header{SaveMagic, serial, missionLayout(campaign), money, area};
    const std::uint32_t count = std::ranges::count_if(owned_, [](const auto& entry) { return entry.level != 0; });
    bool ok = write(&header, sizeof(header));
    for (const auto i : team_) ok = ok && write(recruits_[i].id, sizeof(FighterId));
    ok = ok && write(&count, sizeof(count));
    for (unsigned i = 0; i < owned_.size(); ++i) {
        if (!member(i)) continue;
        ok = ok && write(recruits_[i].id, sizeof(FighterId)) && write(&owned_[i], sizeof(MusouMember));
    }
    const auto stars = progress.stars();
    const auto checksum = std::uint32_t(crc32(crc, stars.data(), stars.size()));
    ok = ok && std::fwrite(stars.data(), 1, stars.size(), file) == stars.size() &&
         std::fwrite(&checksum, sizeof(checksum), 1, file) == 1;
    return std::fclose(file) == 0 && ok;
}

} // namespace bvn
