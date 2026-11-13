// Adapted from 5DPLAY's EffectModel and EffectHitHandler.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_effects.hpp"
#include "combat_effect_data.hpp"
#include <algorithm>
#include <cstdio>

namespace bvn {
std::span<const CombatEffect> combatEffects() { return CombatEffectData; }
const CombatEffect* combatEffect(std::string_view id) {
    const auto result=std::ranges::lower_bound(CombatEffectData,id,{},&CombatEffect::id);
    return result!=std::end(CombatEffectData)&&result->id==id?result:nullptr;
}
const CombatEffect* combatHitEffect(int type,bool musouEnemy) {
    char key[40];std::snprintf(key,sizeof(key),musouEnemy?"hit_enemy_%d":"hit_%d",type);
    return combatEffect(key);
}
const CombatEffect* combatDefenseEffect(int type,int defenseType,bool musouEnemy) {
    if(defenseType==1) { if(type==1)type=2;else if(type==6)type=3; }
    char key[40];std::snprintf(key,sizeof(key),musouEnemy?"defense_enemy_%d":"defense_%d",type);
    const auto* result=combatEffect(key);
    return result||musouEnemy?result:combatEffect("defense_5");
}
const CombatEffect* combatSteelEffect(int type) {
    if(!type)return nullptr;
    if(type==1||type==6)return combatEffect("steel_hit_kan");
    if(type==2||type==3)return combatEffect("steel_hit_qdj");
    return combatEffect("steel_hit_mfdj");
}
} // namespace bvn
