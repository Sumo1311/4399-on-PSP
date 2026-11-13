// Adapted from 5DPLAY Game Studio's MusouPlayerData and MusouFighterModel.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "musou_battle.hpp"
#include "roster_asset.hpp"

namespace bvn {

struct MusouRecruit {
    char id[32];
    std::uint32_t price;
    std::uint16_t roster, reserved;
};

class MusouParty {
public:
    bool open(const char* path, const RosterAsset& roster);
    std::span<const MusouRecruit> recruits() const { return recruits_; }
    const std::array<unsigned, 3>& team() const { return team_; }
    const MusouMember* member(unsigned recruit) const;
    bool available(unsigned recruit) const;
    bool acquire(unsigned recruit);
    bool replace(unsigned slot, unsigned recruit);
    bool setLeader(unsigned slot);
    std::array<MusouMember, 3> battleMembers() const;
    void storeBattleMembers(const std::array<MusouMember, 3>& members);

    bool loadSave(const char* path, const MusouCampaign& campaign, MusouProgress& progress,
                  unsigned slot, std::uint32_t& serial, unsigned& area);
    bool writeSave(const char* path, const MusouCampaign& campaign, const MusouProgress& progress,
                   unsigned money, std::uint32_t serial, unsigned area) const;

private:
    std::optional<unsigned> find(std::string_view id) const;

    const RosterAsset* roster_ = nullptr;
    std::vector<MusouRecruit> recruits_;
    std::vector<MusouMember> owned_;
    std::array<unsigned, 3> team_{};
};

} // namespace bvn
