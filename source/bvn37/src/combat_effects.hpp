// Source EffectModel, EffectHitHandler and EffectVO value fields.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace bvn {
enum class EffectBlend : std::uint8_t { Normal, Add, Lighten, Hardlight, Screen };
struct CombatEffect {
    std::string_view id, className, sound, specialEffect;
    EffectBlend blend;
    unsigned freezeMs;
    bool hasShine;
    std::uint32_t shineColor;
    double shineAlpha;
    double shakeX, shakeY, shakePower;
    unsigned shakeTimeMs;
    double slowRate;
    unsigned slowTimeMs;
    bool randomRotation, followDirection, special, buff, steel;
    bool hasTargetTint;
    std::array<int,3> targetTint;
};

std::span<const CombatEffect> combatEffects();
const CombatEffect* combatEffect(std::string_view id);
const CombatEffect* combatHitEffect(int type, bool musouEnemy = false);
const CombatEffect* combatDefenseEffect(int type, int defenseType, bool musouEnemy = false);
const CombatEffect* combatSteelEffect(int type);
} // namespace bvn
