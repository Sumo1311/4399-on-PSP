#pragma once
#include "quickjs.h"
#include <cmath>
#include <cstdint>

namespace devisual {
enum class DeathVisual { Dying, Corpse };
struct CorpseCensus {unsigned actors=0,dead=0,dying=0,unsupported=0;};
// Read-only presentation policy. Preserve living actors and the source death
// motion. Only settled bodies in the source corpse-flash phase disappear.
// Borrowed own data never runs getters, callbacks or source mutations.
template<class Emit>
CorpseCensus collect_corpses(JSContext* c,JSValueConst root,JSAtom game_key,
    JSAtom baddies_key,JSAtom health_key,JSAtom alive_key,JSAtom landed_key,
    JSAtom dead_count_key,JSAtom shadow_key,JSAtom uid_key,Emit emit){
    CorpseCensus stats;JSValue game,list;
    if(!JS_IsObject(root)||JS_TryOwnDataPropertyBorrowed(root,game_key,&game)!=1||!JS_IsObject(game))return stats;
    if(JS_TryOwnDataPropertyBorrowed(game,baddies_key,&list)!=1)return stats;
    uint32_t count=0;if(!JS_TryPlainArrayLength(list,&count)||count>4096){++stats.unsupported;return stats;}
    auto mark=[&](JSValueConst object,DeathVisual phase){JSValue value;uint32_t uid=0;
        if(JS_IsObject(object)&&JS_TryOwnDataPropertyBorrowed(object,uid_key,&value)==1&&JS_IsNumber(value)&&!JS_ToUint32(c,&uid,value)&&uid)emit(uid,phase);
    };
    for(uint32_t i=0;i<count;++i){
        JSValue actor,health,alive,landed,dead_count,shadow;
        if(!JS_TryPlainArrayValueBorrowed(list,i,&actor)||!JS_IsObject(actor)){++stats.unsupported;continue;}
        ++stats.actors;
        if(JS_TryOwnDataPropertyBorrowed(actor,health_key,&health)!=1||!JS_IsNumber(health)){++stats.unsupported;continue;}
        double hp=0;if(JS_ToFloat64(c,&hp,health)||!std::isfinite(hp)||!(hp<=0))continue;
        if(JS_TryOwnDataPropertyBorrowed(actor,alive_key,&alive)!=1||!JS_IsBool(alive)){++stats.unsupported;continue;}
        if(JS_ToBool(c,alive))continue;
        // Special bosses have their own death timeline. Without explicit own
        // grounded/countdown state, leave their original presentation intact.
        if(JS_TryOwnDataPropertyBorrowed(actor,landed_key,&landed)!=1||!JS_IsBool(landed)||
           JS_TryOwnDataPropertyBorrowed(actor,dead_count_key,&dead_count)!=1||!JS_IsNumber(dead_count)){++stats.unsupported;continue;}
        double remaining=0;if(JS_ToFloat64(c,&remaining,dead_count)||!std::isfinite(remaining)){++stats.unsupported;continue;}
        if(JS_ToBool(c,landed)&&remaining<60){
            ++stats.dead;mark(actor,DeathVisual::Corpse);
            if(JS_TryOwnDataPropertyBorrowed(actor,shadow_key,&shadow)==1)mark(shadow,DeathVisual::Corpse);
        }else{
            // CharacterFinal blinks as soon as deadCount<60, even during a
            // knockback. Keep only the actor's alpha steady until it settles.
            ++stats.dying;mark(actor,DeathVisual::Dying);
        }
    }
    return stats;
}
}
