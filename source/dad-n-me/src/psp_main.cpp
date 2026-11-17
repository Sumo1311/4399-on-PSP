#include "frame_profile.hpp"
#include "render_psp.hpp"
#include "frame_captures.hpp"
#include "audio_psp.hpp"
#include "device_font.hpp"
#include "replay.hpp"
#include "source_ui.hpp"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_types.h"
#include "gameswf/gameswf_sprite.h"
#include "base/tu_file.h"
#include "base/tu_timer.h"
#include "base/tu_random.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <new>
#include <exception>
#include <malloc.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>

PSP_MODULE_INFO("Dad n Me",PSP_MODULE_USER,1,1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(512);
PSP_HEAP_SIZE_KB(-1024);
static volatile bool running=true;
static FILE* logfile;static bool verbose_log=false;
static unsigned source_noop_calls=0;
static double numeric(gameswf::as_object* object,const char* name) {
    gameswf::as_value value;if(!object || !object->get_member(name,&value))return 0;return value.to_number();
}
static gameswf::as_object* child(gameswf::as_object* object,const char* name) {
    gameswf::as_value value;if(!object || !object->get_member(name,&value))return nullptr;return value.to_object();
}
static int exit_callback(int,int,void*) { if(logfile)std::fputs("PSP exit callback received\n",logfile);running=false;return 0; }
static int callback_thread(SceSize,void*) {
    int cb=sceKernelCreateCallback("Dad n Me exit",exit_callback,nullptr);
    sceKernelRegisterExitCallback(cb);sceKernelSleepThreadCB();return 0;
}
static tu_file* open_file(const char* file) {
    if(std::strstr(file,"://"))return nullptr;
    return new tu_file(file,"rb");
}
static void log_message(bool error,const char* msg) {
    // This undefined call exists in the supplied original as well. Retain its
    // no-op behavior while avoiding hundreds of memory-stick writes per second.
    if(error && std::strstr(msg,"].f_SeekOwner")){++source_noop_calls;return;}
    if(logfile)std::fprintf(logfile,"%s%s",error?"ERROR ":"",msg);
}
extern "C" void dnm_graphics_trace(unsigned frame,unsigned draws,unsigned largest,unsigned bytes) {
    if(verbose_log && logfile)std::fprintf(logfile,"graphics frame=%u draws=%u largest=%u bytes=%u source_noops=%u\n",frame,draws,largest,bytes,source_noop_calls);
}
extern "C" void dnm_cache_trace(unsigned frame,unsigned hits,unsigned misses,unsigned decoded,unsigned resident) {
    if(verbose_log && logfile)std::fprintf(logfile,"cache frame=%u hits=%u misses=%u decoded=%u resident=%u\n",frame,hits,misses,decoded,resident);
}
extern "C" void dnm_audio_trace(const char* message) { if(!verbose_log && std::strncmp(message,"audio play ",11)==0)return; log_message(std::strncmp(message,"audio play ",11)!=0,message); }

extern "C" {
struct Probe {
    unsigned tick,rendered,buttons,phase;float x,y,health,camera;unsigned free_memory;
    unsigned checkpoint,kills,kills_goal,combo;float hero_frame;unsigned replay_done,heap_free,presented_tick;
} g_dnm __attribute__((used,externally_visible));
}

extern "C" {
struct PresentationProbe { unsigned source_tick,presentations,midpoint,moving_samples,vcount; };
__attribute__((used,externally_visible)) volatile PresentationProbe g_dnm_60{};
}
int main() {
    std::freopen("STDOUT.LOG","w",stdout);std::freopen("STDERR.LOG","w",stderr);
    std::setvbuf(stderr,nullptr,_IONBF,0);
    std::set_terminate([] {
        const char message[]="Uncaught C++ exception\n";
        int fd=sceIoOpen("FATAL.LOG",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
        if(fd>=0){sceIoWrite(fd,message,sizeof(message)-1);sceIoClose(fd);}
        sceKernelExitGame();
    });
    scePowerSetClockFrequency(333,333,166);perf::init();
    int cb=sceKernelCreateThread("Dad n Me callbacks",callback_thread,0x11,4096,0,nullptr);
    if(cb>=0)sceKernelStartThread(cb,0,nullptr);
    pspDebugScreenInit();pspDebugScreenPrintf("Dad 'n Me - loading original game...\n");
    if(FILE* qa=std::fopen("LOG.QA","r")){verbose_log=true;std::fclose(qa);}
    logfile=std::fopen("PORT.LOG","w");
    if(logfile) std::setvbuf(logfile,nullptr,_IOFBF,65536);
    std::set_new_handler([] {
        if(release_psp_texture())return;
        const char message[]="Allocation failed after evicting all texture memory\n";
        int fd=sceIoOpen("OOM.LOG",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
        if(fd>=0){sceIoWrite(fd,message,sizeof(message)-1);sceIoClose(fd);}
        sceKernelExitGame();
    });
    if(logfile)std::fprintf(logfile,"before renderer heap_free=%u largest=%u\n",unsigned(mallinfo().fordblks),sceKernelMaxFreeMemSize());
    tu_timer::init_timer();gameswf::register_file_opener_callback(open_file);
    gameswf::register_log_callback(log_message);
    auto renderer=make_psp_renderer();renderer->open();gameswf::set_render_handler(renderer);
    auto fonts=make_device_font_provider();gameswf::set_glyph_provider(fonts);
    auto audio=make_psp_audio();
    if(!audio->is_open()){log_message(true,"Audio initialization failed\n");sceKernelExitGame();return 1;}
    gameswf::set_sound_handler(audio);
    gameswf::set_curve_max_pixel_error(.5f);
    gameswf::gc_ptr<gameswf::player> player=new gameswf::player;
    player->set_separate_thread(false);player->set_force_realtime_framerate(false);
    FrameCaptures captures;InputReplay replay;replay.load();
    unsigned trace_frames=0;FILE* trace=nullptr;
    if(FILE* config=std::fopen("TRACE.DNM","rb")){std::fread(&trace_frames,4,1,config);std::fclose(config);trace=std::fopen("ENGINE.CSV","w");}
    if(trace)std::fputs("tick,buttons,rng,root_frame,x,y,hero_frame,kid_x,kid_y,kid_health,kid_frame,pause\n",trace);
    if(replay.loaded){tu_random::seed_random(replay.seed);if(logfile)std::fprintf(logfile,"input replay seed=%u frames=%u\n",replay.seed,replay.total);}
    if(logfile)std::fprintf(logfile,"before SWF heap_free=%u\n",unsigned(mallinfo().fordblks));
    auto root=player->load_file("source.swf");
    if(logfile)std::fprintf(logfile,"after SWF heap_free=%u root=%p\n",unsigned(mallinfo().fordblks),root.get_ptr());
    if(!root) { log_message(true,"Failed to load source.swf\n");sceKernelExitGame();return 1; }
    root->set_display_viewport(59,0,362,272);
    sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    unsigned previous=0;bool paused=false;
    uint64_t next=sceKernelGetSystemTimeWide();
    unsigned clock_fraction=0;
    while(running) {
#if !DNM_SMOOTH_VISUALS || defined(DNM_HEADLESS)
        auto scheduled_now=sceKernelGetSystemTimeWide();
        if(!paused && scheduled_now<next){sceKernelDelayThread(unsigned(std::min<uint64_t>(next-scheduled_now,500)));continue;}
#endif
        perf::begin();unsigned logic_time=0,draw_time=0,finish_time=0;
        SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);
        unsigned buttons=pad.Buttons;
        if(pad.Lx<80)buttons|=PSP_CTRL_LEFT;if(pad.Lx>176)buttons|=PSP_CTRL_RIGHT;
        if(pad.Ly<80)buttons|=PSP_CTRL_UP;if(pad.Ly>176)buttons|=PSP_CTRL_DOWN;
        if(replay.loaded)buttons=replay.buttons(g_dnm.tick);
        if((buttons&~previous)&PSP_CTRL_START){paused=!paused;pause_psp_audio(audio,paused);}
        if(!paused && ((buttons&~previous)&PSP_CTRL_CROSS))dnm::activate_continue(root.get_ptr());
        struct Binding { unsigned mask;gameswf::key::code key; };
        const Binding bindings[]={{PSP_CTRL_LEFT,gameswf::key::LEFT},{PSP_CTRL_RIGHT,gameswf::key::RIGHT},
            {PSP_CTRL_UP,gameswf::key::UP},{PSP_CTRL_DOWN,gameswf::key::DOWN},
            {PSP_CTRL_CROSS,gameswf::key::A},{PSP_CTRL_SQUARE,gameswf::key::S}};
        for(auto b:bindings)if((buttons^previous)&b.mask)player->notify_key_event(b.key,buttons&b.mask);
        previous=buttons;
        uint64_t now=sceKernelGetSystemTimeWide();
        if((g_dnm.rendered & 1u)==0) {
            if(!paused)prepare_psp_tick(g_dnm.tick);
            if(verbose_log && g_dnm.tick<6 && logfile)std::fprintf(logfile,"advance %u begin heap_free=%u used=%u\n",g_dnm.tick,unsigned(mallinfo().fordblks),unsigned(mallinfo().uordblks));
            if(!paused) {
                auto logic_begin=perf::now();audio->advance(1.f/30);root->advance(1.f/30);logic_time=perf::elapsed(logic_begin);++g_dnm.tick;
                if(trace && g_dnm.tick<=trace_frames) {
                    auto game=child(root->get_root_movie(),"game"),hero=child(game,"dude1"),kid=child(game,"kid1");
                    std::fprintf(trace,"%u,%u,%u,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",g_dnm.tick,buttons,tu_random::state_hash(),root->get_current_frame()+1,numeric(hero,"x"),numeric(hero,"y"),numeric(hero,"_currentframe"),numeric(kid,"x"),numeric(kid,"y"),numeric(kid,"health"),numeric(kid,"_currentframe"),numeric(game,"pause"));
                    if(g_dnm.tick==trace_frames){std::fclose(trace);trace=nullptr;}
                }
            }
            if(verbose_log && g_dnm.tick<=6 && logfile)std::fprintf(logfile,"advance %u end heap_free=%u used=%u\n",g_dnm.tick,unsigned(mallinfo().fordblks),unsigned(mallinfo().uordblks));
            next+=33333;
            if(++clock_fraction==3){++next;clock_fraction=0;}
            if(paused || now>next+200000)next=now+33333;
        }
        if(verbose_log && g_dnm.rendered<6 && logfile)std::fprintf(logfile,"display %u begin\n",g_dnm.rendered);
#ifndef DNM_HEADLESS
        gameswf::presentation::begin(g_dnm.tick, paused || (g_dnm.rendered & 1u) ? 1.f : .5f);
        auto draw_begin=perf::now();root->display();draw_time=perf::elapsed(draw_begin);auto finish_begin=perf::now();
        if(verbose_log && g_dnm.rendered<6 && logfile)std::fprintf(logfile,"display %u submitted\n",g_dnm.rendered);
        bool continue_ready=dnm::continue_button(root.get_ptr())!=nullptr;
        finish_psp_frame(paused?"PAUSED\n\n   START: RESUME\n   D-PAD / STICK: MOVE\n   CROSS: LIGHT ATTACK\n   SQUARE: HEAVY ATTACK":(continue_ready?"CROSS: PLAY AGAIN":nullptr),!paused && continue_ready);
        finish_time=perf::elapsed(finish_begin);
#else
        sceDisplayWaitVblankStart();
#endif
        if(verbose_log && g_dnm.rendered<6 && logfile)std::fprintf(logfile,"display %u complete\n",g_dnm.rendered);
        g_dnm.presented_tick=g_dnm.tick;perf::present();
        g_dnm_60.source_tick=g_dnm.tick;g_dnm_60.presentations=g_dnm.rendered+1;g_dnm_60.midpoint=gameswf::presentation::alpha<1;g_dnm_60.moving_samples=gameswf::presentation::moving_samples;g_dnm_60.vcount=sceDisplayGetVcount();captures.capture(g_dnm.tick);++g_dnm.rendered;perf::end(g_dnm.tick,g_dnm.rendered,root->get_current_frame()+1,logic_time,draw_time,finish_time,now>next?unsigned(now-next):0,replay.done);g_dnm.buttons=buttons;g_dnm.phase=root->get_current_frame();
        g_dnm.replay_done=replay.done;
        if(g_dnm.rendered%6==0) {
            g_dnm.free_memory=sceKernelTotalFreeMemSize();
            g_dnm.heap_free=mallinfo().fordblks;
            if(g_dnm.phase==2) {
                g_dnm.x=std::atof(root->get_variable("_root.game.dude1.x"));
                g_dnm.y=std::atof(root->get_variable("_root.game.dude1.y"));
                g_dnm.health=std::atof(root->get_variable("_root.game.dude1.health"));
                g_dnm.camera=std::atof(root->get_variable("_root.game._x"));
                unsigned checkpoint=std::atoi(root->get_variable("_root.game.checkpoint"));
                if(checkpoint!=g_dnm.checkpoint && logfile)std::fprintf(logfile,"checkpoint=%u tick=%u rendered=%u heap_free=%u\n",checkpoint,g_dnm.tick,g_dnm.rendered,g_dnm.heap_free);
                g_dnm.checkpoint=checkpoint;
                g_dnm.kills=std::atoi(root->get_variable("_root.game.kills"));
                g_dnm.kills_goal=std::atoi(root->get_variable("_root.game.kills_goal"));
                g_dnm.combo=std::atoi(root->get_variable("_root.game.combo_count"));
                g_dnm.hero_frame=std::atof(root->get_variable("_root.game.dude1._currentframe"));
            }
        }
#if DNM_SMOOTH_VISUALS && !defined(DNM_HEADLESS)
        next=sceKernelGetSystemTimeWide();
#endif
    }
    root=nullptr;player=nullptr;gameswf::set_sound_handler(nullptr);delete audio;
    gameswf::set_glyph_provider(nullptr);delete fonts;
    gameswf::set_render_handler(nullptr);delete renderer;
    if(trace)std::fclose(trace);
    if(logfile)std::fclose(logfile);
    sceKernelExitGame();return 0;
}
