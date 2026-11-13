// Adapted from 5DPLAY Game Studio's FighterAILogic.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fight_match.hpp"

namespace bvn {

class FighterCpu {
public:
    explicit FighterCpu(std::uint32_t seed = 1, unsigned level = 1, unsigned gameHz = 30)
        : state_(seed ? seed : 1), policy_(level, seed), gameHz_(gameHz) {}
    FighterControls step(const FightMatch& match, unsigned side);
    void update(const FightMatch& match, unsigned side);
    FighterControls controls() const;
    AiObservation observe(const FightMatch& match, unsigned side);
    std::uint32_t intents() const { return intents_; }
    unsigned decisions() const { return decisions_; }
    unsigned level()const{return policy_.level();}
    std::uint32_t policyRandomState() const { return policy_.randomState(); }
    unsigned movementFrames() const { return policy_.movementFrames(); }
    const AiObservation& lastObservation() const { return last_; }
    FighterControls stepBoss(const FighterMotion& self, const FighterMotion& opponent,
                             std::span<const FightObject> objects, unsigned partySize, bool animate);
    void updateSourceBoss(const FighterMotion& self,const FighterMotion& opponent,
                          std::span<const FightObject> objects,unsigned partySize);

private:
    AiObservation observeActors(const FighterMotion& self,const FighterMotion& opponent,
                                std::span<const FightObject> objects,unsigned ownId,unsigned partySize=0);
    FighterControls step(const FighterMotion& self, const FighterMotion& opponent,
                         std::span<const FightObject> objects, unsigned side, bool boss);
    bool chance(unsigned perThousand);

    std::uint32_t state_;
    unsigned moveTicks_ = 0;
    bool defending_ = false;
    FighterControls bossInput_{};
    FighterAiPolicy policy_;
    unsigned gameHz_, decisions_ = 0, observedAnimation_ = 0;
    std::uint32_t intents_ = 0;
    std::array<AiThreat, FightMatch::ObjectLimit> threats_{};
    AiObservation last_{};
};

} // namespace bvn
