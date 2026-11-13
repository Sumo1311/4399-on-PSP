// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace bvn {

inline constexpr std::uint16_t MissingFighter = 0xffff;

struct RosterFighter {
    static constexpr std::uint16_t InternalForm = 1;
    char file[16];
    char music[32];
    std::uint16_t family;
    std::uint8_t form, faction, musicChance, random;
    std::uint16_t flags;

    bool playable() const { return file[0] != '\0'; }
    bool internal() const { return flags & InternalForm; }
    bool selectable() const { return playable() && !internal(); }
};

struct RosterCell {
    std::int16_t x, y;
    std::uint16_t fighter, moreCount;
    std::array<std::uint16_t, 8> more;

    unsigned choice(unsigned alternate) const { return alternate ? more[alternate - 1] : fighter; }
};

class RosterAsset {
public:
    bool open(const char* path, bool assists = false);
    std::size_t size() const { return fighters_.size(); }
    const RosterFighter& fighter(unsigned id) const { return fighters_[id]; }
    std::span<const RosterCell> cells() const { return cells_; }
    std::optional<unsigned> firstCell(unsigned id) const;
    unsigned initialAlternate(unsigned cell) const;
    unsigned move(unsigned cell, int horizontal, int vertical) const;
    std::optional<unsigned> resolve(unsigned id, unsigned seed, std::span<const unsigned> excluded = {}) const;
    std::vector<unsigned> forms(unsigned id) const;

private:
    unsigned rows_ = 0, columns_ = 0;
    std::vector<RosterFighter> fighters_;
    std::vector<RosterCell> cells_;
};

} // namespace bvn
