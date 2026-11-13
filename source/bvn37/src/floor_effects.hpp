// Adapted from 5DPLAY's FighterEffectCtrl.hitFloor and EffectCtrl.hitFloorEffect.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fighter_motion.hpp"
#include <algorithm>

namespace bvn {
enum class FloorImpact : unsigned { Bounce, Normal, Heavy };

inline void appendFloorImpact(FighterEffects& requests, FloorImpact type, Point origin,
                              double shake, bool musouEnemy) {
    if (musouEnemy && shake > 1) shake = 1;
    if (shake > 0) {
        requests.shakeX = 0;
        requests.shakeY = float(shake * 2);
        requests.shakeTimeMs = 0;
        requests.shakeRequested = true;
    }
    const auto emit = [&](std::string_view id) {
        if (const auto* effect = combatEffect(id))
            requests.contactEffects.push_back({effect, ContactEffectKind::Hurt, origin, 1, {}});
    };
    switch (type) {
    case FloorImpact::Bounce: emit("hit_floor"); break;
    case FloorImpact::Normal: emit("hit_floor_low"); break;
    case FloorImpact::Heavy: emit("hit_floor_heavy"); emit("hit_floor_yan"); break;
    }
}

// The original FighterMC computes this at HURT_FLY_FALL, after two fall frames.
inline double appendLandingImpact(FighterEffects& requests, bool heavy, Point origin,
                                  double fallDistance, double hitY, bool musouEnemy) {
    if (heavy) {
        appendFloorImpact(requests, FloorImpact::Heavy, origin,
                          std::clamp(fallDistance * .04 * (1 + hitY * .1), 2.0, 5.0), musouEnemy);
        return 0;
    }
    const double bounce = std::clamp(fallDistance * .04, 3.0, 8.0);
    appendFloorImpact(requests, FloorImpact::Bounce, origin, std::clamp(bounce, .5, 3.0), musouEnemy);
    return -bounce;
}
} // namespace bvn
