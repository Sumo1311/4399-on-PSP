#pragma once
#include "gameswf/gameswf_sprite.h"
#include "gameswf/gameswf_function.h"
#include <cstdio>
#include <cmath>
#include <cstdint>

namespace dnm {
inline gameswf::as_value own(gameswf::as_object* o,const char* key){
    if(o){auto i=o->m_members.find(key);if(i!=o->m_members.end()&&!i->second.is_property())return i->second;}
    return gameswf::as_value();
}
// The source has exactly two control-locking intros. If their body animation
// is interrupted/removed, its final pause=false action can never run. Release
// only this orphaned game flag; preserve native pause, HP, hits and actor state.
class IntroPauseGuard {
    weak_ptr<gameswf::sprite_instance> owner;
    unsigned age=0,orphan=0;
public:
    static bool intro_body(gameswf::sprite_instance* body){
        if(!body||(body->get_id()!=688&&body->get_id()!=620))return false;
        int first=-1,last=body->get_id()==688?67:84;
        return body->m_def->get_labeled_frame("intro",&first)&&body->get_play_state()==gameswf::character::PLAY&&
            body->get_current_frame()>=first&&body->get_current_frame()<=last;
    }
    static bool active_intro(gameswf::sprite_instance* game){
        if(!game)return false;
        for(int i=0;i<game->m_display_list.size();++i){
            auto actor=gameswf::cast_to<gameswf::sprite_instance>(game->m_display_list.get_character(i));
            if(!actor)continue;
            // Sunchips' intro clip620 is unnamed; its inner artwork is named
            // body. Match source IDs rather than assuming both NPC layouts.
            if(intro_body(actor))return true;
            for(int j=0;j<actor->m_display_list.size();++j)
                if(intro_body(gameswf::cast_to<gameswf::sprite_instance>(actor->m_display_list.get_character(j))))return true;
        }
        return false;
    }
    unsigned step(gameswf::sprite_instance* game){
        if(owner.get_ptr()!=game){owner=game;age=orphan=0;}
        auto pause=own(game,"pause");
        if((!pause.is_bool()&&!pause.is_number())||!pause.to_bool()){age=orphan=0;return 0;}
        ++age;orphan=active_intro(game)?0:orphan+1;
        // Both authored sequences finish within 85 source ticks. The generous
        // 120-tick ceiling also covers a repeatedly restarted/stalled intro.
        unsigned reason=orphan>=2?1:age>120?2:0;
        if(reason){game->set_member("pause",false);age=orphan=0;}
        return reason;
    }
};

// RAM-only, sparse device observations. No regular card writes. A confirmed
// orphan release, standing input stall, or L+R+SELECT snapshot saves the ring.
class ControlWatch {
    struct Row {
        uint64_t us=0;unsigned tick=0,raw=0,buttons=0,phase=0,keys=0,gate=0,analog=0,lx=0,ly=0;
        float x=0,y=0,health=0;int frame=-1,pause=0,body=-1,loaded=0,controllers=0;
    };
    Row rows[128]{};unsigned count=0,manual_previous=0,saved=0;
    uint64_t last_sample=0,last_saved=0,still_since=0;
    unsigned attempted=0;
    static float num(gameswf::as_object* o,const char* n){auto v=own(o,n);return v.is_number()?float(v.to_number()):NAN;}
    static gameswf::sprite_instance* named(gameswf::character* ch,const char* n){
        auto p=gameswf::cast_to<gameswf::sprite_instance>(ch);
        if(!p)return nullptr;
        if(auto c=gameswf::cast_to<gameswf::sprite_instance>(p->m_display_list.get_character_by_name_i(n)))return c;
        return gameswf::cast_to<gameswf::sprite_instance>(own(p,n).to_object());
    }
    static void members(FILE* f,const char* title,gameswf::sprite_instance* s){
        if(!s)return;std::fprintf(f,"%s id=%d frame=%d playing=%d loaded=%d children=%d\n",title,s->get_id(),s->get_current_frame()+1,s->get_play_state(),s->m_on_event_load_called,s->m_display_list.size());
        for(auto i=s->m_members.begin();i!=s->m_members.end();++i){const auto& v=i->second;
            if(v.is_number())std::fprintf(f,"  %s=%.17g\n",i->first.c_str(),v.to_number());
            else if(v.is_bool())std::fprintf(f,"  %s=%s\n",i->first.c_str(),v.to_bool()?"true":"false");
            else if(v.is_string())std::fprintf(f,"  %s=%s\n",i->first.c_str(),v.to_string());
            else if(v.is_undefined())std::fprintf(f,"  %s=undefined\n",i->first.c_str());
            else if(v.is_object()){auto c=gameswf::cast_to<gameswf::character>(v.to_object());if(c)std::fprintf(f,"  %s=clip %d %s depth=%d\n",i->first.c_str(),c->get_id(),c->get_name().c_str(),c->get_depth());}
        }
        for(int i=0;i<s->m_display_list.size();++i){auto c=s->m_display_list.get_character(i);if(c->get_id()!=62)continue;
            auto fn=gameswf::cast_to<gameswf::as_s_function>(own(c,"onEnterFrame").to_object());
            std::fprintf(f,"  controller depth=%d pc=%d length=%d fingerprint=%llu\n",c->get_depth(),fn?fn->m_start_pc:-1,fn?fn->m_length:-1,(unsigned long long)(fn?fn->m_body_fingerprint:0));
        }
    }
    void save(unsigned reason,gameswf::sprite_instance* game,gameswf::sprite_instance* hero){
        if(saved>=8)return;char base[32],path[40];
        for(;;){if(saved>=8)return;std::snprintf(base,sizeof(base),"CONTROL%02u",saved++);std::snprintf(path,sizeof(path),"%s.CSV",base);FILE* prior=std::fopen(path,"rb");if(!prior)break;std::fclose(prior);}
        FILE* f=std::fopen(path,"w");if(!f)return;
        std::fputs("us,tick,raw,buttons,phase,key_mask,blocked_buttons,blocked_analog,lx,ly,x,y,health,hero_frame,game_pause,body_frame,loaded,controllers\n",f);
        for(unsigned n=count>128?count-128:0;n<count;++n){const auto& r=rows[n%128];
            std::fprintf(f,"%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g,%d,%d,%d,%d,%d\n",(unsigned long long)r.us,r.tick,r.raw,r.buttons,r.phase,r.keys,r.gate,r.analog,r.lx,r.ly,double(r.x),double(r.y),double(r.health),r.frame,r.pause,r.body,r.loaded,r.controllers);
        }std::fclose(f);
        std::snprintf(path,sizeof(path),"%s.TXT",base);f=std::fopen(path,"w");if(!f)return;
        std::fprintf(f,"R8 control snapshot reason=%u (1 orphan-intro, 2 stalled-intro, 3 standing-input, 4 manual)\n",reason);
        members(f,"game",game);members(f,"hero",hero);members(f,"hero-body",named(hero,"body"));
        if(game)for(int i=0;i<game->m_display_list.size();++i){auto a=gameswf::cast_to<gameswf::sprite_instance>(game->m_display_list.get_character(i));
            if(a&&a->get_name().c_str()[0]=='k'){members(f,a->get_name().c_str(),a);members(f,"  body",named(a,"body"));}}
        std::fclose(f);
    }
public:
    void sample(gameswf::root* root,unsigned tick,unsigned raw,unsigned buttons,unsigned keys,unsigned gate,bool analog,unsigned lx,unsigned ly,uint64_t now,unsigned reason,bool manual,bool native_pause){
        bool pressed=manual&&!manual_previous;manual_previous=manual;
        if(now-last_sample<500000&&!reason&&!pressed)return;last_sample=now;
        auto game=named(root->get_root_movie(),"game"),hero=named(game,"dude1");Row r;
        r.us=now;r.tick=tick;r.raw=raw;r.buttons=buttons;r.keys=keys;r.gate=gate;r.analog=analog;r.lx=lx;r.ly=ly;r.phase=root->get_current_frame()+1;
        if(hero){r.x=num(hero,"x");r.y=num(hero,"y");r.health=num(hero,"health");r.frame=hero->get_current_frame()+1;r.loaded=hero->m_on_event_load_called;
            if(auto b=named(hero,"body"))r.body=b->get_current_frame()+1;
            for(int i=0;i<hero->m_display_list.size();++i)if(hero->m_display_list.get_character(i)->get_id()==62)++r.controllers;}
        r.pause=own(game,"pause").to_bool();
        if(count){const auto& p=rows[(count-1)%128];bool still=!native_pause&&r.phase==3&&r.health>0&&r.frame==1&&(buttons&0xf0)&&p.x==r.x&&p.y==r.y;
            if(still){if(!still_since)still_since=now;attempted|=buttons&0xf0;
                if(now-still_since>=4000000&&(attempted&(attempted-1))&&(!last_saved||now-last_saved>=30000000))reason=reason?reason:3;
            }else{still_since=0;attempted=0;}}
        rows[count++%128]=r;
        if(pressed)reason=reason?reason:4;
        if(reason){save(reason,game,hero);last_saved=now;}
    }
};
}
