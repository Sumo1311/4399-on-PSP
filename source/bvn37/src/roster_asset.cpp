// SPDX-License-Identifier: GPL-3.0-or-later
#include "roster_asset.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>

namespace bvn {
namespace {

struct Header {
    char magic[8];
    std::uint16_t rows, columns, fighters, reserved;
};

static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(Header) == 16 && sizeof(RosterFighter) == 56 && sizeof(RosterCell) == 24);

bool fighterPath(std::string_view file, bool assists) {
    if (file.empty()) return true;
    if (file.size() <= 4 || file.size() > 12 || !file.ends_with(assists ? ".BVS" : ".BVF")) return false;
    file.remove_suffix(4);
    return std::ranges::all_of(file, [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); });
}

} // namespace

bool RosterAsset::open(const char* path, bool assists) {
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
    Header header{};
    if (!file || std::fread(&header, sizeof(header), 1, file.get()) != 1 ||
        std::memcmp(header.magic, assists ? "BVNAST01" : "BVNSEL01", 8) || !header.rows || header.rows > 12 ||
        !header.columns || header.columns > 12 || !header.fighters || header.fighters > 256 || header.reserved) return false;

    RosterAsset next;
    next.rows_ = header.rows;
    next.columns_ = header.columns;
    next.fighters_.resize(header.fighters);
    next.cells_.resize(next.rows_ * next.columns_);
    if (std::fread(next.fighters_.data(), sizeof(RosterFighter), next.fighters_.size(), file.get()) != next.fighters_.size() ||
        std::fread(next.cells_.data(), sizeof(RosterCell), next.cells_.size(), file.get()) != next.cells_.size() ||
        std::fgetc(file.get()) != EOF || std::ferror(file.get())) return false;

    for (const auto& fighter : next.fighters_) {
        if (!std::memchr(fighter.file, 0, sizeof(fighter.file)) || !std::memchr(fighter.music, 0, sizeof(fighter.music)) ||
            !fighterPath(fighter.file, assists) || fighter.family >= header.fighters || fighter.form > 7 ||
            fighter.faction > 2 || fighter.musicChance > 100 || fighter.random > 3 ||
            (fighter.random && fighter.playable()) ||
            (fighter.flags && (assists || fighter.flags != RosterFighter::InternalForm || !fighter.playable()))) return false;
    }
    for (const auto& cell : next.cells_) {
        if (cell.fighter == MissingFighter) {
            if (cell.moreCount) return false;
            continue;
        }
        if (cell.x < 0 || cell.x > 750 || cell.y < 0 || cell.y > 550 ||
            cell.fighter >= header.fighters || cell.moreCount > cell.more.size() || next.fighter(cell.fighter).internal()) return false;
        for (const auto id : std::span(cell.more).first(cell.moreCount)) {
            if (id >= header.fighters || next.fighter(id).random || next.fighter(id).internal()) return false;
        }
    }
    *this = std::move(next);
    return true;
}

std::optional<unsigned> RosterAsset::firstCell(unsigned id) const {
    const auto found = std::ranges::find(cells_, id, &RosterCell::fighter);
    if (found == cells_.end()) return {};
    return static_cast<unsigned>(found - cells_.begin());
}

unsigned RosterAsset::initialAlternate(unsigned cell) const {
    const auto& entry = cells_[cell];
    if (entry.fighter == MissingFighter || fighter(entry.fighter).playable()) return 0;
    for (unsigned i = 1; i <= entry.moreCount; ++i) {
        if (fighter(entry.choice(i)).playable()) return i;
    }
    return 0;
}

unsigned RosterAsset::move(unsigned index, int horizontal, int vertical) const {
    const int rows = static_cast<int>(rows_), columns = static_cast<int>(columns_);
    const int row = static_cast<int>(index / columns_), column = static_cast<int>(index % columns_);
    if (horizontal) {
        for (int i = 1; i < columns; ++i) {
            const auto cell = row * columns + (column + horizontal * i + columns) % columns;
            if (cells_[cell].fighter != MissingFighter) return cell;
        }
    } else if (vertical) {
        for (int i = 1; i < rows; ++i) {
            const auto next = (row + vertical * i + rows) % rows * columns;
            if (cells_[next + column].fighter != MissingFighter) return next + column;
            for (int distance = 1; distance < columns; ++distance) {
                const int right = column + distance, left = column - distance;
                if (right < columns && cells_[next + right].fighter != MissingFighter) return next + right;
                if (left >= 0 && cells_[next + left].fighter != MissingFighter) return next + left;
            }
        }
    }
    return index;
}

std::optional<unsigned> RosterAsset::resolve(unsigned id, unsigned seed, std::span<const unsigned> excluded) const {
    const auto& selected = fighter(id);
    const auto allowed = [&](unsigned index) {
        return std::ranges::find(excluded, index) == excluded.end();
    };
    if (!selected.random) return selected.selectable() && allowed(id) ? std::optional(id) : std::nullopt;
    const auto eligible = [&](const RosterFighter& candidate) {
        return candidate.selectable() && allowed(&candidate - fighters_.data()) &&
            (selected.random == 1 || candidate.faction == selected.random - 2);
    };
    const auto count = std::ranges::count_if(fighters_, eligible);
    if (!count) return {};
    auto index = seed % count;
    for (unsigned i = 0; i < fighters_.size(); ++i) {
        if (eligible(fighters_[i]) && index-- == 0) return i;
    }
    return {};
}

std::vector<unsigned> RosterAsset::forms(unsigned id) const {
    const auto& selected = fighter(id);
    std::vector<unsigned> result;
    if (!selected.playable()) return result;
    for (unsigned i = 0; i < fighters_.size(); ++i) {
        const auto& form = fighters_[i];
        if (!form.playable() || form.family != selected.family) continue;
        if (form.form >= result.size()) result.resize(form.form + 1, MissingFighter);
        if (result[form.form] == MissingFighter) result[form.form] = i;
    }
    result[selected.form] = id;
    return result;
}

} // namespace bvn
