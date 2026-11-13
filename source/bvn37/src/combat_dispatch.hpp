// Adapted from 5DPLAY Game Studio's EffectCtrl and EffectHitHandler.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fighter_motion.hpp"
#include <cmath>

namespace bvn {

// All consequences belong to an accepted EffectView, including the three-view
// limit for one HitVO. Keep that ordering identical in versus and Musou.
template<class Freeze, class Slow, class Sound>
bool dispatchCombatEffect(FightEffects& effects, const ContactEffectRequest& request,
                          Freeze freeze, Slow slow, Sound sound) {
    if (!request.effect) return false;
    const auto& effect = *request.effect;
    if (!effects.emit(effect, request.origin, request.facing, request.identity)) return false;
    if (!effect.sound.empty()) sound(effect.sound);
    if (effect.freezeMs) freeze(effect.freezeMs);
    FighterEffects response{};
    response.shakeX = request.shakeX();
    response.shakeY = request.shakeY();
    response.shakeRequested = effect.shakePower || effect.shakeX || effect.shakeY || effect.shakeTimeMs;
    response.shakeTimeMs = effect.shakeTimeMs;
    if (effect.hasShine) response.addShine(effect.shineColor, unsigned(std::lround(effect.shineAlpha * 100)));
    if (effect.slowTimeMs) slow(effect.slowRate, effect.slowTimeMs);
    effects.play(response);
    if (request.target) {
        effects.setLastTarget(request.target);
        if (!effect.specialEffect.empty())
            if (const auto* extra = combatEffect(effect.specialEffect)) effects.special(*extra, request.target);
    }
    return true;
}

// EffectCtrl's counter and GameCtrl's paused flag are separate: the boss KO
// disables the counter, then resumes gameplay without clearing the old counter.
class CombatFreeze {
public:
    bool request(unsigned milliseconds, unsigned gameHz) {
        const unsigned frames = milliseconds * gameHz / 1000;
        if (!enabled_ || !frames || remaining_ > frames) return false;
        remaining_ = frames;
        paused_ = true;
        return true;
    }
    bool step(unsigned gameHz,unsigned deliveryHz=LogicHz) {
        phase_ += gameHz;
        if (phase_ < deliveryHz) return false;
        phase_ -= deliveryHz;
        if (!enabled_ || !remaining_ || --remaining_) return false;
        paused_ = false;
        return true;
    }
    void disable() { enabled_ = false; }
    void resume() { enabled_ = true; paused_ = false; }
    bool paused() const { return enabled_ && paused_; }
    bool nextPaused(unsigned gameHz,unsigned deliveryHz=LogicHz) const { return paused() && remaining_>(phase_+gameHz>=deliveryHz?1U:0U); }
    bool enabled() const { return enabled_; }
    unsigned remaining() const { return remaining_; }
private:
    unsigned remaining_ = 0, phase_ = 0;
    bool enabled_ = true, paused_ = false;
};

} // namespace bvn
