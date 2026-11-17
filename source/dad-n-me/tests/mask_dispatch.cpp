#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_sprite.h"
#include "gameswf/gameswf_function.h"
#include "base/tu_file.h"
#include "base/tu_timer.h"
#include "null_renderer.hpp"
#include <cstdio>
#include <cstdlib>
using namespace gameswf;
namespace gameswf {as_value dnm_test_mask_dispatch(as_environment*,as_object*,const as_value&,const as_value&);}
static unsigned checks=0,gotos=0,getters=0,setters=0,rebound=0,method_getters=0;
static as_value property_value,original_method;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"mask check failed: %u\n",checks);std::exit(1);}}
static tu_file* open_file(const char* p){return new tu_file(p,"rb");}
static void go(const fn_call& fn){check(fn.nargs==1&&fn.arg(0).to_number()==1);++gotos;}
static void get(const fn_call& fn){++getters;*fn.result=property_value;}
static void set(const fn_call& fn){++setters;property_value=fn.arg(0);}
static void changed(const fn_call& fn){check(fn.nargs==2);++rebound;*fn.result=42;}
static void method(const fn_call& fn){++method_getters;*fn.result=original_method;}
int main(int argc,char**argv){
 if(argc!=2)return 2;tu_timer::init_timer();register_file_opener_callback(open_file);NullRenderer renderer;set_render_handler(&renderer);
 gc_ptr<player> p=new player;p->set_separate_thread(false);auto root=p->load_file(argv[1]);check(bool(root));
 for(int i=0;i<240;++i)root->advance(1.f/30);
 as_value value;root->get_root_movie()->get_member("game",&value);gc_ptr<as_object> game=value.to_object();check(bool(game));
 game->get_member("f_CheckMask",&original_method);auto function=cast_to<as_s_function>(original_method.to_object());check(function&&function->m_body_fingerprint==10363626387468703778ULL);
 auto env=game->get_environment();check(env!=nullptr);
 gc_ptr<as_object> actor=new as_object(p),other=new as_object(p),different=new as_object(p),mask=new as_object(p),proto=new as_object(p);
 mask->set_member("gotoAndStop",as_value(go));actor->set_member("mask",mask.get_ptr());
 auto invoke=[&](){return dnm_test_mask_dispatch(env,game,as_value(actor.get_ptr()),as_value(other.get_ptr()));};
 auto own=[&](as_object* object,const char* name){return object->m_members.find(name);};
 invoke();check(gotos==0&&own(actor,"mask_pt")==actor->m_members.end());
 actor->set_member("mask_pt",different.get_ptr());invoke();check(gotos==0&&own(actor,"mask_pt")->second.to_object()==different.get_ptr());
 actor->set_member("mask_pt",other.get_ptr());invoke();check(gotos==1&&own(actor,"mask_pt")->second.is_undefined());
 gc_ptr<as_function> getter=new as_c_function(p,get),setter=new as_c_function(p,set),method_getter=new as_c_function(p,method);
 as_value property(as_value(getter.get_ptr()),as_value(setter.get_ptr()));
 property_value=different.get_ptr();actor->m_members.set("mask_pt",property);invoke();check(getters==1&&setters==0&&gotos==1);
 property_value=other.get_ptr();invoke();check(getters==2&&setters==1&&gotos==2&&property_value.is_undefined());
 actor->m_members.erase("mask_pt");actor->m_proto=proto;proto->set_member("mask_pt",different.get_ptr());invoke();check(gotos==2&&own(actor,"mask_pt")==actor->m_members.end());
 proto->m_members.set("mask_pt",property);property_value=different.get_ptr();invoke();check(getters==3&&setters==1&&gotos==2);
 proto->m_members.set("mask_pt",as_value(other.get_ptr()));invoke();check(gotos==3&&own(actor,"mask_pt")->second.is_undefined()&&own(proto,"mask_pt")->second.to_object()==other.get_ptr());
 game->m_members.set("f_CheckMask",as_value(changed));auto result=invoke();check(rebound==1&&result.to_number()==42);
 game->m_members.set("f_CheckMask",as_value(as_value(method_getter.get_ptr()),as_value()));actor->m_members.set("mask_pt",as_value(different.get_ptr()));invoke();check(method_getters==1&&gotos==3);
 game->m_members.set("f_CheckMask",original_method);
 std::printf("{\"checks\":%u,\"gotos\":%u,\"getters\":%u,\"setters\":%u,\"rebound_calls\":%u,\"method_getters\":%u,\"pass\":true}\n",checks,gotos,getters,setters,rebound,method_getters);
}
