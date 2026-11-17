#pragma once
#include "gameswf/gameswf_sprite.h"
#include <typeinfo>
#include <cstdlib>
namespace dnm {
#ifndef PSP
struct FrameEventStats {unsigned long long checked=0,absent=0;};
inline FrameEventStats frame_event_stats;
#endif
inline bool event_lookup_gate_enabled(){
#ifndef PSP
    static const bool enabled=std::getenv("DNM_DISABLE_EVENT_LOOKUP_GATE")==nullptr&&std::getenv("DNM_DISABLE_FRAME_EVENT_GATE")==nullptr;
    return enabled;
#else
    return true;
#endif
}
// Prove a missing event handler from live data, without retaining any
// negative result. Present values (including undefined/properties), children,
// builtins, unknown virtual lookups and long/cyclic chains keep old dispatch.
inline bool event_handler_absent(gameswf::sprite_instance* clip,const gameswf::event_id& id){
    using namespace gameswf;
#ifndef PSP
    ++frame_event_stats.checked;
#endif
    if(typeid(*clip)!=typeid(sprite_instance)||id.m_id<=event_id::INVALID||id.m_id>=event_id::EVENT_COUNT)return false;
    struct EventName {bool initialized=false;tu_stringi name;size_t hash=0;bool ordinary=false;};
    static EventName names[event_id::EVENT_COUNT];
    auto& key=names[id.m_id];
    if(!key.initialized){key.name=id.get_function_name();key.hash=stringi_hash_functor<tu_stringi>()(key.name);key.ordinary=get_standard_member(key.name)==M_INVALID_MEMBER;key.initialized=true;}
    const auto& name=key.name;const auto hash=key.hash;
    if(!key.ordinary||clip->m_members.find_prehashed(name,hash)!=clip->m_members.end())return false;
    if(clip->m_display_list.get_character_by_name_i(name))return false;
    auto* proto=clip->get_proto();
    for(unsigned depth=0;proto&&depth<8;++depth){
        if(typeid(*proto)!=typeid(as_object))return false;
        if(proto->m_members.find_prehashed(name,hash)!=proto->m_members.end())return false;
        proto=proto->get_proto();
    }
    if(proto||has_builtin_prehashed(BUILTIN_SPRITE_METHOD,name,hash)||has_builtin_prehashed(BUILTIN_OBJECT_METHOD,name,hash))return false;
#ifndef PSP
    ++frame_event_stats.absent;
#endif
    return true;
}
inline bool frame_event_absent(gameswf::sprite_instance* clip){return event_handler_absent(clip,gameswf::event_id::ENTER_FRAME);}
}
