#include "source_member_keys.hpp"
#include "source_visual_policy.hpp"
#include "gameswf/gameswf_function.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_sprite.h"
#include <cmath>
#include "base/tu_random.h"
#include <cstdlib>
#include <string>
#include <initializer_list>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <typeinfo>
namespace gameswf {
namespace {
auto member(as_object* object,const source_keys::Key& key){
#ifndef PSP
 static const bool prehashed=!std::getenv("DNM_DISABLE_PREHASH");
 if(!prehashed)return object->m_members.find(key.name);
 static const bool cached=!std::getenv("DNM_DISABLE_MEMBER_INDEX_CACHE");
 if(!cached)return object->m_members.find_prehashed(key.name,key.hash);
#endif
 auto* oldest=&key.cache[0];unsigned now=++key.clock;
 for(auto& entry:key.cache){
  if(entry.owner==object){entry.used=now;return object->m_members.find_prehashed_cached(key.name,key.hash,entry.epoch,entry.index);}
  if(now-entry.used>now-oldest->used)oldest=&entry;
 }
 oldest->owner=object;oldest->epoch=0;oldest->index=-1;oldest->used=now;
 return object->m_members.find_prehashed_cached(key.name,key.hash,oldest->epoch,oldest->index);
}
// Exact decimal names for fixed small source-loop indices. Dynamic values
// retain the original conversion path. No local or helper call is removed.
const char* const wall_names[2][128]={
 {"static0","static1","static2","static3","static4","static5","static6","static7","static8","static9","static10","static11","static12","static13","static14","static15","static16","static17","static18","static19","static20","static21","static22","static23","static24","static25","static26","static27","static28","static29","static30","static31","static32","static33","static34","static35","static36","static37","static38","static39","static40","static41","static42","static43","static44","static45","static46","static47","static48","static49","static50","static51","static52","static53","static54","static55","static56","static57","static58","static59","static60","static61","static62","static63","static64","static65","static66","static67","static68","static69","static70","static71","static72","static73","static74","static75","static76","static77","static78","static79","static80","static81","static82","static83","static84","static85","static86","static87","static88","static89","static90","static91","static92","static93","static94","static95","static96","static97","static98","static99","static100","static101","static102","static103","static104","static105","static106","static107","static108","static109","static110","static111","static112","static113","static114","static115","static116","static117","static118","static119","static120","static121","static122","static123","static124","static125","static126","static127"},
 {"object_pt0","object_pt1","object_pt2","object_pt3","object_pt4","object_pt5","object_pt6","object_pt7","object_pt8","object_pt9","object_pt10","object_pt11","object_pt12","object_pt13","object_pt14","object_pt15","object_pt16","object_pt17","object_pt18","object_pt19","object_pt20","object_pt21","object_pt22","object_pt23","object_pt24","object_pt25","object_pt26","object_pt27","object_pt28","object_pt29","object_pt30","object_pt31","object_pt32","object_pt33","object_pt34","object_pt35","object_pt36","object_pt37","object_pt38","object_pt39","object_pt40","object_pt41","object_pt42","object_pt43","object_pt44","object_pt45","object_pt46","object_pt47","object_pt48","object_pt49","object_pt50","object_pt51","object_pt52","object_pt53","object_pt54","object_pt55","object_pt56","object_pt57","object_pt58","object_pt59","object_pt60","object_pt61","object_pt62","object_pt63","object_pt64","object_pt65","object_pt66","object_pt67","object_pt68","object_pt69","object_pt70","object_pt71","object_pt72","object_pt73","object_pt74","object_pt75","object_pt76","object_pt77","object_pt78","object_pt79","object_pt80","object_pt81","object_pt82","object_pt83","object_pt84","object_pt85","object_pt86","object_pt87","object_pt88","object_pt89","object_pt90","object_pt91","object_pt92","object_pt93","object_pt94","object_pt95","object_pt96","object_pt97","object_pt98","object_pt99","object_pt100","object_pt101","object_pt102","object_pt103","object_pt104","object_pt105","object_pt106","object_pt107","object_pt108","object_pt109","object_pt110","object_pt111","object_pt112","object_pt113","object_pt114","object_pt115","object_pt116","object_pt117","object_pt118","object_pt119","object_pt120","object_pt121","object_pt122","object_pt123","object_pt124","object_pt125","object_pt126","object_pt127"}
};
as_value read(as_object* o,const source_keys::Key& key){as_value v;if(o){
 // These built-in names are not sprite methods. Preserve sprite child-name
 // precedence, then use the exact character getter expressions and as_value
 // conversion. Every other property retains ordinary virtual resolution.
 if(key.standard!=M_INVALID_MEMBER){
  switch(key.standard){
   case M_X:case M_Y:case M_XSCALE:case M_YSCALE:case M_WIDTH:case M_HEIGHT:
    if(auto sprite=cast_to<sprite_instance>(o)){
     if(!sprite->m_display_list.get_character_by_name_i(key.name)){
      const auto& matrix=sprite->get_matrix();
      switch(key.standard){
       case M_X:v.set_double(double(float(matrix.m_[0][2]))/20.0);break;
       case M_Y:v.set_double(double(float(matrix.m_[1][2]))/20.0);break;
       case M_XSCALE:v.set_double(matrix.get_x_scale()*100);break;
       case M_YSCALE:v.set_double(matrix.get_y_scale()*100);break;
       case M_WIDTH:v.set_double((int)TWIPS_TO_PIXELS(sprite->get_width()));break;
       case M_HEIGHT:v.set_double((int)TWIPS_TO_PIXELS(sprite->get_height()));break;
       default:break;
      }
      return v;
     }
    }
    break;
   default:break;
  }
  o->get_member(key.name,&v);
 }else{
   auto it=member(o,key);
  if(it!=o->m_members.end() && !it->second.is_property())v=it->second;
  else o->get_member(key.name,&v);
 }
 }if(v.is_property()){as_value result;v.get_property(&result);return result;}return v;}
as_object* object(as_object* o,const source_keys::Key& key){return read(o,key).to_object();}
double number(as_object* o,const source_keys::Key& key){
 // The ordinary read path copies an as_value before converting it. An own
 // primitive number has no getter/conversion callback, so read it directly.
 // Built-ins, properties, inherited values and other types keep that path.
#ifndef PSP
 static const bool fast=!std::getenv("DNM_DISABLE_NUMERIC_READ_ACCEL");
 if(fast)
#endif
 if(o&&key.standard==M_INVALID_MEMBER){
   auto it=member(o,key);
  if(it!=o->m_members.end()&&it->second.is_number())return it->second.to_number();
 }
 return read(o,key).to_number();
}
// The bound source defines every named collision zone as sprite 1467:
// one frame containing only immutable DefineShape3 1466. Cache the original
// built-in width/height results only while both local matrices and that exact
// live display child are unchanged. Parent transforms do not enter get_bound.
// Weak references prevent this cache from extending a clip's lifetime.
bool collision_dimensions(as_object* object,double& height,double& width){
 auto zone=cast_to<sprite_instance>(object);
 if(!zone || zone->get_id()!=1467 || zone->get_frame_count()!=1 ||
    zone->m_display_list.size()!=1)return false;
 auto child=zone->m_display_list.get_character(0);
 if(!child || child->get_id()!=1466)return false;
 struct Entry {
  weak_ptr<sprite_instance> zone;
  weak_ptr<character> child;
  character_def* definition=nullptr;
  matrix zone_matrix,child_matrix;
  double height=0,width=0;
 };
 static std::unordered_map<sprite_instance*,Entry> entries;
 auto found=entries.find(zone);
 if(found!=entries.end()){
  const auto& entry=found->second;
  if(entry.zone.get_ptr()==zone && entry.child.get_ptr()==child &&
     entry.definition==child->get_character_def() &&
     entry.zone_matrix==zone->get_matrix() && entry.child_matrix==child->get_matrix()){
   height=entry.height;width=entry.width;return true;
  }
 }
 // Preserve the built-in property's integer-pixel conversion and evaluation
 // order instead of reimplementing its floating-point bounds arithmetic.
 height=number(zone,source_keys::get(source_keys::key__height));
 width=number(zone,source_keys::get(source_keys::key__width));
 if(entries.size()>=512)entries.clear();
 auto& entry=entries[zone];entry.zone=zone;entry.child=child;
 entry.definition=child->get_character_def();entry.zone_matrix=zone->get_matrix();
 entry.child_matrix=child->get_matrix();entry.height=height;entry.width=width;
 return true;
}
struct WallRectangle {double xs,ys,x,y,h,w;};
// Reuse exactly the six original double results. The guarded path performs no
// script accessors: x/y are own numeric values, scale properties are native,
// and the zone contains the source's one immutable shape. Recheck every live
// input before a hit, including all matrix bits and the resolved zone pointer.
// This does not cache collision decisions, actor positions, or helper effects.
bool wall_rectangle(as_object* object,as_object* zone_object,WallRectangle& out){
#ifndef PSP
 static const bool enabled=!std::getenv("DNM_DISABLE_RECT_CACHE");
 if(!enabled)return false;
#endif
 auto other=cast_to<sprite_instance>(object),zone=cast_to<sprite_instance>(zone_object);
 if(!other||!zone||zone->get_id()!=1467||zone->get_frame_count()!=1||zone->m_display_list.size()!=1)return false;
 auto child=zone->m_display_list.get_character(0);
 if(!child||child->get_id()!=1466||child->get_name().length()!=0)return false;
 const auto& xkey=source_keys::get(source_keys::key_x);const auto& ykey=source_keys::get(source_keys::key_y);
 auto xi=member(other,xkey),yi=member(other,ykey);
 if(xi==other->m_members.end()||yi==other->m_members.end()||!xi->second.is_number()||!yi->second.is_number())return false;
 double px=xi->second.to_number(),py=yi->second.to_number();
 if(!std::isfinite(px)||!std::isfinite(py))return false;
 if(other->m_display_list.get_character_by_name_i(source_keys::get(source_keys::key__xscale).name)||
    other->m_display_list.get_character_by_name_i(source_keys::get(source_keys::key__yscale).name))return false;
 struct Entry {
  weak_ptr<sprite_instance> other,zone;weak_ptr<character> child;
  character_def* definition=nullptr;matrix other_matrix,zone_matrix,child_matrix;
  double x=0,y=0;WallRectangle result{};
 };
 static Entry entries[256];
 auto& e=entries[(reinterpret_cast<uintptr_t>(other)>>4)&255];
 const auto& om=other->get_matrix();const auto& zm=zone->get_matrix();const auto& cm=child->get_matrix();
 if(e.other.get_ptr()==other&&e.zone.get_ptr()==zone&&e.child.get_ptr()==child&&e.definition==child->get_character_def()&&
    !std::memcmp(&e.x,&px,sizeof(px))&&!std::memcmp(&e.y,&py,sizeof(py))&&
    !std::memcmp(&e.other_matrix.m_,&om.m_,sizeof(om.m_))&&
    !std::memcmp(&e.zone_matrix.m_,&zm.m_,sizeof(zm.m_))&&
    !std::memcmp(&e.child_matrix.m_,&cm.m_,sizeof(cm.m_))){out=e.result;return true;}
 out.xs=std::fabs(number(other,source_keys::get(source_keys::key__xscale)))/100;
 out.ys=std::fabs(number(other,source_keys::get(source_keys::key__yscale)))/100;
 out.x=number(other,xkey)+number(zone,source_keys::get(source_keys::key__x));
 out.y=number(other,ykey)+number(zone,source_keys::get(source_keys::key__y))+2;
 double height=0,width=0;
 if(!collision_dimensions(zone,height,width))return false;
 out.h=height*out.ys;out.w=width/2*out.xs;
 e.other=other;e.zone=zone;e.child=child;e.definition=child->get_character_def();
 e.other_matrix=om;e.zone_matrix=zm;e.child_matrix=cm;e.x=px;e.y=py;e.result=out;
 return true;
}
void write(as_object* o,const source_keys::Key& key,const as_value& v){if(!o)return;
 if(key.standard!=M_INVALID_MEMBER){o->set_member(key.name,v);return;}
  auto it=member(o,key);
 if(o->m_watch==nullptr && it!=o->m_members.end() && !it->second.is_property() && !it->second.is_readonly()){int flags=it->second.get_flags();it->second=v;it->second.set_flags(flags);}else o->set_member(key.name,v);
}
bool mask_is_noop(const as_s_function& function,const as_value& actor_value,const as_value& other){
#ifndef PSP
 if(std::getenv("DNM_DISABLE_ACCEL")||std::getenv("DNM_DISABLE_MASK_NOOP"))return false;
#endif
 if(function.m_start_pc!=32515||function.m_length!=77||function.m_body_fingerprint!=10363626387468703778ULL||function.m_with_stack.size())return false;
 if(!actor_value.is_object()||other.is_property())return false;
 auto actor=actor_value.to_object();if(!actor)return false;
 const auto& key=source_keys::get(source_keys::key_mask_pt);
 if(auto sprite=cast_to<sprite_instance>(actor)){
  if(sprite->m_display_list.get_character_by_name_i(key.name))return false;
 }else if(typeid(*actor)!=typeid(as_object))return false;
 auto it=member(actor,key);
 if(it!=actor->m_members.end())return !it->second.is_property()&&it->second!=other;
 // Missing own mask_pt is common. Prove the inherited result without invoking
 // a getter or a subclass's virtual lookup. Unknown/cyclic chains fall back.
 auto proto=actor->get_proto();
 for(unsigned depth=0;proto&&depth<16;++depth){
  if(typeid(*proto)!=typeid(as_object))return false;
  auto found=member(proto,key);
  if(found!=proto->m_members.end())return !found->second.is_property()&&found->second!=other;
  proto=proto->get_proto();
 }
 return !proto && as_value()!=other;
}
as_value invoke(as_environment* env,as_object* receiver,const source_keys::Key& key,std::initializer_list<as_value> args){
 auto method=read(receiver,key);
 // Resolve the original method first. Its guarded no-action branch needs no
 // argument-stack or environment setup; matching/property/rebound cases keep
 // ordinary dispatch. The method getter itself is never skipped.
#ifndef PSP
 static const bool early_mask=!std::getenv("DNM_DISABLE_EARLY_MASK_NOOP");
 if(early_mask)
#endif
 if(receiver && receiver->m_this_ptr==nullptr && args.size()==2 && &key==&source_keys::get(source_keys::key_f_CheckMask)){
  if(auto function=cast_to<as_s_function>(method.to_object()))
   if(mask_is_noop(*function,*args.begin(),*(args.begin()+1)))return as_value();
 }
 for(auto i=args.end();i!=args.begin();)env->push(*--i);
 auto result=call_method(method,env,receiver,args.size(),env->get_top_index());env->drop(args.size());return result;
}
// Cache immutable names and the current loop-counter binding, never object
// values or hash-table addresses. Nested helpers invalidate the binding.
const source_keys::Key* indexed_source_key(unsigned kind,const as_value& index){
 if(!index.is_number())return nullptr;double n=index.to_number();
 if(!(n>0&&n<128&&n==int(n)))return nullptr;
 static const auto keys=[](){
  std::vector<source_keys::Key> out;out.reserve(5*128);
  for(const char* prefix:{"static","object","object_pt","kid","shadow"})for(unsigned i=0;i<128;++i){char name[32];std::snprintf(name,sizeof(name),"%s%u",prefix,i);out.emplace_back(name);}
  return out;
 }();
 return &keys[kind*128+unsigned(n)];
}
as_value indexed_read(as_object* self,unsigned kind,const as_value& index){
 if(auto key=indexed_source_key(kind,index))return read(self,*key);
 const char* prefixes[]={"static","object","object_pt","kid","shadow"};std::string name=prefixes[kind];name+=index.to_string();return read(self,name.c_str());
}
void indexed_write(as_object* self,unsigned kind,const as_value& index,const as_value& value){
 if(auto key=indexed_source_key(kind,index)){write(self,*key,value);return;}
 const char* prefixes[]={"static","object","object_pt","kid","shadow"};std::string name=prefixes[kind];name+=index.to_string();write(self,name.c_str(),value);
}
struct SourceIndex {
 as_environment* env;array<with_stack_entry>& stack;tu_string name{"i"};int slot=-1;
 weak_ptr<character> target;bool plain=false,dirty=true,optimized=true;
 SourceIndex(as_environment* e,array<with_stack_entry>& s,bool fast=true):env(e),stack(s),optimized(fast){}
 void refresh(){slot=env->find_local(name,true);auto t=env->get_target();target=t;auto sprite=cast_to<sprite_instance>(t);plain=t&&(!sprite||!sprite->m_display_list.get_character_by_name_i(name));dirty=false;}
 as_value get(){
  if(!optimized)return env->get_variable(name,stack);
  if(dirty||target.get_ptr()!=env->get_target())refresh();
  if(slot>=0&&slot<env->m_local_frames.size()&&env->m_local_frames[slot].m_name==name)return env->m_local_frames[slot].m_value;
  auto t=target.get_ptr();if(plain&&t){auto it=t->m_members.find(name);if(it!=t->m_members.end()&&!it->second.is_property())return it->second;}
  return env->get_variable_raw(name,stack);
 }
 void set(const as_value& value){
  if(!optimized){env->set_variable(name,value,stack);return;}
  if(dirty||target.get_ptr()!=env->get_target())refresh();
  if(slot>=0&&slot<env->m_local_frames.size()&&env->m_local_frames[slot].m_name==name){env->m_local_frames[slot].m_value=value;return;}
  auto t=target.get_ptr();if(plain&&t&&!t->m_watch){auto it=t->m_members.find(name);if(it!=t->m_members.end()&&!it->second.is_property()&&!it->second.is_readonly()){int flags=it->second.get_flags();it->second=value;it->second.set_flags(flags);return;}}
  env->set_variable_raw(name,value,stack);
 }
 void invalidate(){dirty=true;}
};
}
// Translation is enabled only for this exact original body and constant pool
// placement. Every other AVM1 function retains its interpreter implementation.
bool dnm_mask_check_noop(const as_s_function& function,const fn_call& fn){
 // This exact body has no effects when its own plain value differs. No
 // getter, conversion callback or nested method is evaluated in this case.
 return fn.nargs==2&&mask_is_noop(function,fn.arg(0),fn.arg(1));
}
#ifndef PSP
as_value dnm_test_mask_dispatch(as_environment* env,as_object* self,const as_value& actor,const as_value& other){
 return invoke(env,self,source_keys::get(source_keys::key_f_CheckMask),{actor,other});
}
#endif
bool dnm_accelerate(const as_s_function& function,const fn_call& fn,as_environment* env,as_object* self){
 if(dnm_visual_accelerate(function,fn,env,self))return true;
#ifndef PSP
 if(std::getenv("DNM_DISABLE_ACCEL"))return false;
#endif
 if(!self || function.m_with_stack.size())return false;
 bool movement_callbacks=true;
#ifndef PSP
 movement_callbacks=!std::getenv("DNM_DISABLE_MOVE_CALLBACK_ACCEL");
#endif
 if(movement_callbacks&&function.m_start_pc==42238&&function.m_length==547&&function.m_body_fingerprint==3000697099375045018ULL&&fn.nargs==1){
  static const source_keys::Key sx("speed_x"),sy("speed_y"),flip("f_FlipChar"),move("f_MoveKid");
  as_value value=fn.arg(0),saved=env->m_global_register[1];gc_ptr<as_object> actor=value.to_object();
  if(number(actor,sx)>0&&number(actor,source_keys::get(source_keys::key__xscale))<0)invoke(env,self,flip,{value});
  else if(number(actor,sx)<0&&number(actor,source_keys::get(source_keys::key__xscale))>0)invoke(env,self,flip,{value});
  if(!invoke(env,self,move,{value,false,read(actor,sx)}).to_bool())write(actor,sx,number(actor,sx)*-1);
  if(!invoke(env,self,move,{value,true,read(actor,sy)}).to_bool())write(actor,sy,number(actor,sy)*-1);
  if(number(actor,source_keys::get(source_keys::key_y))>number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_y))&&number(actor,sy)>0)write(actor,sy,number(actor,sy)*-1);
  else if(number(actor,source_keys::get(source_keys::key_y))<number(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_y))&&number(actor,sy)<0)write(actor,sy,number(actor,sy)*-1);
  if(number(actor,source_keys::get(source_keys::key_x))>number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_x))&&number(actor,sx)>0)write(actor,sx,number(actor,sx)*-1);
  else if(number(actor,source_keys::get(source_keys::key_x))<number(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_x))&&number(actor,sx)<0)write(actor,sx,number(actor,sx)*-1);
  env->m_global_register[1]=saved;return true;
 }
 if(movement_callbacks&&function.m_start_pc==77993&&function.m_length==607&&function.m_body_fingerprint==7944265605127752648ULL&&fn.nargs==1){
  static const source_keys::Key ball_key("target_ball"),target("target_pt"),tx("target_x"),ty("target_y"),partner("partner"),autoaction("autoaction"),
   slide_hit("slide_hit"),slide_speed("slide_speed"),ground("s_Ground1"),start("start"),chase("f_SoccerChase");
  as_value value=fn.arg(0),saved1=env->m_global_register[1],saved2=env->m_global_register[2];gc_ptr<as_object> actor=value.to_object();array<with_stack_entry> empty;bool moving=true;
  auto ball=[&](){return object(actor,ball_key);};auto field=[&](){return object(self,source_keys::get(source_keys::key_soccerfield));};
  write(actor,target,read(actor,ball_key));write(actor,tx,read(ball(),source_keys::get(source_keys::key_x)));
  if(read(ball(),source_keys::get(source_keys::key_owner))==read(actor,partner)){
   if(number(actor,source_keys::get(source_keys::key_y))<number(field(),source_keys::get(source_keys::key_y)))write(actor,ty,number(ball(),source_keys::get(source_keys::key_y))-100);
   else write(actor,ty,number(ball(),source_keys::get(source_keys::key_y))+100);
  }else write(actor,ty,read(ball(),source_keys::get(source_keys::key_y)));
  if(std::fabs(number(actor,source_keys::get(source_keys::key_x))-number(actor,tx))<50&&std::fabs(number(actor,source_keys::get(source_keys::key_y))-number(actor,ty))<10){
   if(read(object(actor,target),source_keys::get(source_keys::key_owner))==env->get_variable("NULL",empty)){
    write(object(actor,target),source_keys::get(source_keys::key_owner),value);write(actor,source_keys::get(source_keys::key_item),read(actor,target));invoke(env,object(actor,target),autoaction,{read(actor,target)});
   }else if(read(object(actor,target),source_keys::get(source_keys::key_owner))==read(self,source_keys::get(source_keys::key_dude1))){
    moving=false;write(actor,slide_hit,false);write(actor,slide_speed,16);invoke(env,object(self,ground),start,{0,0});invoke(env,actor,source_keys::get(source_keys::key_gotoAndStop),{"soccerslide"});
   }else{moving=false;invoke(env,actor,source_keys::get(source_keys::key_gotoAndStop),{"soccerwait"});}
  }
  if(moving){invoke(env,self,chase,{value});invoke(env,actor,source_keys::get(source_keys::key_gotoAndStop),{"soccerwalk"});}
  env->m_global_register[1]=saved1;env->m_global_register[2]=saved2;return true;
 }
 if(movement_callbacks&&function.m_start_pc==10726&&function.m_length==1106&&function.m_body_fingerprint==16407877870710803228ULL&&fn.nargs==1){
  static const source_keys::Key sy("speed_y"),volume("f_EngineVolume"),current("current_vehicle"),total("total_vehicles"),engine("s_Engine"),stop("stop"),
   car("car1"),play("gotoAndPlay"),distance("car_distance"),check("f_CarCheckHit"),flip("f_FlipChar"),sound("f_HardPunchSound");
  as_value value=fn.arg(0),saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};gc_ptr<as_object> vehicle=value.to_object();array<with_stack_entry> empty;SourceIndex counter(env,empty);
  write(vehicle,source_keys::get(source_keys::key__y),number(vehicle,source_keys::get(source_keys::key__y))+number(vehicle,sy));
  write(vehicle,source_keys::get(source_keys::key_y),read(vehicle,source_keys::get(source_keys::key__y)));write(vehicle,source_keys::get(source_keys::key_x),read(vehicle,source_keys::get(source_keys::key__x)));
  write(vehicle,sy,number(vehicle,sy)+1);write(vehicle,source_keys::get(source_keys::key__xscale),number(vehicle,source_keys::get(source_keys::key__xscale))+number(vehicle,sy)/12);
  write(vehicle,source_keys::get(source_keys::key__yscale),number(vehicle,source_keys::get(source_keys::key__yscale))+number(vehicle,sy)/12);write(vehicle,source_keys::get(source_keys::key_depth_mod),990);
  invoke(env,self,source_keys::get(source_keys::key_f_Depth),{value,read(vehicle,source_keys::get(source_keys::key_y))});invoke(env,self,volume,{});
  auto bottom=[&](){return number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_y));};
  if(number(vehicle,source_keys::get(source_keys::key_y))>bottom()+number(vehicle,source_keys::get(source_keys::key__height))){
   auto n=read(self,current);n+=1;write(self,current,n);if(number(self,current)>number(self,total))write(self,current,1);
   invoke(env,object(self,engine),stop,{});invoke(env,object(object(self,source_keys::get(source_keys::key_bg2)),car),play,{2});invoke(env,vehicle,source_keys::get(source_keys::key_gotoAndStop),{1});
  }else{
   write(self,distance,number(self,distance)+(number(vehicle,source_keys::get(source_keys::key_y))>bottom()+number(vehicle,source_keys::get(source_keys::key__height))-300?-15:4));
   bool hit=false;counter.invalidate();counter.set(1);
   while(counter.get().to_number()<=number(self,source_keys::get(source_keys::key_total_kids))){
    as_value other=indexed_read(self,3,counter.get());
    if(read(other.to_object(),source_keys::get(source_keys::key_alive)).to_bool()&&invoke(env,self,check,{value,other}).to_bool())hit=true;
    counter.invalidate();auto n=counter.get();n+=1;counter.set(n);
   }
   counter.set(1);
   while(counter.get().to_number()<=number(self,source_keys::get(source_keys::key_total_objects))){
    auto other=indexed_read(self,2,counter.get());if(invoke(env,self,check,{value,other}).to_bool())hit=true;
    counter.invalidate();auto n=counter.get();n+=1;counter.set(n);
   }
   as_value hero_value=read(self,source_keys::get(source_keys::key_dude1));gc_ptr<as_object> hero=hero_value.to_object();
   if(number(hero,source_keys::get(source_keys::key_y))<number(vehicle,source_keys::get(source_keys::key_y))&&number(hero,source_keys::get(source_keys::key_y))>number(vehicle,source_keys::get(source_keys::key_y))-100&&
      std::fabs(number(vehicle,source_keys::get(source_keys::key_x))-number(hero,source_keys::get(source_keys::key_x)))<number(vehicle,source_keys::get(source_keys::key__width))/2&&!read(hero,source_keys::get(source_keys::key_nohit)).to_bool()){
    hit=true;invoke(env,self,source_keys::get(source_keys::key_f_Damage),{hero_value,10});
    if((number(hero,source_keys::get(source_keys::key_x))<number(vehicle,source_keys::get(source_keys::key_x))&&number(hero,source_keys::get(source_keys::key__xscale))<0)||
       (number(hero,source_keys::get(source_keys::key_x))>=number(vehicle,source_keys::get(source_keys::key_x))&&number(hero,source_keys::get(source_keys::key__xscale))>0))invoke(env,self,flip,{hero_value});
    invoke(env,self,source_keys::get(source_keys::key_f_DropItem),{hero_value});write(hero,source_keys::get(source_keys::key_speed_toss_y),-double(tu_random::next_random()%10+10));write(hero,source_keys::get(source_keys::key_speed_toss_x),double(tu_random::next_random()%8+12));
    invoke(env,self,source_keys::get(source_keys::key_f_FX),{read(hero,source_keys::get(source_keys::key_x)),number(object(hero,source_keys::get(source_keys::key_body)),source_keys::get(source_keys::key__y))+number(hero,source_keys::get(source_keys::key_y)),int(std::floor(number(hero,source_keys::get(source_keys::key_y))))+7,"impact1"});invoke(env,self,source_keys::get(source_keys::key_f_CallJuggle1),{hero_value});
   }
   if(hit)invoke(env,self,sound,{});
  }
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 bool lookup_accel=true;
#ifndef PSP
 lookup_accel=!std::getenv("DNM_DISABLE_LOOKUP_ACCEL");
#endif
 if(lookup_accel&&function.m_start_pc==32515&&function.m_length==77&&function.m_body_fingerprint==10363626387468703778ULL&&fn.nargs==2){
  gc_ptr<as_object> actor=fn.arg(0).to_object();as_value compare=fn.arg(1),saved=env->m_global_register[1];
  if(read(actor,source_keys::get(source_keys::key_mask_pt))==compare){
   write(actor,source_keys::get(source_keys::key_mask_pt),as_value());
   invoke(env,object(actor,source_keys::get(source_keys::key_mask)),source_keys::get(source_keys::key_gotoAndStop),{1});
  }
  env->m_global_register[1]=saved;return true;
 }
 bool spawn_accel=true;
#ifndef PSP
 spawn_accel=!std::getenv("DNM_DISABLE_SPAWN_ACCEL");
#endif
 bool kid_spawn_accel=spawn_accel;
#ifndef PSP
 kid_spawn_accel=kid_spawn_accel&&!std::getenv("DNM_DISABLE_KID_SPAWN_ACCEL");
#endif
 // The measured dense soccer encounter spends most of its remaining script
 // time in these movement decisions. Preserve every move/collision call and
 // use the original Math functions for trigonometry and source rounding.
 unsigned soccer=0;
 if(fn.nargs==1){
  if(function.m_start_pc==75799&&function.m_length==856&&function.m_body_fingerprint==6614926656306855499ULL)soccer=1;
  if(function.m_start_pc==78963&&function.m_length==960&&function.m_body_fingerprint==9111082864838019926ULL)soccer=2;
  if(function.m_start_pc==78627&&function.m_length==312&&function.m_body_fingerprint==5424700705093343475ULL)soccer=3;
 }
#ifndef PSP
 if(std::getenv("DNM_DISABLE_SOCCER_ACCEL"))soccer=0;
#endif
 if(soccer){
  static const source_keys::Key success_x("success_x"),success_y("success_y"),north("north"),target_x("target_x"),target_y("target_y"),
   target_ball("target_ball"),target_goal("target_goal"),target_pt("target_pt"),default_x("default_x"),default_y("default_y"),
   top("top"),partner("partner"),last_x("last_x"),last_y("last_y"),flip("f_FlipChar"),move("f_MoveKid"),chase("f_SoccerChase"),
   hunt("f_HuntBall"),follow("f_SoccerFollow"),kick("f_SoccerKick"),rad2deg("f_rad2deg"),atan2_key("atan2"),cos_key("cos"),sin_key("sin");
  as_value actor_value=fn.arg(0),saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};
  gc_ptr<as_object> actor=actor_value.to_object();array<with_stack_entry> empty;
  auto R=[&](const source_keys::Key& key){return read(actor,key);};
  auto N=[&](const source_keys::Key& key){return number(actor,key);};
  auto W=[&](const source_keys::Key& key,const as_value& value){write(actor,key,value);};
  auto field=[&](){return object(self,source_keys::get(source_keys::key_soccerfield));};
  auto ball=[&](){return object(actor,target_ball);};
  auto animation=[&](const char* label){invoke(env,actor,source_keys::get(source_keys::key_gotoAndStop),{label});};
  if(soccer==1){
   if(R(success_y).to_bool()){
    if(N(source_keys::get(source_keys::key_x))<N(target_x)&&N(source_keys::get(source_keys::key__xscale))<0)invoke(env,self,flip,{actor_value});
    else if(N(source_keys::get(source_keys::key_x))>N(target_x)&&N(source_keys::get(source_keys::key__xscale))>0)invoke(env,self,flip,{actor_value});
   }
   if(R(success_x).to_bool()){
    if(N(source_keys::get(source_keys::key_y))<N(target_y)&&R(north).to_bool())W(north,false);
    else if(N(source_keys::get(source_keys::key_y))>N(target_y)&&!R(north).to_bool())W(north,true);
   }
   as_value tx=R(success_y)==as_value(false)?as_value(N(target_x)+(N(source_keys::get(source_keys::key__xscale))>0?300:-300)):R(target_x);env->set_local("target_x",tx);
   as_value ty=R(success_x)==as_value(false)?as_value(N(target_y)+(R(north).to_bool()?-300:300)):R(target_y);env->set_local("target_y",ty);
   auto math=env->get_variable("Math",empty);auto angle=invoke(env,math.to_object(),atan2_key,{N(source_keys::get(source_keys::key_y))-ty.to_number(),N(source_keys::get(source_keys::key_x))-tx.to_number()});
   auto dir=invoke(env,self,rad2deg,{angle});env->set_local("dir",dir);
   as_value speed=R(source_keys::get(source_keys::key_item))!=env->get_variable("NULL",empty)?-6:-5;env->set_local("temp_speed",speed);
   auto vx=speed.to_number()*invoke(env,env->get_variable("Math",empty).to_object(),cos_key,{env->get_variable("PI",empty).to_number()*dir.to_number()}).to_number();
   auto vy=speed.to_number()*invoke(env,env->get_variable("Math",empty).to_object(),sin_key,{env->get_variable("PI",empty).to_number()*dir.to_number()}).to_number();
   W(last_x,vx);W(last_y,vy);
   W(success_x,invoke(env,self,move,{actor_value,false,vx}));W(success_y,invoke(env,self,move,{actor_value,true,vy}));
   if(!R(success_x).to_bool()){
    W(success_x,invoke(env,self,move,{actor_value,false,vx}));
    if(!R(success_y).to_bool())W(success_y,invoke(env,self,move,{actor_value,true,vy}));
   }
  }else if(soccer==3){
   bool moving=false;
   if(std::fabs(N(source_keys::get(source_keys::key_x))-number(ball(),source_keys::get(source_keys::key_x)))>200){moving=true;W(target_x,read(ball(),source_keys::get(source_keys::key_x)));}
   else W(target_x,R(source_keys::get(source_keys::key_x)));
   if(std::fabs(N(source_keys::get(source_keys::key_y))-number(ball(),source_keys::get(source_keys::key_y)))>100){moving=true;W(target_y,read(ball(),source_keys::get(source_keys::key_y)));}
   else W(target_y,R(source_keys::get(source_keys::key_y)));
   if(moving){invoke(env,self,chase,{actor_value});animation("soccerwalk");}else animation("soccerwait");
  }else{
   if((read(self,source_keys::get(source_keys::key_score_left)).to_bool()&&R(target_goal)==read(self,source_keys::get(source_keys::key_goal_left)))||
      (read(self,source_keys::get(source_keys::key_score_right)).to_bool()&&R(target_goal)==read(self,source_keys::get(source_keys::key_goal_right)))){
    W(target_x,R(default_x));W(target_y,R(default_y));
    if(std::fabs(N(target_x)-N(source_keys::get(source_keys::key_x)))<50&&std::fabs(N(target_y)-N(source_keys::get(source_keys::key_y)))<50){
     if(N(source_keys::get(source_keys::key_x))>number(field(),source_keys::get(source_keys::key_x))){if(N(source_keys::get(source_keys::key__xscale))>0)invoke(env,self,flip,{actor_value});}
     else if(N(source_keys::get(source_keys::key__xscale))<0)invoke(env,self,flip,{actor_value});
     animation("soccerwait");
    }else{invoke(env,self,chase,{actor_value});animation("soccerwalk");}
   }else if(R(source_keys::get(source_keys::key_item))==env->get_variable("NULL",empty)){
    bool choose_hunt;
    if(number(ball(),source_keys::get(source_keys::key_y))<number(field(),source_keys::get(source_keys::key_y)))
     choose_hunt=R(top).to_bool()||!read(object(actor,partner),source_keys::get(source_keys::key_alive)).to_bool()||read(object(self,source_keys::get(source_keys::key_dude1)),source_keys::get(source_keys::key_grab_pointer))==R(partner);
    else choose_hunt=R(top)==as_value(false)||!read(object(actor,partner),source_keys::get(source_keys::key_alive)).to_bool()||read(object(self,source_keys::get(source_keys::key_dude1)),source_keys::get(source_keys::key_grab_pointer))==R(partner);
    invoke(env,self,choose_hunt?hunt:follow,{actor_value});
   }else{
    auto goal=object(actor,target_goal);
    if(number(goal,source_keys::get(source_keys::key_x))<number(field(),source_keys::get(source_keys::key_x)))W(target_x,number(goal,source_keys::get(source_keys::key_x))+250);
    else W(target_x,number(object(actor,target_goal),source_keys::get(source_keys::key_x))-250);
    if(N(source_keys::get(source_keys::key_y))<number(field(),source_keys::get(source_keys::key_y)))W(target_y,number(object(actor,target_goal),source_keys::get(source_keys::key_y))-10);
    else W(target_y,number(object(actor,target_goal),source_keys::get(source_keys::key_y))+10);
    W(target_pt,R(target_goal));invoke(env,self,chase,{actor_value});
    if(std::fabs(N(source_keys::get(source_keys::key_x))-N(target_x))<50&&std::fabs(N(source_keys::get(source_keys::key_y))-N(target_y))<100)invoke(env,self,kick,{actor_value});
   }
  }
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 // Authored enemy creation, including attachMovie, initialization, collision
 // helpers and their order. No enemy is constructed early or over several ticks.
 if(kid_spawn_accel&&function.m_start_pc==63883&&function.m_length==780&&function.m_body_fingerprint==16686002596385696143ULL&&fn.nargs==6){
  static const source_keys::Key current_shadow("current_shadow"),dm_current("kid_dm_current"),dm_default("kid_dm_default"),
   attach("attachMovie"),depth_val("f_DepthVal"),shirt_type("shirt_type"),hat_type("hat_type"),skin_type("skin_type"),name_key("name"),kid_type("kid_type"),init("f_KidInit"),check_spawn("f_CheckSpawn");
  as_value args[6]={fn.arg(0),fn.arg(1),fn.arg(2),fn.arg(3),fn.arg(4),fn.arg(5)};
  as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};array<with_stack_entry> empty;
  auto null_value=env->get_variable("NULL",empty);as_value kid_name=null_value,index=1,this_kid;
  while(index.to_number()<=number(self,source_keys::get(source_keys::key_total_kids))){
   if(indexed_read(self,3,index)==env->get_variable("NULL",empty)){
    std::string text="kid";text+=index.to_string();kid_name=text.c_str();this_kid=index;env->set_local("this_kid",this_kid);
    index=number(self,source_keys::get(source_keys::key_total_kids))+1;
   }
   index+=1;
  }
  if(kid_name==env->get_variable("NULL",empty)){
   auto total=read(self,source_keys::get(source_keys::key_total_kids));total+=1;write(self,source_keys::get(source_keys::key_total_kids),total);
   this_kid=read(self,source_keys::get(source_keys::key_total_kids));env->set_local("this_kid",this_kid);
   std::string text="kid";text+=read(self,source_keys::get(source_keys::key_total_kids)).to_string();kid_name=text.c_str();
  }
  std::string shadow_text="shadow";shadow_text+=read(self,current_shadow).to_string();as_value shadow_name=shadow_text.c_str();env->set_local("shadow_name",shadow_name);
  as_value kid_y=args[1];env->set_local("kid_y",kid_y);
  auto dm=read(self,dm_current);dm+=2;write(self,dm_current,dm);
  as_value kid_depth_mod=number(self,dm_current)+number(self,dm_default);env->set_local("kid_depth_mod",kid_depth_mod);
  auto depth=invoke(env,self,depth_val,{kid_depth_mod,kid_y});invoke(env,self,attach,{"kid",kid_name,depth});
  invoke(env,self,attach,{"shadow",shadow_name,this_kid.to_number()-200});
  as_value actor_value=indexed_read(self,3,this_kid);gc_ptr<as_object> actor=actor_value.to_object();
  write(actor,source_keys::get(source_keys::key_depth_mod),kid_depth_mod);
  write(actor,source_keys::get(source_keys::key_x),args[0]);write(actor,source_keys::get(source_keys::key_y),kid_y);
  write(actor,source_keys::get(source_keys::key__x),read(actor,source_keys::get(source_keys::key_x)));
  write(actor,source_keys::get(source_keys::key__y),read(actor,source_keys::get(source_keys::key_y)));
  write(actor,source_keys::get(source_keys::key_shadow_pt),indexed_read(self,4,read(self,current_shadow)));
  auto shadow_index=read(self,current_shadow);shadow_index+=1;write(self,current_shadow,shadow_index);
  write(object(actor,source_keys::get(source_keys::key_shadow_pt)),source_keys::get(source_keys::key__x),read(actor,source_keys::get(source_keys::key_x)));
  write(object(actor,source_keys::get(source_keys::key_shadow_pt)),source_keys::get(source_keys::key__y),read(actor,source_keys::get(source_keys::key_y)));
  write(actor,shirt_type,args[2]);write(actor,hat_type,args[3]);write(actor,skin_type,args[5]);write(actor,name_key,args[4]);
  write(actor,source_keys::get(source_keys::key_item),env->get_variable("NULL",empty));write(actor,kid_type,1);
  if(number(actor,source_keys::get(source_keys::key_x))>number(object(self,source_keys::get(source_keys::key_dude1)),source_keys::get(source_keys::key_x))&&number(actor,source_keys::get(source_keys::key__xscale))>0)
   invoke(env,self,source_keys::Key("f_FlipChar"),{actor_value});
  else if(number(actor,source_keys::get(source_keys::key_x))<number(object(self,source_keys::get(source_keys::key_dude1)),source_keys::get(source_keys::key_x))&&number(actor,source_keys::get(source_keys::key__xscale))<0)
   invoke(env,self,source_keys::Key("f_FlipChar"),{actor_value});
  invoke(env,self,init,{actor_value});invoke(env,self,source_keys::get(source_keys::key_f_Depth),{actor_value,read(actor,source_keys::get(source_keys::key_y))});invoke(env,self,check_spawn,{actor_value});
  env->m_global_register[0]=actor_value;for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];*fn.result=actor_value;return true;
 }
 if(kid_spawn_accel&&function.m_start_pc==67467&&function.m_length==1127&&function.m_body_fingerprint==8112757160102920819ULL&&fn.nargs==0){
  static const source_keys::Key spawn("f_SpawnKid"),set_xy("f_SetXY"),latin("color_latin"),brown("color_brown"),beige("color_beige"),
   kid_type("kid_type"),target_ball("target_ball"),target_goal("target_goal"),top("top"),default_x("default_x"),default_y("default_y"),partner("partner"),kills_goal("kills_goal");
  as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};
  auto field=[&](){return object(self,source_keys::get(source_keys::key_soccerfield));};
  auto make=[&](double dx,double dy,int shirt,int hat,int name,const source_keys::Key& color){
   as_value y=number(field(),source_keys::get(source_keys::key__y))+dy;env->set_local("spawn_y",y);
   as_value x=number(field(),source_keys::get(source_keys::key__x))+dx;env->set_local("spawn_x",x);
   return invoke(env,self,spawn,{x,y,shirt,hat,name,read(self,color)});
  };
  auto setup=[&](const as_value& value,bool right,bool is_top,double dx,double dy){
   gc_ptr<as_object> kid=value.to_object();write(kid,kid_type,3);invoke(env,kid,source_keys::get(source_keys::key_gotoAndStop),{"wait"});
   if(right)write(kid,source_keys::get(source_keys::key__xscale),number(kid,source_keys::get(source_keys::key__xscale))*-1);
   write(kid,target_ball,read(self,source_keys::get(source_keys::key_soccerball)));
   write(kid,target_goal,read(self,source_keys::get(right?source_keys::key_goal_right:source_keys::key_goal_left)));
   write(kid,top,is_top);write(kid,default_x,number(field(),source_keys::get(source_keys::key__x))+dx);write(kid,default_y,number(field(),source_keys::get(source_keys::key__y))+dy);
  };
  auto kid1=make(100,-75,9,3,66,latin);env->set_local("u_kid1",kid1);setup(kid1,false,true,140,-75);
  invoke(env,self,set_xy,{read(self,source_keys::get(source_keys::key_soccerball)),read(field(),source_keys::get(source_keys::key__x)),number(field(),source_keys::get(source_keys::key__y))+5});
  invoke(env,self,source_keys::get(source_keys::key_f_StaticRange),{});
  auto kid2=make(50,0,9,1,67,brown);setup(kid2,false,false,150,75);
  auto kid3=make(-100,-70,45,17,36,beige);setup(kid3,true,true,-130,-75);
  auto kid4=make(-120,75,48,1,37,beige);setup(kid4,true,false,-160,75);
  write(kid1.to_object(),partner,kid2);write(kid2.to_object(),partner,kid1);write(kid3.to_object(),partner,kid4);write(kid4.to_object(),partner,kid3);
  auto kills=read(self,kills_goal);kills+=4;write(self,kills_goal,kills);
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 if(spawn_accel&&function.m_start_pc==86733&&function.m_length==442&&function.m_body_fingerprint==6511027278010129998ULL&&fn.nargs==0){
  static const source_keys::Key static_index("static_index"),object_index("object_index"),active("active");
  as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};array<with_stack_entry> empty;SourceIndex counter(env,empty);
  env->set_local("u_min",read(self,static_index));env->set_local("u_max",1);counter.set(1);
  while(counter.get().to_number()<=number(self,static_index)){
   auto index=counter.get();gc_ptr<as_object> item=indexed_read(self,0,index).to_object();
   if(number(item,source_keys::get(source_keys::key_x))>number(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_x))&&
      number(item,source_keys::get(source_keys::key_x))<number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_x))){
    if(index.to_number()<env->get_variable("u_min",empty).to_number())env->set_local("u_min",index);
    if(index.to_number()>env->get_variable("u_max",empty).to_number())env->set_local("u_max",index);
   }
   auto i=counter.get();i+=1;counter.set(i);
  }
  write(self,source_keys::get(source_keys::key_statics_min),env->get_variable("u_min",empty));
  write(self,source_keys::get(source_keys::key_total_statics),env->get_variable("u_max",empty));
  env->set_local("u_min",read(self,object_index));write(self,source_keys::get(source_keys::key_total_objects),0);counter.set(1);
  while(counter.get().to_number()<=number(self,object_index)){
   auto index=counter.get();auto value=indexed_read(self,1,index);gc_ptr<as_object> item=value.to_object();
   if(number(item,source_keys::get(source_keys::key__x))>number(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_x))&&
      number(item,source_keys::get(source_keys::key__x))<number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_x))&&read(item,active).to_bool()){
    auto total=read(self,source_keys::get(source_keys::key_total_objects));total+=1;write(self,source_keys::get(source_keys::key_total_objects),total);
    indexed_write(self,2,read(self,source_keys::get(source_keys::key_total_objects)),value);
   }
   auto i=counter.get();i+=1;counter.set(i);
  }
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 if(spawn_accel&&function.m_start_pc==36087&&function.m_length==204&&function.m_body_fingerprint==16790413979621090585ULL&&fn.nargs==1){
  static const source_keys::Key check("f_CheckSpawnObject");
  as_value actor=fn.arg(0),saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};array<with_stack_entry> empty;SourceIndex counter(env,empty);
  counter.set(1);
  while(counter.get().to_number()<=number(self,source_keys::get(source_keys::key_total_objects))){
   invoke(env,self,check,{actor,indexed_read(self,2,counter.get())});counter.invalidate();
   auto i=counter.get();i+=1;counter.set(i);
  }
  counter.set(read(self,source_keys::get(source_keys::key_statics_min)));
  while(counter.get().to_number()<=number(self,source_keys::get(source_keys::key_total_statics))){
   invoke(env,self,check,{actor,indexed_read(self,0,counter.get())});counter.invalidate();
   auto i=counter.get();i+=1;counter.set(i);
  }
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 if(spawn_accel&&function.m_start_pc==35759&&function.m_length==303&&function.m_body_fingerprint==6751691184845219227ULL&&fn.nargs==2){
  static const source_keys::Key set_xy("f_SetXY");
  as_value actor_value=fn.arg(0),saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};
  gc_ptr<as_object> actor=actor_value.to_object(),item=fn.arg(1).to_object();
  double x=number(item,source_keys::get(source_keys::key_x))+number(object(item,source_keys::get(source_keys::key_zone)),source_keys::get(source_keys::key__x));env->set_local("x",x);
  double y=number(item,source_keys::get(source_keys::key_y))+number(object(item,source_keys::get(source_keys::key_zone)),source_keys::get(source_keys::key__y));
  double height,width;auto zone=object(item,source_keys::get(source_keys::key_zone));
  if(!collision_dimensions(zone,height,width)){height=number(zone,source_keys::get(source_keys::key__height));width=number(zone,source_keys::get(source_keys::key__width));}
  double h=height*(number(item,source_keys::get(source_keys::key__yscale))/100);env->set_local("h",h);
  double w=width/2*(number(item,source_keys::get(source_keys::key__xscale))/100);env->set_local("w",w);
  if(std::fabs(number(actor,source_keys::get(source_keys::key_x))-x)<w&&number(actor,source_keys::get(source_keys::key_y))<y&&number(actor,source_keys::get(source_keys::key_y))>y-h)
   invoke(env,self,set_xy,{actor_value,read(actor,source_keys::get(source_keys::key_x)),y+2});
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 // Preserve the authored per-direction timers and call order. Movement,
 // collisions, dash speed and sand effects still use the original helpers.
 if(function.m_start_pc==28544 && function.m_length==1731 && function.m_body_fingerprint==3699498192785585706ULL && fn.nargs==1
#ifndef PSP
    && !std::getenv("DNM_DISABLE_WALK_ACCEL")
#endif
 ){
  static const source_keys::Key walking("walking"),holding("holding"),is_down("isDown"),
   button_left("button_left"),button_right("button_right"),button_up("button_up"),button_down("button_down"),
   left_timer("left_timer"),right_timer("right_timer"),up_timer("up_timer"),down_timer("down_timer"),
   left_last("left_last"),right_last("right_last"),left_last2("left_last2"),right_last2("right_last2"),
   speed_x("speed_x"),speed_y("speed_y"),dash_timer("g_dash_timer"),insand("insand"),sandbox("sandbox"),sand_timer("sand_timer"),
   flip("f_FlipChar"),move("f_MoveChar");
  gc_ptr<as_object> actor=fn.arg(0).to_object();as_value actor_value=fn.arg(0);
  as_value saved1=env->m_global_register[1],saved2=env->m_global_register[2];array<with_stack_entry> empty;
  auto down=[&](const source_keys::Key& button){return invoke(env,env->get_variable("Key",empty).to_object(),is_down,{read(actor,button)}).to_bool();};
  write(actor,walking,false);write(actor,source_keys::get(source_keys::key_dashing),false);
  auto horizontal_timer=[&](const source_keys::Key& button,const source_keys::Key& timer,const source_keys::Key& last,const source_keys::Key& last2){
   if(down(button)){
    if(number(actor,timer)<=0){write(actor,last2,read(actor,timer));write(actor,timer,1);}
    else write(actor,timer,number(actor,timer)+1);
   }else if(number(actor,timer)>0){write(actor,last,read(actor,timer));write(actor,timer,0);}
   else write(actor,timer,number(actor,timer)-1);
  };
  horizontal_timer(button_left,left_timer,left_last,left_last2);
  horizontal_timer(button_right,right_timer,right_last,right_last2);
  auto horizontal=[&](bool left,const source_keys::Key& last,const source_keys::Key& last2){
   if(left?number(actor,source_keys::get(source_keys::key__xscale))>0:number(actor,source_keys::get(source_keys::key__xscale))<0)
    invoke(env,self,flip,{actor_value});
   double speed;
   if(read(actor,holding).to_bool())speed=number(actor,speed_x)/2;
   else if(number(actor,last)<number(self,dash_timer) && number(actor,last2)<number(self,dash_timer)){
    speed=number(actor,speed_x)*2;write(actor,source_keys::get(source_keys::key_dashing),true);
   }else speed=number(actor,speed_x);
   if(number(actor,source_keys::get(source_keys::key_rage))>=number(actor,source_keys::get(source_keys::key_rage_goal)))speed*=1.5;
   double directed=left?-speed:speed;
   invoke(env,self,move,{actor_value,false,directed});
   if(read(actor,source_keys::get(source_keys::key_diagonalhit)).to_bool())invoke(env,self,move,{actor_value,true,directed});
   write(actor,walking,true);
  };
  if(number(actor,left_timer)>0 && number(actor,left_timer)>number(actor,right_timer))horizontal(true,left_last,left_last2);
  if(number(actor,right_timer)>0 && number(actor,right_timer)>number(actor,left_timer))horizontal(false,right_last,right_last2);
  write(actor,up_timer,down(button_up)?number(actor,up_timer)+1:0);
  write(actor,down_timer,down(button_down)?number(actor,down_timer)+1:0);
  double speed=read(actor,holding).to_bool()?number(actor,speed_y)/2:number(actor,speed_y);
  if(number(actor,up_timer)>0 && number(actor,up_timer)>number(actor,down_timer)){
   if(number(actor,source_keys::get(source_keys::key_rage))>=number(actor,source_keys::get(source_keys::key_rage_goal)))speed*=1.5;
   invoke(env,self,move,{actor_value,true,-speed});
   if(read(actor,source_keys::get(source_keys::key_diagonalhit)).to_bool())invoke(env,self,move,{actor_value,false,-speed});
   write(actor,walking,true);
  }
  if(number(actor,down_timer)>0 && number(actor,down_timer)>number(actor,up_timer)){
   if(number(actor,source_keys::get(source_keys::key_rage))>=number(actor,source_keys::get(source_keys::key_rage_goal)))speed*=1.5;
   invoke(env,self,move,{actor_value,true,speed});
   if(read(actor,source_keys::get(source_keys::key_diagonalhit)).to_bool())invoke(env,self,move,{actor_value,false,speed});
   write(actor,walking,true);
  }
  if(read(actor,insand).to_bool() && read(actor,walking).to_bool() && number(object(self,sandbox),sand_timer)<=0){
   write(object(self,sandbox),sand_timer,5);
   // Match the retained interpreter's ActionToInteger opcode0x18 exactly.
   int depth=int(std::floor(number(actor,source_keys::get(source_keys::key_y))));
   invoke(env,self,source_keys::get(source_keys::key_f_FX),{read(actor,source_keys::get(source_keys::key_x)),number(actor,source_keys::get(source_keys::key_y))+10,depth+1,"sand",read(actor,source_keys::get(source_keys::key__xscale))});
  }
  env->m_global_register[1]=saved1;env->m_global_register[2]=saved2;return true;
 }
 // This original sandbox callback is hot while the opening terrain is active. Translate
 // its exact body without caching live properties, changing geometry tests,
 // removing the timer update, or skipping its authored sound/timeline calls.
 if(function.m_start_pc==84377 && function.m_length==750 && function.m_body_fingerprint==2491496232791573453ULL && fn.nargs==1
#ifndef PSP
    && !std::getenv("DNM_DISABLE_SANDBOX_ACCEL")
#endif
 ){
  static const source_keys::Key insand("insand"),sandbox("sandbox"),sand_timer("sand_timer"),
   crushed("crushed"),top_left("topleft"),bottom_left("bottomleft"),
   top_right("topright"),bottom_right("bottomright"),fart("s_Fart"),start("start"),frame("_currentframe");
  gc_ptr<as_object> zone=fn.arg(0).to_object();
  as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};
  auto hero=[&](){return object(self,source_keys::get(source_keys::key_dude1));};
  write(hero(),insand,false);
  write(object(self,sandbox),sand_timer,number(object(self,sandbox),sand_timer)-1);
  if(number(zone,source_keys::get(source_keys::key__x))+number(self,source_keys::get(source_keys::key__X)) < -200){
   invoke(env,zone,source_keys::get(source_keys::key_gotoAndStop),{3});
  }else{
   if(number(zone,frame)==1 &&
      std::fabs(number(hero(),source_keys::get(source_keys::key_y))-number(zone,source_keys::get(source_keys::key_y)))<10 &&
      std::fabs(number(hero(),source_keys::get(source_keys::key_x))-number(zone,source_keys::get(source_keys::key_x)))<80){
    invoke(env,object(self,fart),start,{0,0});write(zone,crushed,true);
    invoke(env,zone,source_keys::get(source_keys::key_gotoAndStop),{2});
   }
   double left_top=number(object(zone,top_left),source_keys::get(source_keys::key__x))+number(zone,source_keys::get(source_keys::key_x));
   env->set_local("u_topleft",left_top);
   double left_bottom=number(object(zone,bottom_left),source_keys::get(source_keys::key__x))+number(zone,source_keys::get(source_keys::key_x));
   double top=number(object(zone,top_left),source_keys::get(source_keys::key__y))+number(zone,source_keys::get(source_keys::key_y));
   env->set_local("u_top",top);
   double bottom=number(object(zone,bottom_left),source_keys::get(source_keys::key__y))+number(zone,source_keys::get(source_keys::key_y));
   env->set_local("u_bottom",bottom);
   double right_top=number(object(zone,top_right),source_keys::get(source_keys::key__x))+number(zone,source_keys::get(source_keys::key_x));
   env->set_local("u_topright",right_top);
   double right_bottom=number(object(zone,bottom_right),source_keys::get(source_keys::key__x))+number(zone,source_keys::get(source_keys::key_x));
   if(number(hero(),source_keys::get(source_keys::key_y))>top && number(hero(),source_keys::get(source_keys::key_y))<bottom &&
      number(hero(),source_keys::get(source_keys::key_x))>=left_bottom && number(hero(),source_keys::get(source_keys::key_x))<=right_top){
    double percent=1-(number(hero(),source_keys::get(source_keys::key_y))-top)/100;
    env->set_local("u_percent",percent);
    double left=left_bottom+(left_top-left_bottom)*percent;env->set_local("u_left",left);
    double right=right_bottom+(right_top-right_bottom)*percent;env->set_local("u_right",right);
    if(number(hero(),source_keys::get(source_keys::key_x))>=left && number(hero(),source_keys::get(source_keys::key_x))<=right)write(hero(),insand,true);
   }
  }
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 if(function.m_start_pc==32515 && function.m_length==77 && function.m_body_fingerprint==10363626387468703778ULL && fn.nargs==2){
  as_value actor_value=fn.arg(0),other=fn.arg(1);auto actor=actor_value.to_object();as_value saved=env->m_global_register[1];
  if(read(actor,source_keys::get(source_keys::key_mask_pt))==other){write(actor,source_keys::get(source_keys::key_mask_pt),as_value());invoke(env,object(actor,source_keys::get(source_keys::key_mask)),source_keys::get(source_keys::key_gotoAndStop),{1});}
  env->m_global_register[1]=saved;return true;
 }
 if(function.m_start_pc==35528 && function.m_length==191 && function.m_body_fingerprint==9364094992853636204ULL && fn.nargs==3){
  as_value actor=fn.arg(0),vert=fn.arg(1),speed=fn.arg(2);as_value saved1=env->m_global_register[1],saved2=env->m_global_register[2];
  bool hit=invoke(env,self,source_keys::get(source_keys::key_f_HitWallGroup),{actor,vert,speed,read(self,source_keys::get(source_keys::key_total_objects)),1,"object_pt"}).to_bool();
  if(!hit)hit=invoke(env,self,source_keys::get(source_keys::key_f_HitWallGroup),{actor,vert,speed,read(self,source_keys::get(source_keys::key_total_statics)),read(self,source_keys::get(source_keys::key_statics_min)),"static"}).to_bool();
  write(actor.to_object(),source_keys::get(source_keys::key_hitwall),hit);env->m_global_register[0]=hit;env->m_global_register[1]=saved1;env->m_global_register[2]=saved2;*fn.result=hit;return true;
 }
 if(function.m_start_pc==75274 && function.m_length==499 && function.m_body_fingerprint==6300594194946868343ULL){
  auto ball=object(self,source_keys::get(source_keys::key_soccerball));
  if(number(ball,source_keys::get(source_keys::key_x))<number(object(self,source_keys::get(source_keys::key_soccerfield)),source_keys::get(source_keys::key_x))){if(read(self,source_keys::get(source_keys::key_score_right)).to_bool())write(self,source_keys::get(source_keys::key_score_right),false);}
  else if(read(self,source_keys::get(source_keys::key_score_left)).to_bool())write(self,source_keys::get(source_keys::key_score_left),false);
  if(std::fabs(number(ball,source_keys::get(source_keys::key_x))-number(object(self,source_keys::get(source_keys::key_goal_left)),source_keys::get(source_keys::key_x)))<50){
   if(number(ball,source_keys::get(source_keys::key_y))<number(object(self,source_keys::get(source_keys::key_soccer1)),source_keys::get(source_keys::key_y)) && number(ball,source_keys::get(source_keys::key_y))>number(object(self,source_keys::get(source_keys::key_soccer3)),source_keys::get(source_keys::key_y)) && !read(self,source_keys::get(source_keys::key_ball_left)).to_bool() && read(ball,source_keys::get(source_keys::key_owner))==as_value((as_object*)nullptr)){
    write(self,source_keys::get(source_keys::key_score_left),true);write(self,source_keys::get(source_keys::key_ball_left),true);write(self,source_keys::get(source_keys::key_score_team2),number(self,source_keys::get(source_keys::key_score_team2))+1);
   }
  }else{
   write(self,source_keys::get(source_keys::key_ball_left),false);
   if(std::fabs(number(ball,source_keys::get(source_keys::key_x))-number(object(self,source_keys::get(source_keys::key_goal_right)),source_keys::get(source_keys::key_x)))<50){
    if(number(ball,source_keys::get(source_keys::key_y))<number(object(self,source_keys::get(source_keys::key_soccer4)),source_keys::get(source_keys::key_y)) && number(ball,source_keys::get(source_keys::key_y))>number(object(self,source_keys::get(source_keys::key_soccer6)),source_keys::get(source_keys::key_y)) && !read(self,source_keys::get(source_keys::key_ball_right)).to_bool() && read(ball,source_keys::get(source_keys::key_owner))==as_value((as_object*)nullptr)){
     write(self,source_keys::get(source_keys::key_score_right),true);write(self,source_keys::get(source_keys::key_ball_right),true);write(self,source_keys::get(source_keys::key_score_team1),number(self,source_keys::get(source_keys::key_score_team1))+1);
    }
   }else write(self,source_keys::get(source_keys::key_ball_right),false);
  }
  return true;
 }
 if(function.m_start_pc==38120 && function.m_length==102 && function.m_body_fingerprint==11109071345880683083ULL && fn.nargs==2){
  as_value actor_value=fn.arg(0),depth=fn.arg(1),saved=env->m_global_register[1];auto actor=actor_value.to_object();
  write(actor,source_keys::get(source_keys::key_current_depth),depth.to_number()*1000+number(actor,source_keys::get(source_keys::key_depth_mod)));invoke(env,actor,source_keys::get(source_keys::key_swapDepths),{read(actor,source_keys::get(source_keys::key_current_depth))});env->m_global_register[1]=saved;return true;
 }
 if(function.m_start_pc==61292 && function.m_length==656 && function.m_body_fingerprint==13421220374301253646ULL && fn.nargs==1){
  auto actor=object(self,source_keys::get(source_keys::key_dude1));if(!actor || actor->m_watch || !fn.arg(0).is_number())return false;
  double down=fn.arg(0).to_number(),rage=number(actor,source_keys::get(source_keys::key_rage))-down,goal=number(actor,source_keys::get(source_keys::key_rage_goal));
  if(rage<goal && rage+down>=goal)rage=0;if(rage<0)rage=0;
  double ratio=rage/goal*100;if(ratio<0)ratio=0;else if(ratio>100)ratio=100;
  if(ratio==100)return false; // Retain original RNG/FX evaluation order intact.
  as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};
  write(actor,source_keys::get(source_keys::key_rage),rage);auto hud=object(fn.get_root()->get_root_movie(),source_keys::get(source_keys::key_hud));write(object(hud,source_keys::get(source_keys::key_ragebar)),source_keys::get(source_keys::key__xscale),ratio);invoke(env,object(hud,source_keys::get(source_keys::key_ragebg)),source_keys::get(source_keys::key_gotoAndStop),{1});
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 if(function.m_start_pc==41744 && function.m_length==473 && function.m_body_fingerprint==17875612170526721716ULL && fn.nargs==3){
  as_value actor_value=fn.arg(0),vert=fn.arg(1),speed_value=fn.arg(2);auto actor=actor_value.to_object();double speed=speed_value.to_number();
  as_value saved1=env->m_global_register[1],saved2=env->m_global_register[2];array<with_stack_entry> empty;
  env->set_variable("return_val",true,empty);write(actor,source_keys::get(source_keys::key_bounds),false);
  if(invoke(env,self,source_keys::get(source_keys::key_f_HitWall),{actor_value,vert,speed}).to_bool()){speed=0;env->set_variable("return_val",false,empty);}
  if(vert.to_bool()){write(actor,source_keys::get(source_keys::key_y),number(actor,source_keys::get(source_keys::key_y))+speed);invoke(env,self,source_keys::get(source_keys::key_f_Depth),{actor_value,read(actor,source_keys::get(source_keys::key_y))});write(actor,source_keys::get(source_keys::key__y),read(actor,source_keys::get(source_keys::key_y)));}
  else{write(actor,source_keys::get(source_keys::key_x),number(actor,source_keys::get(source_keys::key_x))+speed);write(actor,source_keys::get(source_keys::key__x),read(actor,source_keys::get(source_keys::key_x)));if(number(actor,source_keys::get(source_keys::key_x))>number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_x))){if(speed>0)write(actor,source_keys::get(source_keys::key_bounds),true);}else if(number(actor,source_keys::get(source_keys::key_x))<number(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_x))){if(speed<0)write(actor,source_keys::get(source_keys::key_bounds),true);}}
  if(read(actor,source_keys::get(source_keys::key_mask_pt))!=as_value())invoke(env,self,source_keys::get(source_keys::key_f_Mask),{actor_value});
  write(object(actor,source_keys::get(source_keys::key_shadow_pt)),source_keys::get(source_keys::key__x),read(actor,source_keys::get(source_keys::key_x)));write(object(actor,source_keys::get(source_keys::key_shadow_pt)),source_keys::get(source_keys::key__y),read(actor,source_keys::get(source_keys::key_y)));
  if(read(actor,source_keys::get(source_keys::key_item)).to_bool())invoke(env,self,source_keys::get(source_keys::key_f_MoveItem),{actor_value,read(actor,source_keys::get(source_keys::key_item)),speed});
  auto result=env->get_variable("return_val",empty);env->m_global_register[0]=result;env->m_global_register[1]=saved1;env->m_global_register[2]=saved2;*fn.result=result;return true;
 }
 if(function.m_start_pc==36928 && function.m_length==1100 && function.m_body_fingerprint==5427562851917725250ULL && fn.nargs==3){
  as_value actor_value=fn.arg(0),vert_value=fn.arg(1),speed_value=fn.arg(2);auto actor=actor_value.to_object();bool vert=vert_value.to_bool();double speed=speed_value.to_number();
  as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};
  write(actor,source_keys::get(source_keys::key_bounds),false);write(actor,source_keys::get(source_keys::key_diagonalhit),false);
  if(vert){
   if(number(actor,source_keys::get(source_keys::key_y))+speed>number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_y)))speed=number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_y))-number(actor,source_keys::get(source_keys::key_y))+speed;
   else if(number(actor,source_keys::get(source_keys::key_y))+speed<number(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_y)))speed=number(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_y))-number(actor,source_keys::get(source_keys::key_y))+speed;
   if(invoke(env,self,source_keys::get(source_keys::key_f_HitWall),{actor_value,vert_value,speed}).to_bool())speed=read(actor,source_keys::get(source_keys::key_diagonalhit)).to_bool()?number(actor,source_keys::get(source_keys::key_diagonal_speed)):0;
   write(actor,source_keys::get(source_keys::key_y),number(actor,source_keys::get(source_keys::key_y))+speed);invoke(env,self,source_keys::get(source_keys::key_f_Depth),{actor_value,read(actor,source_keys::get(source_keys::key_y))});
  }else{
   if(invoke(env,self,source_keys::get(source_keys::key_f_HitWall),{actor_value,vert_value,speed}).to_bool())speed=read(actor,source_keys::get(source_keys::key_diagonalhit)).to_bool()?number(actor,source_keys::get(source_keys::key_diagonal_speed)):0;
   if(number(actor,source_keys::get(source_keys::key_x))+speed<number(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_x)) && number(actor,source_keys::get(source_keys::key_x))+speed>number(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_x)))write(actor,source_keys::get(source_keys::key_x),number(actor,source_keys::get(source_keys::key_x))+speed);
   else{write(actor,source_keys::get(source_keys::key_bounds),true);speed=0;}
  }
  write(actor,source_keys::get(source_keys::key__x),read(actor,source_keys::get(source_keys::key_x)));write(actor,source_keys::get(source_keys::key__y),read(actor,source_keys::get(source_keys::key_y)));
  if(read(actor,source_keys::get(source_keys::key_mask_pt))!=as_value())invoke(env,self,source_keys::get(source_keys::key_f_Mask),{actor_value});
  if(read(actor,source_keys::get(source_keys::key_item)).to_bool())invoke(env,self,source_keys::get(source_keys::key_f_MoveItem),{actor_value,read(actor,source_keys::get(source_keys::key_item)),speed});
  if(read(actor,source_keys::get(source_keys::key_scroller)).to_bool()){
   if(vert){if((speed>0 && number(actor,source_keys::get(source_keys::key_y))+number(self,source_keys::get(source_keys::key_game_y))<number(self,source_keys::get(source_keys::key_screen_height))/2) || (speed<0 && number(actor,source_keys::get(source_keys::key_y))+number(self,source_keys::get(source_keys::key_game_y))>number(self,source_keys::get(source_keys::key_screen_height))/2))speed*=(number(self,source_keys::get(source_keys::key_screen_height))/2-std::fabs(number(actor,source_keys::get(source_keys::key_y))+number(self,source_keys::get(source_keys::key_game_y))-number(self,source_keys::get(source_keys::key_screen_height))/2))/(number(self,source_keys::get(source_keys::key_screen_height))/2);}
   else if((speed>0 && number(actor,source_keys::get(source_keys::key_x))+number(self,source_keys::get(source_keys::key_game_x))<number(self,source_keys::get(source_keys::key_screen_width))/2) || (speed<0 && number(actor,source_keys::get(source_keys::key_x))+number(self,source_keys::get(source_keys::key_game_x))>number(self,source_keys::get(source_keys::key_screen_width))/2))speed*=(number(self,source_keys::get(source_keys::key_screen_width))/2-std::fabs(number(actor,source_keys::get(source_keys::key_x))+number(self,source_keys::get(source_keys::key_game_x))-number(self,source_keys::get(source_keys::key_screen_width))/2))/(number(self,source_keys::get(source_keys::key_screen_width))/2);
   invoke(env,self,source_keys::get(source_keys::key_f_Scroll),{speed,vert_value});
  }
  write(object(actor,source_keys::get(source_keys::key_shadow_pt)),source_keys::get(source_keys::key__x),read(actor,source_keys::get(source_keys::key_x)));write(object(actor,source_keys::get(source_keys::key_shadow_pt)),source_keys::get(source_keys::key__y),read(actor,source_keys::get(source_keys::key_y)));for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 if(function.m_start_pc==39222 && function.m_length==1014 && function.m_body_fingerprint==16330909916403295052ULL && fn.nargs==2){
  double speed=fn.arg(0).to_number();bool vert=fn.arg(1).to_bool();as_value saved=env->m_global_register[1];
  auto edge=[&](const char* object_name,const char* axis){return number(object(self,object_name),axis);};
  if(vert){
   if(edge("limit_topleft","y")-speed+number(self,source_keys::get(source_keys::key_game_y))<number(self,source_keys::get(source_keys::key_screen_height))-100 && edge("limit_bottomright","y")-speed+number(self,source_keys::get(source_keys::key_game_y))>number(self,source_keys::get(source_keys::key_screen_height))){
    write(self,source_keys::get(source_keys::key_game_y),number(self,source_keys::get(source_keys::key_game_y))-speed);write(self,source_keys::get(source_keys::key__Y),read(self,source_keys::get(source_keys::key_game_y)));write(object(self,source_keys::get(source_keys::key_bg0)),source_keys::get(source_keys::key__y),number(object(self,source_keys::get(source_keys::key_bg0)),source_keys::get(source_keys::key__y))-speed*.25);write(object(self,source_keys::get(source_keys::key_bg1)),source_keys::get(source_keys::key__y),number(object(self,source_keys::get(source_keys::key_bg1)),source_keys::get(source_keys::key__y))+speed/2);write(object(self,source_keys::get(source_keys::key_bg2)),source_keys::get(source_keys::key__y),number(object(self,source_keys::get(source_keys::key_bg2)),source_keys::get(source_keys::key__y))+speed*.65);
   }
  }else if(edge("limit_topleft","x")-speed+number(self,source_keys::get(source_keys::key_game_x))<0){
   if(edge("limit_bottomright","x")-speed+number(self,source_keys::get(source_keys::key_game_x))>number(self,source_keys::get(source_keys::key_screen_width))){
    write(self,source_keys::get(source_keys::key_game_x),number(self,source_keys::get(source_keys::key_game_x))-speed);write(self,source_keys::get(source_keys::key__X),read(self,source_keys::get(source_keys::key_game_x)));write(object(self,source_keys::get(source_keys::key_bg0)),source_keys::get(source_keys::key__x),number(object(self,source_keys::get(source_keys::key_bg0)),source_keys::get(source_keys::key__x))-speed*.25);write(object(self,source_keys::get(source_keys::key_bg1)),source_keys::get(source_keys::key__x),number(object(self,source_keys::get(source_keys::key_bg1)),source_keys::get(source_keys::key__x))+speed/2);write(object(self,source_keys::get(source_keys::key_bg2)),source_keys::get(source_keys::key__x),number(object(self,source_keys::get(source_keys::key_bg2)),source_keys::get(source_keys::key__x))+speed*.65);
   }else if(read(self,source_keys::get(source_keys::key_checkpoint_x))!=read(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_x))){
    write(self,source_keys::get(source_keys::key_checkpoint_x),read(object(self,source_keys::get(source_keys::key_limit_bottomright)),source_keys::get(source_keys::key_x)));invoke(env,self,source_keys::get(source_keys::key_f_StaticRange),{});
    double checkpoint=number(self,source_keys::get(source_keys::key_checkpoint));
    if(checkpoint==2)invoke(env,self,source_keys::get(source_keys::key_f_KidGroup3),{});
    else if(checkpoint==5)invoke(env,self,source_keys::get(source_keys::key_f_KidGroup4),{});
    else if(checkpoint==6)invoke(env,self,source_keys::get(source_keys::key_f_StopCarScene1),{});
    else if(checkpoint==7){invoke(env,self,source_keys::get(source_keys::key_f_KidGroup7),{});invoke(env,object(self,source_keys::get(source_keys::key_bballhoop)),source_keys::get(source_keys::key_gotoAndStop),{"off"});}
    else if(checkpoint==8)invoke(env,self,source_keys::get(source_keys::key_f_KidGroup11),{});
    else if(checkpoint==9)invoke(env,self,source_keys::get(source_keys::key_f_KidGroup9),{});
   }
   if(edge("limit_topleft","x")+number(self,source_keys::get(source_keys::key_game_x))<0 && edge("limit_topleft","x")<number(self,source_keys::get(source_keys::key_new_limit_topleft))){write(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_x),-number(self,source_keys::get(source_keys::key_game_x)));write(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key__x),read(object(self,source_keys::get(source_keys::key_limit_topleft)),source_keys::get(source_keys::key_x)));}
  }
  env->m_global_register[1]=saved;return true;
 }
 if(function.m_start_pc==21699 && function.m_length==777 && function.m_body_fingerprint==15781807099813371097ULL && fn.nargs==1){
  as_value actor_value=fn.arg(0);gc_ptr<as_object> actor=actor_value.to_object();as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};array<with_stack_entry> empty;bool found=false;env->set_local("u_impact",false);
  SourceIndex counter(env,empty,lookup_accel);
  if(read(actor,source_keys::get(source_keys::key_item))==as_value((as_object*)nullptr)){
   counter.set(1);
   while(counter.get().to_number()<=number(self,source_keys::get(source_keys::key_total_kids))){
    auto index=counter.get();as_value other_value;
    if(lookup_accel)other_value=indexed_read(self,3,index);else{std::string name="kid";name+=index.to_string();other_value=read(self,name.c_str());}gc_ptr<as_object> other=other_value.to_object();
    if(!read(other,source_keys::get(source_keys::key_nohit)).to_bool() && read(other,source_keys::get(source_keys::key_alive)).to_bool() && read(other,source_keys::get(source_keys::key_grab)).to_bool() && std::fabs(number(other,source_keys::get(source_keys::key_y))-number(actor,source_keys::get(source_keys::key_y)))<10 && std::fabs(number(other,source_keys::get(source_keys::key_x))-number(actor,source_keys::get(source_keys::key_x)))<30 && !read(other,source_keys::get(source_keys::key_atv)).to_bool()){
     if(read(actor,source_keys::get(source_keys::key_dashing)).to_bool()){
      env->set_local("u_impact",true);invoke(env,self,source_keys::get(source_keys::key_f_FlipInverse),{other_value,actor_value});write(other,source_keys::get(source_keys::key_speed_toss_y),-12);write(other,source_keys::get(source_keys::key_speed_toss_x),8);invoke(env,self,source_keys::get(source_keys::key_f_Damage),{other_value,read(actor,source_keys::get(source_keys::key_punch_pow_low))});invoke(env,self,source_keys::get(source_keys::key_f_CallJuggle1),{other_value});write(object(other,source_keys::get(source_keys::key_body)),source_keys::get(source_keys::key__y),number(actor,source_keys::get(source_keys::key_y))-50-number(other,source_keys::get(source_keys::key_y)));
     }else{
      write(actor,source_keys::get(source_keys::key_grab_pointer),other_value);write(object(actor,source_keys::get(source_keys::key_grab_pointer)),source_keys::get(source_keys::key_distance),55);write(actor,source_keys::get(source_keys::key_grab_counter),0);invoke(env,self,source_keys::get(source_keys::key_f_KidGrabbed),{actor_value});
      // NULL is an AS1 variable here, not a null literal. Resolve each
      // ownership clear at its source sequence point, retaining its type.
      if(read(other,source_keys::get(source_keys::key_item))!=as_value((as_object*)nullptr)){invoke(env,object(other,source_keys::get(source_keys::key_item)),source_keys::get(source_keys::key_gotoAndStop),{"stand"});write(object(other,source_keys::get(source_keys::key_item)),source_keys::get(source_keys::key_owner),env->get_variable("NULL",empty));write(other,source_keys::get(source_keys::key_item),env->get_variable("NULL",empty));}
      invoke(env,other,source_keys::get(source_keys::key_gotoAndStop),{"grabbed"});counter.invalidate();counter.set(number(self,source_keys::get(source_keys::key_total_kids))+1);found=true;invoke(env,actor,source_keys::get(source_keys::key_gotoAndStop),{"grab"});
     }
    }
    counter.invalidate();auto i=counter.get();i+=1;counter.set(i);
   }
   if(env->get_variable("u_impact",empty).to_bool()){
    invoke(env,self,source_keys::get(source_keys::key_f_PunchSound),{});as_value animation=number(actor,source_keys::get(source_keys::key_rage))>=number(actor,source_keys::get(source_keys::key_rage_goal))?"rageimpact":"impact3";env->set_local("impact_anim",animation);
    invoke(env,self,source_keys::get(source_keys::key_f_FX),{read(actor,source_keys::get(source_keys::key_x)),number(actor,source_keys::get(source_keys::key_y))-50,int(std::floor(number(actor,source_keys::get(source_keys::key_y))))+30,animation});
   }
  }
  env->m_global_register[0]=found;for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];*fn.result=found;return true;
 }
 if(function.m_start_pc==16394 && function.m_length==893 && function.m_body_fingerprint==2595248375496401114ULL && fn.nargs==1){
  as_value actor_value=fn.arg(0);gc_ptr<as_object> actor=actor_value.to_object();as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};array<with_stack_entry> empty;bool hit=false;
  SourceIndex counter(env,empty,lookup_accel);
  if((number(actor,source_keys::get(source_keys::key_speed_toss_y))>40 || number(actor,source_keys::get(source_keys::key_speed_toss_x))>14) && number(object(actor,source_keys::get(source_keys::key_body)),source_keys::get(source_keys::key__y))>-150){
   counter.set(1);
   while(counter.get().to_number()<=number(self,source_keys::get(source_keys::key_total_kids))){
    auto index=counter.get();as_value other_value;
    if(lookup_accel)other_value=indexed_read(self,3,index);else{std::string name="kid";name+=index.to_string();other_value=read(self,name.c_str());}gc_ptr<as_object> other=other_value.to_object();
    if(!read(other,source_keys::get(source_keys::key_nohit)).to_bool() && other!=actor && read(other,source_keys::get(source_keys::key_alive)).to_bool() && !(actor_value==read(self,source_keys::get(source_keys::key_dude1)) && read(other,source_keys::get(source_keys::key_atv)).to_bool()) && std::fabs(number(other,source_keys::get(source_keys::key_y))-number(actor,source_keys::get(source_keys::key_y)))<15 && std::fabs(number(other,source_keys::get(source_keys::key_x))-number(actor,source_keys::get(source_keys::key_x)))<40){
     invoke(env,self,source_keys::get(source_keys::key_f_Damage),{actor_value,1});invoke(env,self,source_keys::get(source_keys::key_f_Damage),{other_value,3});hit=true;invoke(env,self,source_keys::get(source_keys::key_f_FlipSame),{other_value,actor_value});write(other,source_keys::get(source_keys::key_speed_toss_y),-double(tu_random::next_random()%10+10));write(other,source_keys::get(source_keys::key_speed_toss_x),number(actor,source_keys::get(source_keys::key_speed_toss_x))*.75);invoke(env,self,source_keys::get(source_keys::key_f_CheckHold),{other_value});invoke(env,self,source_keys::get(source_keys::key_f_CallJuggle1),{other_value});write(object(other,source_keys::get(source_keys::key_body)),source_keys::get(source_keys::key__y),-50);
    }
    counter.invalidate();auto i=counter.get();i+=1;counter.set(i);
   }
   if(number(actor,source_keys::get(source_keys::key_weight))>0){
    auto other_value=read(self,source_keys::get(source_keys::key_dude1));gc_ptr<as_object> other=other_value.to_object();
    if(!read(other,source_keys::get(source_keys::key_nohit)).to_bool() && other!=actor && std::fabs(number(other,source_keys::get(source_keys::key_y))-number(actor,source_keys::get(source_keys::key_y)))<15 && std::fabs(number(other,source_keys::get(source_keys::key_x))-number(actor,source_keys::get(source_keys::key_x)))<40){
     hit=true;invoke(env,self,source_keys::get(source_keys::key_f_FlipSame),{other_value,actor_value});write(other,source_keys::get(source_keys::key_speed_toss_y),-double(tu_random::next_random()%10+10));write(other,source_keys::get(source_keys::key_speed_toss_x),number(actor,source_keys::get(source_keys::key_speed_toss_x))*.75);invoke(env,self,source_keys::get(source_keys::key_f_DropItem),{other_value});invoke(env,self,source_keys::get(source_keys::key_f_CallJuggle1),{other_value});write(object(other,source_keys::get(source_keys::key_body)),source_keys::get(source_keys::key__y),-50);
    }
   }
   if(hit){invoke(env,self,source_keys::get(source_keys::key_f_PunchSound),{});invoke(env,self,source_keys::get(source_keys::key_f_FX),{read(actor,source_keys::get(source_keys::key_x)),number(object(actor,source_keys::get(source_keys::key_body)),source_keys::get(source_keys::key__y))+number(actor,source_keys::get(source_keys::key_y)),int(std::floor(number(actor,source_keys::get(source_keys::key_y))))+15,"impact1"});}
  }
  for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];return true;
 }
 if(function.m_start_pc!=33326 || function.m_length!=2169 || function.m_body_fingerprint!=2537209739007823015ULL || fn.nargs!=6 || !self || function.m_with_stack.size())return false;
 as_value args[6];for(unsigned i=0;i<6;++i)args[i]=fn.arg(i);
 gc_ptr<as_object> actor=args[0].to_object();if(!actor)return false;
 const bool vert=args[1].to_bool();const double speed=args[2].to_number();
 // Keep the original global loop counter and ordinary helper dispatch. A helper
 // may change the group or counter; neither is hoisted out of the source loop.
 bool valid_x=false,valid_y=false;double cached_x=0,cached_y=0;
 auto ax=[&](){if(!valid_x){cached_x=number(actor,source_keys::get(source_keys::key_x));valid_x=true;}return cached_x;};
 auto ay=[&](){if(!valid_y){cached_y=number(actor,source_keys::get(source_keys::key_y));valid_y=true;}return cached_y;};
 auto invalidate=[&](){valid_x=valid_y=false;};
 array<with_stack_entry> empty;
 static const tu_string index_name("i"),return_name("return_val");
 env->set_variable_raw(index_name,args[4],empty);
 // Cache the binding, never the mutable loop value or a hash-table address.
 // Helpers may retarget the environment or change a shadowing local/child;
 // re-resolve after every helper dispatch before reading or writing again.
 int index_slot=-1;weak_ptr<character> index_target;bool index_plain_target=false,index_dirty=true;
 auto refresh_index=[&](){
  index_slot=env->find_local(index_name,true);auto target=env->get_target();index_target=target;
  auto sprite=cast_to<sprite_instance>(target);
  index_plain_target=target && (!sprite || !sprite->m_display_list.get_character_by_name_i(index_name));
  index_dirty=false;
 };
 auto read_index=[&](){
  if(index_dirty || index_target.get_ptr()!=env->get_target())refresh_index();
  if(index_slot>=0 && index_slot<env->m_local_frames.size() && env->m_local_frames[index_slot].m_name==index_name)
   return env->m_local_frames[index_slot].m_value;
  auto target=index_target.get_ptr();
  if(index_plain_target && target){auto it=target->m_members.find(index_name);
   if(it!=target->m_members.end() && !it->second.is_property())return it->second;}
  return env->get_variable_raw(index_name,empty);
 };
 auto write_index=[&](const as_value& value){
  if(index_dirty || index_target.get_ptr()!=env->get_target())refresh_index();
  if(index_slot>=0 && index_slot<env->m_local_frames.size() && env->m_local_frames[index_slot].m_name==index_name){
   env->m_local_frames[index_slot].m_value=value;return;
  }
  auto target=index_target.get_ptr();
  if(index_plain_target && target && !target->m_watch){auto it=target->m_members.find(index_name);
   if(it!=target->m_members.end() && !it->second.is_property() && !it->second.is_readonly()){
    int flags=it->second.get_flags();it->second=value;it->second.set_flags(flags);return;
   }
  }
  env->set_variable_raw(index_name,value,empty);
 };
 auto invalidate_after_helper=[&](){invalidate();index_dirty=true;};
 bool result=false;as_value saved[3]={env->m_global_register[1],env->m_global_register[2],env->m_global_register[3]};
 // A nested script call appends its own frame and restores its previous top.
 // Cache indices into this call's locals, never addresses into the growable
 // array. Every original local write remains visible to nested helpers.
 enum Local { XScale,YScale,X,Y,H,W,Percent,TempY,TempX,Return,LocalCount };
 static const tu_string local_names[LocalCount]={"xscale_val","yscale_val","x","y","h","w","percent","temp_y","temp_x","return_val"};
 int local_slots[LocalCount];for(auto& slot:local_slots)slot=-1;
 auto store=[&](Local id,const as_value& value){
  int& slot=local_slots[id];
  if(slot<0){env->set_local(local_names[id],value);slot=env->find_local(local_names[id],false);}
  else {assert(slot<env->m_local_frames.size() && env->m_local_frames[slot].m_name==local_names[id]);env->m_local_frames[slot].m_value=value;}
 };
 auto local=[&](Local id,double value){store(id,value);return value;};
 store(Return,false);
 auto check_mask=[&](const as_value& other){invoke(env,self,source_keys::get(source_keys::key_f_CheckMask),{actor.get_ptr(),other});invalidate_after_helper();};
 auto auto_take=[&](const as_value& other){auto result=invoke(env,self,source_keys::get(source_keys::key_f_AutoTake),{actor.get_ptr(),other,args[1],speed})==as_value(false);invalidate_after_helper();return result;};
 int name_group=-1;if(args[5].is_string()){const char* p=args[5].to_string();if(!std::strcmp(p,"static"))name_group=0;else if(!std::strcmp(p,"object_pt"))name_group=1;}
 while(read_index().to_number()<=args[3].to_number()){
  invalidate();as_value index=read_index();const char* fixed=nullptr;
  if(name_group>=0&&index.is_number()){double n=index.to_number();if(n>0&&n<128&&n==int(n))fixed=wall_names[name_group][int(n)];}
  as_value other_value;
  const source_keys::Key* key=lookup_accel&&name_group>=0?indexed_source_key(name_group==0?0:2,index):nullptr;
  if(key)other_value=read(self,*key);
  else if(fixed)other_value=read(self,fixed);
  else{std::string name=args[5].to_string();name+=index.to_string();other_value=read(self,name.c_str());}
  gc_ptr<as_object> other=other_value.to_object();auto zone=object(other,source_keys::get(source_keys::key_zone));
  WallRectangle rectangle;double xs,ys,x,y,h,w;
  if(wall_rectangle(other,zone,rectangle)){
   xs=local(XScale,rectangle.xs);ys=local(YScale,rectangle.ys);
   x=local(X,rectangle.x);y=local(Y,rectangle.y);h=local(H,rectangle.h);w=local(W,rectangle.w);
  }else{
   xs=local(XScale,std::fabs(number(other,source_keys::get(source_keys::key__xscale)))/100);
   ys=local(YScale,std::fabs(number(other,source_keys::get(source_keys::key__yscale)))/100);
   x=local(X,number(other,source_keys::get(source_keys::key_x))+number(zone,source_keys::get(source_keys::key__x)));
   y=local(Y,number(other,source_keys::get(source_keys::key_y))+number(zone,source_keys::get(source_keys::key__y))+2);
   double zone_height=0,zone_width=0;bool dimensions_cached=collision_dimensions(zone,zone_height,zone_width);
   h=local(H,(dimensions_cached?zone_height:number(zone,source_keys::get(source_keys::key__height)))*ys);
   w=local(W,(dimensions_cached?zone_width:number(zone,source_keys::get(source_keys::key__width)))/2*xs);
  }
  auto hit=[&]{result=true;store(Return,true);};
  if(vert){
   if(std::fabs(ax()-x)<w){
    if(read(other,source_keys::get(source_keys::key_diagonal)).to_bool()){
     if(ay()+speed>=y-h && ay()+speed<=y+150){
      double percent=local(Percent,(number(other,source_keys::get(source_keys::key_x2))-ax())/number(other,source_keys::get(source_keys::key_w)));
      double temp=local(TempY,y-number(other,source_keys::get(source_keys::key_h))*percent);
      if(number(other,source_keys::get(source_keys::key_mask))>0){if(ay()<temp){write(actor,source_keys::get(source_keys::key_mask_pt),other_value);invoke(env,object(actor,source_keys::get(source_keys::key_mask)),source_keys::get(source_keys::key_gotoAndStop),{read(other,source_keys::get(source_keys::key_mask))});invalidate_after_helper();}else check_mask(other_value);}
      if(std::fabs(ax()-x)<number(other,source_keys::get(source_keys::key_w))/2){
       if((speed>0 && ay()<=temp && ay()+speed>=temp-number(other,source_keys::get(source_keys::key_h2))) || (speed<0 && ay()>=temp && ay()+speed<=temp+number(other,source_keys::get(source_keys::key_h2)))){
        write(actor,source_keys::get(source_keys::key_diagonal_speed),speed>0?temp-number(other,source_keys::get(source_keys::key_h2))-ay():temp+number(other,source_keys::get(source_keys::key_h2))-ay());write(actor,source_keys::get(source_keys::key_collide),other_value);write(actor,source_keys::get(source_keys::key_diagonalhit),true);hit();
       }
      }
     }else check_mask(other_value);
    }else if(ay()<y-h && ay()+speed>=y-h){if(auto_take(other_value))hit();}
    else if(ay()>y && ay()+speed<=y){if(auto_take(other_value))hit();}
   }else check_mask(other_value);
  }else if(ay()>y-h && ay()<y){
   if(read(other,source_keys::get(source_keys::key_diagonal)).to_bool()){
    if(ax()+speed>x-w && ax()+speed<x+w){
     double percent=local(Percent,1-(y-ay())/number(other,source_keys::get(source_keys::key_h)));
     double temp=local(TempX,number(other,source_keys::get(source_keys::key_x1))+number(other,source_keys::get(source_keys::key_w))*percent);
     if(number(other,source_keys::get(source_keys::key_mask))>0){if(ax()>temp){write(actor,source_keys::get(source_keys::key_mask_pt),other_value);invoke(env,object(actor,source_keys::get(source_keys::key_mask)),source_keys::get(source_keys::key_gotoAndStop),{read(other,source_keys::get(source_keys::key_mask))});invalidate_after_helper();}else check_mask(other_value);}
     if(ay()>y-number(other,source_keys::get(source_keys::key_h)) && ay()<y){
      if((speed>0 && ax()<=temp && ax()+speed>=temp-number(other,source_keys::get(source_keys::key_w2))) || (speed<0 && ax()>=temp && ax()+speed<=temp+number(other,source_keys::get(source_keys::key_w2)))){
       if(number(object(actor,source_keys::get(source_keys::key_body)),source_keys::get(source_keys::key__y))>number(zone,source_keys::get(source_keys::key__y))-number(zone,source_keys::get(source_keys::key__height))){
        write(actor,source_keys::get(source_keys::key_diagonal_speed),speed>0?temp-number(other,source_keys::get(source_keys::key_w2))-ax():temp+number(other,source_keys::get(source_keys::key_w2))-ax());write(actor,source_keys::get(source_keys::key_collide),other_value);write(actor,source_keys::get(source_keys::key_diagonalhit),true);hit();
       }
      }
     }
    }else check_mask(other_value);
   }else if((ax()<x-w && ax()+speed>=x-w) || (ax()>x+w && ax()+speed<=x+w)){
    if(number(object(actor,source_keys::get(source_keys::key_body)),source_keys::get(source_keys::key__y))+ay()>y-(number(other,source_keys::get(source_keys::key__height))+20))if(auto_take(other_value)){write(actor,source_keys::get(source_keys::key_collide),other_value);hit();}
   }
  }else check_mask(other_value);
  auto i=read_index();i+=1;write_index(i);
 }
 result=env->get_variable_raw(return_name,empty).to_bool();env->m_global_register[0]=result;for(unsigned i=0;i<3;++i)env->m_global_register[i+1]=saved[i];*fn.result=result;return true;
}
}
