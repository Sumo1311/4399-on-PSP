#include "gameswf/gameswf.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_sprite.h"
#include "gameswf/gameswf_function.h"
#include "base/tu_file.h"
#include "base/tu_timer.h"
#include "base/tu_random.h"
#include "source_ui.hpp"
#include "null_renderer.hpp"
#include "control_watch.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
using namespace gameswf;
static tu_file* file_open(const char* s){return new tu_file(s,"rb");}
static void log_message(bool error,const char* s){if(error&&!std::strstr(s,"].f_SeekOwner"))std::fprintf(stderr,"%s",s);}
struct IdentityEntry {unsigned epoch=0;std::string text;};
static std::unordered_map<character*,IdentityEntry> identity_cache;
static unsigned identity_epoch=0;
static const std::string& identity(character* ch){
 static const std::string absent="null";if(!ch)return absent;
 auto& e=identity_cache[ch];if(e.epoch==identity_epoch)return e.text;
 e.text=std::to_string(ch->get_id())+":"+std::to_string(ch->get_depth())+":"+ch->get_name().c_str();
 if(ch->get_parent())e.text=identity(ch->get_parent())+"/"+e.text;
 e.epoch=identity_epoch;return e.text;
}
static void scalar(std::string& out,const as_value& v){
 if(v.is_number()){double n=v.to_number();uint64_t bits;std::memcpy(&bits,&n,8);char s[24];std::snprintf(s,sizeof(s),"N%016llx",(unsigned long long)bits);out+=s;}
 else if(v.is_bool())out+=v.to_bool()?"true":"false";
 else if(v.is_string()){out+='S';auto p=v.to_string();out+=std::to_string(std::strlen(p))+":"+p;}
 else if(v.is_undefined())out+="U";
 else if(v.is_null())out+="null";
 else if(v.is_property())out+="property";
 else if(auto ch=cast_to<character>(v.to_object()))out+=identity(ch);
 else if(auto f=cast_to<as_s_function>(v.to_object()))out+="F"+std::to_string(f->m_start_pc)+":"+std::to_string(f->m_body_fingerprint);
 else out+="object";
}
static void snapshot(character* ch,std::string& out){
 if(!ch)return;
 out+=identity(ch)+" frame="+std::to_string(ch->get_current_frame())+" visible="+std::to_string(ch->get_visible())+"\n";
 const auto& m=ch->get_matrix();const auto& cx=ch->get_cxform();
 auto bits=[&](float f){uint32_t n;std::memcpy(&n,&f,4);out+=std::to_string(n)+",";};
 for(int r=0;r<2;++r)for(int c=0;c<3;++c)bits(m.m_[r][c]);
 for(int r=0;r<4;++r)for(int c=0;c<2;++c)bits(cx.m_[r][c]);out+='\n';
 static std::unordered_map<character*,std::vector<std::string>> schemas;
 auto& names=schemas[ch];bool valid=int(names.size())==ch->m_members.size();
 if(valid)for(const auto& n:names){auto i=ch->m_members.find(n.c_str());if(i==ch->m_members.end()||n!=i->first.c_str()){valid=false;break;}}
 if(!valid){names.clear();for(auto it=ch->m_members.begin();it!=ch->m_members.end();++it)names.emplace_back(it->first.c_str());std::sort(names.begin(),names.end());}
 for(const auto& name:names){auto it=ch->m_members.find(name.c_str());out+=name+"=";scalar(out,it->second);out+=" flags="+std::to_string(it->second.get_flags())+"\n";}
 if(auto s=cast_to<sprite_instance>(ch)){
  out+="play="+std::to_string(s->get_play_state())+" children="+std::to_string(s->m_display_list.size())+"\n";
  for(int i=0;i<s->m_display_list.size();++i)snapshot(s->m_display_list.get_character(i),out);
 }
}
static uint64_t state_hash(const std::string& s){uint64_t h=1469598103934665603ULL;for(unsigned char c:s){h^=c;h*=1099511628211ULL;}return h;}
int main(int argc,char** argv){
 if(argc<3)return 2;
 FILE* f=std::fopen(argv[2],"rb");if(!f)return 2;uint32_t header[5];
 if(std::fread(header,sizeof(header),1,f)!=1||header[0]!=0x524d4e44||header[1]!=1)return 2;
 struct Entry{uint32_t frames,buttons;};std::vector<Entry> tape(header[3]);
 if(std::fread(tape.data(),sizeof(Entry),tape.size(),f)!=tape.size()||tape.empty())return 2;std::fclose(f);
 tu_timer::init_timer();register_file_opener_callback(file_open);register_log_callback(log_message);
 NullRenderer renderer;set_render_handler(&renderer);gc_ptr<player> player=new gameswf::player;
 tu_random::seed_random(header[2]);player->set_separate_thread(false);auto root=player->load_file(argv[1]);if(!root)return 2;
 const unsigned masks[]={128,32,16,64,16384,32768};const key::code keys[]={key::LEFT,key::RIGHT,key::UP,key::DOWN,key::A,key::S};
 unsigned n=0,end=tape[0].frames,previous=0,limit=argc>3?std::strtoul(argv[3],nullptr,10):header[4];
 unsigned dump=std::getenv("DNM_AUDIT_DUMP_TICK")?std::strtoul(std::getenv("DNM_AUDIT_DUMP_TICK"),nullptr,10):0;
 std::puts("tick,rng,state_hash,root_frame,hero_frame,health,pause,x,y");
 dnm::IntroPauseGuard pause_guard;bool apply_guard=std::getenv("DNM_APPLY_PAUSE_GUARD");
 std::string state;state.reserve(512*1024);
 for(unsigned tick=0;tick<limit;++tick){
  while(tick>=end&&n+1<tape.size())end+=tape[++n].frames;unsigned buttons=tape[n].buttons;
  if((buttons&~previous)&16384)dnm::activate_continue(root.get_ptr());
  for(unsigned k=0;k<6;++k)if((buttons^previous)&masks[k])player->notify_key_event(keys[k],buttons&masks[k]);previous=buttons;
  root->advance(1.0f/30);
  if(apply_guard){auto game=cast_to<sprite_instance>(cast_to<sprite_instance>(root->get_root_movie())->m_display_list.get_character_by_name_i("game"));
   if(auto reason=pause_guard.step(root->get_current_frame()==2?game:nullptr))std::fprintf(stderr,"pause-guard tick=%u reason=%u\n",tick+1,reason);}
  ++identity_epoch;if(identity_cache.size()>100000)identity_cache.clear();state.clear();snapshot(root->get_root_movie(),state);
  if(dump==tick+1){FILE* d=std::fopen(std::getenv("DNM_AUDIT_DUMP_PATH"),"wb");if(d){std::fwrite(state.data(),1,state.size(),d);std::fclose(d);}}
  auto var=[&](const char* p){const char* s=root->get_variable(p);return std::string(s?s:"undefined");};
  std::printf("%u,%u,%016llx,%d,%s,%s,%s,%s,%s\n",tick+1,tu_random::state_hash(),(unsigned long long)state_hash(state),root->get_current_frame()+1,
   var("_root.game.dude1._currentframe").c_str(),var("_root.game.dude1.health").c_str(),var("_root.game.pause").c_str(),var("_root.game.dude1.x").c_str(),var("_root.game.dude1.y").c_str());
 }
}
