#pragma once
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_sprite.h"

namespace dnm {
inline gameswf::sprite_instance* end_clip(gameswf::root* root) {
    if(root->get_current_frame()!=4 && root->get_current_frame()!=5)return nullptr;
    gameswf::as_value end;
    if(!root->get_root_movie()->get_member(root->get_current_frame()==4?"end":"over",&end))return nullptr;
    return gameswf::cast_to<gameswf::sprite_instance>(end.to_object());
}
inline gameswf::character* find_button(gameswf::character* ch,int id,unsigned depth=0) {
    if(!ch || depth>16 || !ch->get_visible())return nullptr;
    if(ch->get_id()==id)return ch;
    auto clip=gameswf::cast_to<gameswf::sprite_instance>(ch);if(!clip)return nullptr;
    for(int i=0;i<clip->m_display_list.size();i++)
        if(auto button=find_button(clip->m_display_list.get_character(i),id,depth+1))return button;
    return nullptr;
}
inline gameswf::character* continue_button(gameswf::root* root) {
    auto clip=end_clip(root);
    if(!clip || clip->get_play_state()!=gameswf::sprite_instance::STOP)return nullptr;
    return find_button(clip,root->get_current_frame()==4?1983:2042);
}
inline bool activate_continue(gameswf::root* root) {
    if(auto button=continue_button(root))return button->on_event(gameswf::event_id::PRESS);
    return false;
}
}
