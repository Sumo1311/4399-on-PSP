#include "gameswf/gameswf.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_sprite.h"
#include "gameswf/gameswf_as_classes/as_key.h"
#include "base/tu_file.h"
#include "base/tu_timer.h"
#include "base/tu_random.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static tu_file* open_file(const char* path) { return new tu_file(path,"rb"); }
static void log_message(bool error,const char* s) { if(error&&std::strstr(s,"].f_SeekOwner"))return; std::fprintf(stderr,"%s%s",error?"ERROR ":"",s); }
int main(int argc,char** argv) {
    if(argc<2) return 2;
    tu_timer::init_timer();
    gameswf::register_file_opener_callback(open_file);
    gameswf::register_log_callback(log_message);
    gameswf::gc_ptr<gameswf::player> player=new gameswf::player;
    tu_random::seed_random(42);
    player->set_separate_thread(false);
    auto root=player->load_file(argv[1]);
    if(!root) return 1;
    const int frames=argc>2?std::atoi(argv[2]):900;
    for(int i=0;i<frames;++i) {
        if(i==240) player->notify_key_event(gameswf::key::RIGHT,true);
        if(i==340) player->notify_key_event(gameswf::key::RIGHT,false);
        if(i==350) player->notify_key_event(gameswf::key::A,true);
        root->advance(1.0f/30);
        if(i==260) {
            gameswf::as_value kv,gv,dv;
            player->get_global()->get_member("Key",&kv);
            root->get_root_movie()->get_member("game",&gv);
            gv.to_object()->get_member("dude1",&dv);
            auto dude=gameswf::cast_to<gameswf::sprite_instance>(dv.to_object());
            std::fprintf(stderr,"RIGHT=%d dude children=%d\n",gameswf::cast_to<gameswf::as_key>(kv.to_object())->is_key_down(39),dude->m_display_list.size());
            for(int n=0;n<dude->m_display_list.size();n++) {
                auto c=dude->m_display_list.get_character(n);
                gameswf::as_value event;
                bool found=c->get_member("onEnterFrame",&event);
                std::fprintf(stderr,"child id=%d frame=%d event=%d\n",c->get_id(),c->get_current_frame(),found);
            }
        }
        if(i%30==0 || i==239 || i==339) {
            std::printf("{\"tick\":%d,\"root_frame\":%d",i,root->get_current_frame());
            const char* vars[]={"_root.game._x","_root.game.dude1.x","_root.game.dude1.y","_root.game.dude1.health","_root.game.dude1._currentframe","_root.game.dude1.walking","_root.game.dude1.right_timer","_root.game.dude1.button_right","_root.game.pause","_root.game.checkpoint","_root.game.kills","_root.game.total_kids","_root.game.combo_count"};
            for(auto v:vars) {
                const char* result=root->get_variable(v);
                std::printf(",\"%s\":\"%s\"",v,result?result:"null");
            }
            std::puts("}");std::fflush(stdout);
        }
    }
}
