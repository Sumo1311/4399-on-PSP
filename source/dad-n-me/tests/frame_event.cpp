#include "../src/frame_event_gate.hpp"
#include "gameswf/gameswf_player.h"
#include "base/tu_file.h"
#include "base/tu_timer.h"
#include "null_renderer.hpp"
#include <cstdio>
#include <cstdlib>
using namespace gameswf;
static unsigned checks=0,calls=0,last_args=0;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"frame-event mismatch at %u calls=%u\n",checks,calls);std::exit(1);}}
static tu_file* open_file(const char* p){return new tu_file(p,"rb");}
static void quiet(bool,const char*){}
static void handler(const fn_call& fn){++calls;last_args=fn.nargs;fn.result->set_undefined();}
struct VirtualProto:as_object {
 using as_object::as_object;
 bool get_member(const tu_stringi& name,as_value* out)override{if(name=="onEnterFrame"){out->set_as_c_function(handler);return true;}return as_object::get_member(name,out);}
};
struct VirtualClip:sprite_instance {
 using sprite_instance::sprite_instance;
 bool get_member(const tu_stringi& name,as_value* out)override{if(name=="onEnterFrame"){out->set_as_c_function(handler);return true;}return sprite_instance::get_member(name,out);}
};
int main(int argc,char** argv){
 if(argc!=2)return 2;tu_timer::init_timer();register_file_opener_callback(open_file);register_log_callback(quiet);NullRenderer renderer;set_render_handler(&renderer);
 gc_ptr<player> p=new player;p->set_separate_thread(false);auto root=p->load_file(argv[1]);if(!root)return 2;
 auto rs=cast_to<sprite_instance>(root->get_root_movie());gc_ptr<sprite_instance> clip=cast_to<sprite_instance>(rs->add_empty_movieclip("frame_gate_qa",1000));check(clip.get_ptr()!=nullptr);
 for(unsigned n=0;n<100;++n){
  check(dnm::frame_event_absent(clip));check(!clip->on_event(event_id::ENTER_FRAME));unsigned before=calls;
  clip->m_members.set("ONENTerFrame",as_value(handler));check(!dnm::frame_event_absent(clip));check(clip->on_event(event_id::ENTER_FRAME));check(calls==before+1);
  clip->m_members.set("onEnterFrame",as_value());check(!dnm::frame_event_absent(clip));check(clip->on_event(event_id::ENTER_FRAME));check(calls==before+1);
  clip->m_members.erase("onEnterFrame");check(dnm::frame_event_absent(clip));
 }
 gc_ptr<as_object> proto=new as_object(p.get_ptr()),next=new as_object(p.get_ptr());clip->m_proto=proto;proto->m_proto=next;
 check(dnm::frame_event_absent(clip));next->m_members.set("onEnterFrame",as_value(handler));check(!dnm::frame_event_absent(clip));unsigned before=calls;check(clip->on_event(event_id::ENTER_FRAME));check(calls==before+1);
 next->m_members.erase("onEnterFrame");check(dnm::frame_event_absent(clip));
 clip->m_proto=new VirtualProto(p.get_ptr());check(!dnm::frame_event_absent(clip));before=calls;check(clip->on_event(event_id::ENTER_FRAME));check(calls==before+1);clip->m_proto=nullptr;
 gc_ptr<sprite_instance> child=cast_to<sprite_instance>(clip->add_empty_movieclip("OnEnterFrame",1));check(!dnm::frame_event_absent(clip));clip->m_display_list.remove_display_object(child);check(dnm::frame_event_absent(clip));
 clip->m_members.set("onEnterFrame",as_value(as_value(handler),as_value()));before=calls;check(!dnm::frame_event_absent(clip));check(calls==before);clip->m_members.erase("onEnterFrame");
 // Builtin registries are read live, including mutations after a prior miss.
 for(auto id:{BUILTIN_SPRITE_METHOD,BUILTIN_OBJECT_METHOD}){
  auto* map=new_standard_method_map(id);check(dnm::frame_event_absent(clip));map->set("onEnterFrame",as_value(handler));check(!dnm::frame_event_absent(clip));before=calls;check(clip->on_event(event_id::ENTER_FRAME));check(calls==before+1);map->erase("onEnterFrame");check(dnm::frame_event_absent(clip));
 }
 gc_ptr<sprite_instance> derived=new VirtualClip(p.get_ptr(),clip->m_def.get_ptr(),clip->m_root,rs,2000);check(!dnm::frame_event_absent(derived));before=calls;check(derived->on_event(event_id::ENTER_FRAME));check(calls==before+1);
 // Long/cyclic prototypes fall back rather than inventing an absent result.
 clip->m_proto=proto;proto->m_proto=next;next->m_proto=proto;check(!dnm::frame_event_absent(clip));next->m_proto=nullptr;clip->m_proto=nullptr;
 for(unsigned n=1;n<event_id::EVENT_COUNT;++n){
  event_id id(static_cast<event_id::id_code>(n));const auto name=id.get_function_name().to_tu_stringi();
  check(!clip->on_event(id));clip->m_members.set(name,as_value(handler));check(!dnm::event_handler_absent(clip,id));before=calls;check(clip->on_event(id));check(calls==before+1);clip->m_members.erase(name);check(!clip->on_event(id));
 }
 check(!clip->has_keypress_event());check(!clip->can_handle_mouse_event());
 for(const char* name:{"onKeyPress","onRelease","onDragOver","onDragOut","onPress","onReleaseOutside","onRollout","onRollover","onMouseMove"}){
  clip->m_members.set(name,as_value());check(clip->can_handle_mouse_event());clip->m_enabled=false;check(!clip->can_handle_mouse_event());clip->m_enabled=true;
  if(tu_stringi(name)=="onKeyPress")check(clip->has_keypress_event());clip->m_members.erase(name);check(!clip->can_handle_mouse_event());
 }
 array<as_value> args;args.push_back(as_value(7));args.push_back(as_value("argument"));clip->m_members.set("onPress",as_value(handler));before=calls;check(clip->on_event(event_id(event_id::PRESS,&args)));check(calls==before+1);check(last_args==2);clip->m_members.erase("onPress");
 std::printf("{\"checks\":%u,\"callback_calls\":%u,\"gate_checks\":%llu,\"absence_proofs\":%llu,\"pass\":true}\n",checks,calls,dnm::frame_event_stats.checked,dnm::frame_event_stats.absent);
}
