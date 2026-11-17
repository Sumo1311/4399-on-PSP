#include "source_visual_policy.hpp"
#include "gameswf/gameswf_function.h"
#include "gameswf/gameswf_sprite.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <vector>
#include <unordered_set>

namespace gameswf {
namespace {
bool enabled(){
#ifndef PSP
    if(std::getenv("DNM_DISABLE_VISUAL_POLICY"))return false;
#endif
    return true;
}
struct Pending {weak_ptr<as_object> clip,game;unsigned kind;};
std::vector<Pending> pending;
struct Dying {weak_ptr<character> clip,owner,shadow;};
std::vector<Dying> dying;
std::unordered_set<character*> hidden;
DnmVisualStats stats;
as_value read(as_object* object,const char* name){as_value v;if(object)object->get_member(name,&v);return v;}
void write(as_object* object,const char* name,const as_value& value){if(object)object->set_member(name,value);}
as_value call(as_environment* env,as_object* self,const char* name,std::initializer_list<as_value> args){
    auto method=read(self,name);
    for(auto i=args.end();i!=args.begin();){--i;env->push(*i);}
    auto result=call_method(method,env,self,args.size(),env->get_top_index());
    env->drop(args.size());return result;
}
bool bind(const Pending& item){
    auto clip=cast_to<sprite_instance>(item.clip.get_ptr());auto game=item.game.get_ptr();
    if(!clip||!game||!clip->get_parent()||clip->get_depth()==0)return true;
    character* parents[4]{};auto parent=clip->get_parent();
    for(unsigned n=0;parent&&n<4;++n,parent=parent->get_parent())parents[n]=parent;
    const char* key=item.kind==0?"shirt_type":item.kind==1?"hat_type":"skin_type";
    as_object* owner=nullptr;
    for(int depth:{2,3,4,1})if(parents[depth-1]&&read(parents[depth-1],"root").to_bool()){
        owner=parents[depth-1];break;
    }
    // attachMovie executes child initialization before the caller assigns its
    // costume. Retry at the end of this source tick, after spawn parameters.
    if(!owner||read(owner,key).is_undefined()){
        owner=nullptr;
        for(int depth:{2,3,4,1})if(parents[depth-1]){
            auto found=parents[depth-1]->m_members.find(key);
            if(found!=parents[depth-1]->m_members.end()&&!found->second.is_undefined()){
                owner=parents[depth-1];break;
            }
        }
    }
    if(!owner)return false;
    auto value=read(owner,key);auto env=game->get_environment();if(!env)return false;
    if(item.kind==2)call(env,game,"f_ColorSwap",{as_value(clip),value});
    else call(env,clip,"gotoAndStop",{value});
    return true;
}
void queue_or_bind(as_object* clip,as_object* game,unsigned kind){
    Pending p{clip,game,kind};if(bind(p))return;
    auto same=[&](const Pending& v){return v.clip.get_ptr()==clip&&v.kind==kind;};
    if(std::find_if(pending.begin(),pending.end(),same)==pending.end()){
        pending.push_back(p);++stats.deferred;
    }
}
}
bool dnm_visual_accelerate(const as_s_function& f,const fn_call& fn,as_environment* env,as_object* self){
    if(!enabled()||!self||f.m_with_stack.size())return false;
    if(fn.nargs==1){
        int kind=-1;
        if(f.m_start_pc==62000&&f.m_length==321&&f.m_body_fingerprint==4081843245078997947ULL)kind=0;
        if(f.m_start_pc==62339&&f.m_length==321&&f.m_body_fingerprint==2815345938753162399ULL)kind=1;
        if(f.m_start_pc==62679&&f.m_length==325&&f.m_body_fingerprint==5564890099258681107ULL)kind=2;
        if(kind>=0){
            auto saved=env->m_global_register[1];queue_or_bind(fn.arg(0).to_object(),self,kind);
            env->m_global_register[1]=saved;return true;
        }
    }
    if(f.m_start_pc==17559&&f.m_length==285&&f.m_body_fingerprint==4344991489285263991ULL&&fn.nargs==6){
        auto type=fn.arg(3).to_tu_string();if(type!="fire1"&&type!="ragesmoke")return false;
        auto saved=env->m_global_register[1];
        tu_string slot="fx";slot+=read(self,"current_fx").to_tu_string();auto effect=read(self,slot.c_str());
        auto target=effect.to_object();
        write(target,"_x",fn.arg(0));write(target,"_y",fn.arg(1));
        write(target,"_xscale",fn.arg(4).is_undefined()?as_value(100):fn.arg(4));
        write(target,"_yscale",fn.arg(5).is_undefined()?as_value(100):fn.arg(5));
        call(env,self,"f_Depth",{effect,fn.arg(2)});
        // Original fire/smoke children have only play/remove actions, no RNG.
        // Keep caller RNG, the shared FX cursor and mask/depth helper order.
        call(env,target,"gotoAndStop",{"remove"});
        write(target,"x",fn.arg(0));write(target,"y",fn.arg(2));
        call(env,self,"f_SpawnMask",{effect});
        double cursor=read(self,"current_fx").to_number()+1;
        if(cursor>read(self,"total_fx").to_number())cursor=1;
        write(self,"current_fx",cursor);*fn.result=effect;
        env->m_global_register[1]=saved;++stats.fire_suppressed;return true;
    }
    return false;
}
void dnm_finish_visual_bindings(){
    if(!enabled())return;
    if(!pending.empty()){
        std::vector<Pending> work;work.swap(pending);
        for(const auto& p:work){if(bind(p))++stats.rebound;else pending.push_back(p);}
    }
    hidden.clear();
    dying.erase(std::remove_if(dying.begin(),dying.end(),[](const Dying& entry){
        auto clip=entry.clip.get_ptr(),owner=entry.owner.get_ptr();
        return !clip||!owner||!owner->get_parent()||owner->get_depth()==0;
    }),dying.end());
    for(const auto& entry:dying){
        auto clip=entry.clip.get_ptr();int start=clip->get_id()==748?34:15;
        if(clip->get_current_frame()>=start+6){
            hidden.insert(entry.owner.get_ptr());
            if(auto shadow=entry.shadow.get_ptr())hidden.insert(shadow);
        }
    }
}
int dnm_visual_frame(character* clip,int target){
    if(!enabled())return target;
    int start=clip->get_id()==748?34:(clip->get_id()==665||clip->get_id()==439)?15:-1;
    if(start>=0&&target==start){
        auto found=std::find_if(dying.begin(),dying.end(),[&](const Dying& entry){return entry.clip.get_ptr()==clip;});
        if(found==dying.end())for(auto parent=clip->get_parent();parent;parent=parent->get_parent()){
            auto shadow=cast_to<character>(read(parent,"shadow_pt").to_object());
            if(shadow){dying.push_back({clip,parent,shadow});break;}
        }
    }
    const int wrappers[]={284,287,290,427,432,552,557,653,658,742};
    if(target>0&&target<3&&std::find(std::begin(wrappers),std::end(wrappers),clip->get_id())!=std::end(wrappers))return 0;
    if(clip->get_id()==1384&&target==1)return 0; // Normal portrait, no rage flame overlay.
    return target;
}
bool dnm_visual_hidden(character* clip){
    if(!enabled())return false;
    if(hidden.count(clip))return true;
    int start=-1;switch(clip->get_id()){case 665:case 439:start=15;break;case 748:start=34;break;}
    if(start<0)return false;int offset=clip->get_current_frame()-start;
    // One on/off/on blink, then hide the corpse. The original timeline still
    // reaches its score/checkpoint/removal actions at the original source tick.
    return offset>=6||(offset>=2&&offset<4);
}
bool dnm_run_frame_event(character* clip){
    if(!enabled()||clip->get_id()!=62)return true;
#ifndef PSP
    if(std::getenv("DNM_DISABLE_FLAME_CALLBACK_OPT"))return true;
#endif
    auto parent=clip->get_parent();if(!parent)return true;
    // These four audited ClipActions only select the removed onfire artwork.
    // Other instances of the shared empty clip62 run gameplay callbacks.
    switch(parent->get_id()){
    case 284:case 427:case 653:case 742:++stats.flame_callbacks_skipped;return false;
    default:return true;
    }
}
DnmVisualStats dnm_visual_stats(){auto result=stats;result.pending=pending.size();return result;}
}
