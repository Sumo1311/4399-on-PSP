#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_action.h"
#include "gameswf/gameswf_stream.h"
#include "gameswf/gameswf_root.h"
#include "base/tu_file.h"
#include "null_renderer.hpp"
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace gameswf;
using Bytes=std::vector<unsigned char>;
static unsigned checks=0;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"VM string mismatch at %u\n",checks);std::exit(1);}}
static void emit(Bytes& out,unsigned op,const Bytes& payload={}){out.push_back(op);if(op&0x80){out.push_back(payload.size()&255);out.push_back(payload.size()>>8);out.insert(out.end(),payload.begin(),payload.end());}}
static void pool(Bytes& out,const std::vector<std::string>& names){Bytes p={static_cast<unsigned char>(names.size()&255),static_cast<unsigned char>(names.size()>>8)};for(auto& s:names){p.insert(p.end(),s.begin(),s.end());p.push_back(0);}emit(out,0x88,p);}
static void lookup(Bytes& out,unsigned index){if(index<256)emit(out,0x96,{8,(unsigned char)index});else emit(out,0x96,{9,(unsigned char)(index&255),(unsigned char)(index>>8)});}
static void store(Bytes& out){emit(out,0x87,{1});emit(out,0x17);}
static void load(action_buffer& out,Bytes& bytes){tu_file f(tu_file::memory_buffer,bytes.size(),bytes.data());stream s(&f);out.read(&s);}
static action_buffer* log_copy=nullptr;static action_buffer* log_source=nullptr;
static void log_message(bool,const char* text){if(log_copy&&std::strstr(text,"dict length exceeded")){*log_copy=*log_source;log_copy=nullptr;}}
static void execute(action_buffer& buffer,as_environment& env,const std::string& expected){buffer.execute(&env);check(env.m_global_register[1].to_tu_string()==expected.c_str());env.drop(env.size());}
static tu_file* open_file(const char* path){return new tu_file(path,"rb");}
int main(int argc,char** argv){
 if(argc!=2)return 2;register_log_callback(log_message);register_file_opener_callback(open_file);NullRenderer renderer;set_render_handler(&renderer);
 gc_ptr<player> p=new player;p->set_separate_thread(false);auto root=p->load_file(argv[1]);check(bool(root));as_environment env(p.get_ptr());
 std::string long_text(1200,'x');long_text+="中文字符串";
 Bytes a;pool(a,{"alpha",long_text});lookup(a,0);store(a);emit(a,0);action_buffer source;load(source,a);
 action_buffer unprocessed;unprocessed=source;execute(source,env,"alpha");execute(unprocessed,env,"alpha");
 action_buffer saved;saved=source;action_buffer constructed(saved);execute(constructed,env,"alpha");source=source;
 Bytes b;pool(b,{"beta"});lookup(b,0);store(b);emit(b,0);action_buffer replacement;load(replacement,b);execute(replacement,env,"beta");
 source=replacement;execute(source,env,"beta");execute(saved,env,"alpha");
 Bytes wide;std::vector<std::string> words(301,"value");words[300]=long_text;pool(wide,words);lookup(wide,300);store(wide);emit(wide,0);action_buffer high;load(high,wide);execute(high,env,long_text);
 Bytes many;pool(many,{long_text});for(unsigned i=0;i<400;++i)lookup(many,0);store(many);emit(many,0);action_buffer growth;load(growth,many);growth.execute(&env);check(env.size()==399);for(int i=0;i<env.size();++i)check(env.top(i).to_tu_string()==long_text.c_str());env.drop(env.size());
 Bytes two;pool(two,{"first"});pool(two,{"second"});lookup(two,0);store(two);emit(two,0);action_buffer duplicate;load(duplicate,two);execute(duplicate,env,"first");
 // Copy while a malformed pool is being logged. The copy must retain its
 // partial snapshot even as the source fills remaining entries with invalid.
 Bytes malformed;emit(malformed,0x88,{3,0,'o','k',0,'b','a','d'});Bytes numbers;
 for(unsigned i=0;i<51;++i){numbers.push_back(7);for(unsigned n=0;n<4;++n)numbers.push_back(0x11);}numbers.push_back(2);numbers.push_back(3);check(numbers.size()==257);emit(malformed,0x96,numbers);lookup(malformed,2);store(malformed);emit(malformed,0);
 action_buffer broken,snapshot;load(broken,malformed);log_copy=&snapshot;log_source=&broken;execute(broken,env,"<invalid>");check(log_copy==nullptr);execute(snapshot,env,"");
 // Duplication through stack growth and pop/self-alias must preserve values.
 vm_stack stack;as_value value(long_text.c_str());value.set_flags(3);stack.push(value);
 for(unsigned i=0;i<500;++i)stack.push(stack.top(0));for(int i=0;i<stack.size();++i){check(stack.top(i).to_tu_string()==long_text.c_str());check(stack.top(i).get_flags()==3);}
 stack.drop(500);as_value& same=stack.pop();stack.push(same);check(stack.top(0).to_tu_string()==long_text.c_str());stack.drop(1);
 // Replacing the sole owner of an external member must take the old copying
 // path before the owner and its member storage are destroyed.
 gc_ptr<as_object> owner=new as_object(p.get_ptr());owner->set_member("payload",long_text.c_str());weak_ptr<as_object> lifetime=owner.get_ptr();auto raw=owner.get_ptr();stack.push(raw);owner=nullptr;stack.pop();auto& member=raw->m_members.find("payload")->second;stack.push(member);check(lifetime.get_ptr()==nullptr);check(stack.top(0).to_tu_string()==long_text.c_str());stack.drop(1);
 std::printf("{\"checks\":%u,\"pass\":true}\n",checks);
}
