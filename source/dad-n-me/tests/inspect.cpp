#include "gameswf/gameswf.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_sprite.h"
#include "gameswf/gameswf_function.h"
#include "base/tu_file.h"
#include "base/tu_random.h"
#include "base/tu_timer.h"
#include "null_renderer.hpp"
#include "../src/source_ui.hpp"
#include "../src/source_visual_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#ifdef DNM_FUNCTION_PROFILE
namespace gameswf {extern unsigned dnm_profile_tick;}
#endif

static tu_file* open_file(const char* path) { return new tu_file(path,"rb"); }
static void log_message(bool error,const char* s) { std::fprintf(stderr,"%s%s",error?"ERROR ":"",s); }
static void quoted(const std::string& s) {
    std::cout << '"';
    for(unsigned char c:s) {
        if(c=='"'||c=='\\')std::cout << '\\' << c;
        else if(c=='\n')std::cout << "\\n";
        else if(c=='\r')std::cout << "\\r";
        else if(c=='\t')std::cout << "\\t";
        else if(c>=32)std::cout << c;
    }
    std::cout << '"';
}
static gameswf::as_object* object(gameswf::root* root,std::string path) {
    gameswf::as_object* current=root->get_root_movie();
    std::istringstream parts(path);std::string part;
    while(std::getline(parts,part,'.')) {
        if(part=="_root"||part.empty())continue;
        gameswf::as_value value;
        if(!current || !current->get_member(part.c_str(),&value))return nullptr;
        current=value.to_object();
    }
    return current;
}
static void value(const gameswf::as_value& v) {
    if(v.is_number())std::cout << v.to_number();
    else if(v.is_bool())std::cout << (v.to_bool()?"true":"false");
    else if(v.is_null()||v.is_undefined())std::cout << "null";
    else quoted(v.to_string());
}
static void appearance(gameswf::character* ch, const std::string& path, bool& comma) {
    auto sprite=gameswf::cast_to<gameswf::sprite_instance>(ch);
    if(!sprite)return;
    if(ch->get_id()==501 || ch->get_id()==529) {
        gameswf::as_value expected;auto p=ch->get_parent();
        std::vector<gameswf::character*> parents;
        for(int n=0;p && n<4;n++,p=p->get_parent())parents.push_back(p);
        for(int depth:{2,3,4,1})if(int(parents.size())>=depth){
            gameswf::as_value root;parents[depth-1]->get_member("root",&root);
            if(root.to_bool()){parents[depth-1]->get_member(ch->get_id()==501?"shirt_type":"hat_type",&expected);break;}
        }
        if(expected.is_undefined())for(int depth:{2,3,4,1})if(int(parents.size())>=depth){
            auto it=parents[depth-1]->m_members.find(ch->get_id()==501?"shirt_type":"hat_type");
            if(it!=parents[depth-1]->m_members.end()&&!it->second.is_undefined()){expected=it->second;break;}
        }
        if(comma)std::cout<<',';comma=true;
        std::cout<<"{\"path\":";quoted(path);std::cout<<",\"id\":"<<ch->get_id()
            <<",\"frame\":"<<ch->get_current_frame()+1<<",\"expected\":";value(expected);
        std::cout<<",\"children\":"<<sprite->m_display_list.size()<<",\"visible\":"<<(ch->get_visible()?"true":"false")<<'}';
    }
    for(int i=0;i<sprite->m_display_list.size();++i){
        auto child=sprite->m_display_list.get_character(i);
        appearance(child,path+"/"+std::to_string(child->get_id())+":"+child->get_name().c_str(),comma);
    }
}
static void deaths(gameswf::character* ch,unsigned tick){
    auto s=gameswf::cast_to<gameswf::sprite_instance>(ch);if(!s)return;
    int start=ch->get_id()==748?34:(ch->get_id()==665||ch->get_id()==439)?15:-1;
    if(start>=0&&ch->get_current_frame()>=start){
        std::cout<<"{\"death_tick\":"<<tick<<",\"id\":"<<ch->get_id()<<",\"frame\":"<<ch->get_current_frame()+1
            <<",\"offset\":"<<ch->get_current_frame()-start<<",\"suppressed\":"<<(gameswf::dnm_visual_hidden(ch)?"true":"false")<<"}\n";
    }
    for(int i=0;i<s->m_display_list.size();++i)deaths(s->m_display_list.get_character(i),tick);
}
static gameswf::sprite_instance* find_sprite(gameswf::character* ch,int id){
    auto s=gameswf::cast_to<gameswf::sprite_instance>(ch);if(!s)return nullptr;
    if(ch->get_id()==id)return s;
    for(int i=0;i<s->m_display_list.size();++i)if(auto p=find_sprite(s->m_display_list.get_character(i),id))return p;
    return nullptr;
}
static uint64_t visual_hash(gameswf::character* ch,uint64_t h=1469598103934665603ULL){
    auto word=[&](uint32_t n){for(int i=0;i<4;++i){h^=(n>>(i*8))&255;h*=1099511628211ULL;}};
    auto scalar=[&](float n){uint32_t bits;std::memcpy(&bits,&n,4);word(bits);};
    word(ch->get_id());word(ch->get_depth());word(ch->get_current_frame());word(ch->get_visible());
    const auto& m=ch->get_matrix();for(int r=0;r<2;++r)for(int c=0;c<3;++c)scalar(m.m_[r][c]);
    const auto& cx=ch->get_cxform();for(int r=0;r<4;++r)for(int c=0;c<2;++c)scalar(cx.m_[r][c]);
    for(const char* n=ch->get_name().c_str();*n;++n){h^=uint8_t(*n);h*=1099511628211ULL;}
    auto s=gameswf::cast_to<gameswf::sprite_instance>(ch);word(s?s->m_display_list.size():0);
    if(s)for(int i=0;i<s->m_display_list.size();++i)h=visual_hash(s->m_display_list.get_character(i),h);
    return h;
}
int main(int argc,char** argv) {
    if(argc<2)return 2;
    std::cout.precision(16);tu_timer::init_timer();
    gameswf::register_file_opener_callback(open_file);gameswf::register_log_callback(log_message);
    NullRenderer null_renderer;if(argc>3){null_renderer.release_geometry=std::string(argv[3])!="render";gameswf::set_render_handler(&null_renderer);}
    gameswf::gc_ptr<gameswf::player> player=new gameswf::player;
    // player construction intentionally advances the RNG by wall-clock time.
    // Set the fixture seed after that normal startup behavior.
    tu_random::seed_random(argc>2?std::strtoul(argv[2],nullptr,10):42);
    player->set_separate_thread(false);auto root=player->load_file(argv[1]);if(!root)return 1;
    unsigned tick=0;std::string input;
    while(std::getline(std::cin,input)) {
        std::istringstream command(input);std::string op;command>>op;
        if(op.empty()||op[0]=='#')continue;
        if(op=="tick") {
            unsigned count=1;command>>count;for(unsigned n=0;n<count;n++){root->advance(1.0f/30);++tick;}
            std::cout << "{\"tick\":" << tick << ",\"frame\":" << root->get_current_frame()+1 << ",\"rng\":" << tu_random::state_hash() << "}\n";
        } else if(op=="resource-replay") {
            // Host resource discovery from original key events. The native
            // source-frame CSV and PSP pixels remain the acceptance evidence.
            std::string tape;unsigned target;command>>tape>>target;
            FILE* file=std::fopen(tape.c_str(),"rb");if(!file)return 3;
            uint32_t header[5];if(std::fread(header,sizeof(header),1,file)!=1 || header[0]!=0x524d4e44 || header[1]!=1)return 3;
            struct Entry{uint32_t frames,buttons;};std::vector<Entry> entries(header[3]);
            if(std::fread(entries.data(),sizeof(Entry),entries.size(),file)!=entries.size())return 3;std::fclose(file);
            if(tick || entries.empty() || target>header[4])return 3;tu_random::seed_random(header[2]);
            const unsigned masks[]={128,32,16,64,16384,32768};
            const gameswf::key::code keys[]={gameswf::key::LEFT,gameswf::key::RIGHT,gameswf::key::UP,gameswf::key::DOWN,gameswf::key::A,gameswf::key::S};
            unsigned index=0,end=entries[0].frames,previous=0;
            for(;tick<target;++tick){
                while(tick>=end && index+1<entries.size())end+=entries[++index].frames;
                unsigned buttons=entries[index].buttons;
                if((buttons&~previous)&16384)dnm::activate_continue(root.get_ptr());
                for(unsigned n=0;n<6;++n)if((buttons^previous)&masks[n])player->notify_key_event(keys[n],buttons&masks[n]);
                previous=buttons;
#ifdef DNM_FUNCTION_PROFILE
                gameswf::dnm_profile_tick=tick+1;
#endif
                root->advance(1.0f/30);
                if(std::getenv("DNM_AUDIT_DEATHS")){
                    auto game=object(root,"game");
                    for(int i=1;game&&i<=15;++i){gameswf::as_value actor;game->get_member(("kid"+std::to_string(i)).c_str(),&actor);deaths(gameswf::cast_to<gameswf::character>(actor.to_object()),tick+1);}
                }
            }
            null_renderer.trace_shapes=true;root->display();null_renderer.trace_shapes=false;
            std::cout << "{\"resource_replay_tick\":" << tick << ",\"frame\":" << root->get_current_frame()+1 << ",\"rng\":" << tu_random::state_hash() << "}\n";
        } else if(op=="name-lookup-stats") {
            auto s=gameswf::dnm_get_name_lookup_stats();
            std::cout<<"{\"name_lookups\":"<<s.lookups<<",\"negative_rejects\":"<<s.rejects<<",\"scanned_children\":"<<s.scanned<<",\"filter_rebuilds\":"<<s.rebuilds<<",\"positive_hits\":"<<s.hits<<"}\n";
        } else if(op=="resource-frame") {
            // Explicit host-only resource discovery; never gameplay proof.
            unsigned frame;command>>frame;root->goto_frame(frame-1);
            null_renderer.trace_shapes=true;root->display();null_renderer.trace_shapes=false;
            std::cout << "{\"resource_frame\":" << root->get_current_frame()+1 << "}\n";
        } else if(op=="draw") {
            unsigned count=1;command>>count;for(unsigned n=0;n<count;n++)root->display();
            std::cout << "{\"draws\":" << count << ",\"rng\":" << tu_random::state_hash() << "}\n";
        } else if(op=="key") {
            std::string state,name;command>>state>>name;
            gameswf::key::code code=gameswf::key::INVALID;
            if(name=="left")code=gameswf::key::LEFT;if(name=="right")code=gameswf::key::RIGHT;
            if(name=="up")code=gameswf::key::UP;if(name=="down")code=gameswf::key::DOWN;
            if(name=="a")code=gameswf::key::A;if(name=="s")code=gameswf::key::S;
            if(name=="a" && state=="down")dnm::activate_continue(root.get_ptr());
            player->notify_key_event(code,state=="down");std::cout << "{\"key\":true}\n";
        } else if(op=="costume-transitions") {
            for(int id:{501,529}){
                gameswf::gc_ptr<gameswf::sprite_instance> clip=find_sprite(root->get_root_movie(),id);if(!clip)return 8;
                int count=clip->m_def->get_frame_count();
                for(int from=0;from<count;++from)for(int to=0;to<count;++to){
                    clip->goto_frame(from);clip->goto_frame(to);
                    std::cout<<"{\"costume\":"<<id<<",\"from\":"<<from+1<<",\"to\":"<<to+1<<",\"visual_hash\":\""<<visual_hash(clip)<<"\"}\n";
                }
            }
        } else if(op=="visual-stats") {
            auto s=gameswf::dnm_visual_stats();std::cout<<"{\"visual_stats\":{\"deferred\":"<<s.deferred<<",\"rebound\":"<<s.rebound<<",\"pending\":"<<s.pending<<",\"fire_suppressed\":"<<s.fire_suppressed<<"}}\n";
        } else if(op=="appearance") {
            std::string path;command>>path;bool comma=false;
            std::cout<<"{\"appearance\":[";appearance(gameswf::cast_to<gameswf::character>(object(root,path)),path,comma);std::cout<<"]}\n";
        } else if(op=="tree") {
            std::string path;command>>path;auto sprite=gameswf::cast_to<gameswf::sprite_instance>(object(root,path));
            std::cout << "{\"children\":[";
            if(sprite)for(int i=0;i<sprite->m_display_list.size();i++) {
                auto ch=sprite->m_display_list.get_character(i);if(i)std::cout << ',';
                std::cout << "{\"id\":" << ch->get_id() << ",\"depth\":" << ch->get_depth() << ",\"frame\":" << ch->get_current_frame()+1 << ",\"name\":";quoted(ch->get_name().c_str());std::cout << '}';
            }
            std::cout << "]}\n";
        } else if(op=="call-one-object") {
            // Explicit host semantic fixture, separate from input-only replay.
            std::string path,name,arg;command>>path>>name>>arg;auto receiver=object(root,path);
            if(!receiver)return 4;gameswf::as_value method;receiver->get_member(name.c_str(),&method);
            auto env=receiver->get_environment();if(!env)return 4;
            env->push(object(root,arg));gameswf::call_method(method,env,receiver,1,env->get_top_index());env->drop(1);
            std::cout << "{\"called\":";quoted(name);std::cout << "}\n";
        } else if(op=="functions") {
            std::string path;command>>path;auto current=object(root,path);
            std::cout << "{\"functions\":[";bool comma=false;
            if(current)for(auto it=current->m_members.begin();it!=current->m_members.end();++it) {
                auto function=gameswf::cast_to<gameswf::as_s_function>(it->second.to_object());
                if(!function)continue;if(comma)std::cout << ',';comma=true;
                std::cout << "{\"name\":";quoted(it->first.c_str());
                std::cout << ",\"start\":" << function->m_start_pc << ",\"length\":" << function->m_length
                          << ",\"fingerprint\":\"" << function->m_body_fingerprint << "\"}";
            }
            std::cout << "]}\n";
        } else if(op=="info"||op=="members") {
            std::string path;command>>path;auto current=object(root,path);
            std::cout << "{\"tick\":" << tick;
            if(current) {
                if(op=="members")for(auto it=current->m_members.begin();it!=current->m_members.end();++it) {
                    if(it->second.to_object())continue;
                    std::cout << ',';quoted(it->first.c_str());std::cout << ':';value(it->second);
                } else {
                    std::string member;while(command>>member) {gameswf::as_value v;current->get_member(member.c_str(),&v);std::cout << ',';quoted(member);std::cout << ':';value(v);}
                }
            } else std::cout << ",\"missing\":true";
            std::cout << "}\n";
        } else if(op=="set") {
            // Instrumented fixtures only: never used by input-only campaign qualification.
            std::string path,member,text;command>>path>>member>>text;auto current=object(root,path);
            if(current)current->set_member(member.c_str(),gameswf::as_value(std::strtod(text.c_str(),nullptr)));
            std::cout << "{\"instrumented\":true}\n";
        } else if(op=="quit")break;
        else std::cout << "{\"error\":\"unknown command\"}\n";
        std::cout.flush();
    }
}
