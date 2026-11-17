#include "frame_profile.hpp"
#include "source_profile.hpp"
#include "boot_trace.hpp"
#include "presentation_ring.hpp"
#include "response_metrics.hpp"
#include "static_loading.hpp"
#include "control_watch.hpp"
#include "gameswf/gameswf_as_classes/as_key.h"
#include "recorded_renderer.hpp"
#include "ending_resource_prefetch.hpp"
#include "capture_pair.hpp"
#include <functional>
#include <atomic>
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
#include <cstddef>
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
PSP_HEAP_SIZE_KB(40960);
// Negative heap sizes use the SDK threshold, not their negative magnitude.
// Reserve space for the512KiB source stack and audio/callback threads.
PSP_HEAP_THRESHOLD_SIZE_KB(2048);
static volatile bool running=true;
static FILE* logfile;static bool verbose_log=false;
static unsigned source_noop_calls=0;
static double numeric(gameswf::as_object* object,const char* name) {
    gameswf::as_value value;if(!object || !object->get_member(name,&value))return 0;return value.to_number();
}
static gameswf::as_object* child(gameswf::as_object* object,const char* name) {
    gameswf::as_value value;if(!object || !object->get_member(name,&value))return nullptr;return value.to_object();
}
static int exit_callback(int,int,void*) {
    // LoadExec requires the registered HOME callback to enter the system exit
    // path. Returning to wait for VM/thread/destructor cleanup can crash the
    // real PSP after HOME. Do not acquire a resource or stdio lock here.
    sceKernelExitGame();return 0;
}
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

namespace {
SceUID gpu_semaphore=-1;
volatile int gpu_owner=-1;
unsigned gpu_depth=0;
void gpu_lock(){
    int me=sceKernelGetThreadId();
    if(gpu_owner==me){++gpu_depth;return;}
    sceKernelWaitSema(gpu_semaphore,1,nullptr);gpu_owner=me;gpu_depth=1;
}
void gpu_unlock(){if(--gpu_depth==0){gpu_owner=-1;sceKernelSignalSema(gpu_semaphore,1);}}
std::function<int()> worker_function;
std::function<void()> loading_request;
bool entered_game=false;
void before_game_tags(){if(!entered_game){entered_game=true;return;}if(loading_request)loading_request();}
int logic_worker(SceSize,void*){return worker_function();}
struct Scene {
    psp60::Frame midpoint,exact;
    unsigned tick=0,phase=0,moving=0,logic_us=0,capture_us=0;
    bool paused=false,continue_ready=false,ending_approach=false;
    unsigned warm_command=0,warm_tile=0;
    uint64_t sampled_us=0,published_us=0;unsigned buttons=0,worker_wait_us=0;
};
}
extern "C" void dnm_set_game_loading_callback(void (*)());
extern "C" void psp60_resource_lock(){gpu_lock();}
extern "C" void psp60_resource_unlock(){gpu_unlock();}
int main() {
    pspboot::begin();
    gpu_semaphore=sceKernelCreateSema("60fps GPU resources",0,1,1,nullptr);
    pspboot::mark("gpu-semaphore",gpu_semaphore);
    if(gpu_semaphore<0){sceKernelExitGame();return 1;}
    std::freopen("STDOUT.LOG","w",stdout);std::freopen("STDERR.LOG","w",stderr);
    std::setvbuf(stderr,nullptr,_IONBF,0);
    std::set_terminate([] {
        pspboot::mark("uncaught-cpp-exception");
        const char message[]="Uncaught C++ exception\n";
        int fd=sceIoOpen("FATAL.LOG",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
        if(fd>=0){sceIoWrite(fd,message,sizeof(message)-1);sceIoClose(fd);}
        sceKernelExitGame();
    });
    int clock_status=scePowerSetClockFrequency(333,333,166);
    pspboot::mark("clock-set",clock_status);
    pspboot::mark("cpu-clock-mhz",scePowerGetCpuClockFrequencyInt());
    pspboot::mark("bus-clock-mhz",scePowerGetBusClockFrequencyInt());
    perf::init();presentation_ring::init();response_metrics::init();
    int cb=sceKernelCreateThread("Dad n Me callbacks",callback_thread,0x11,4096,0,nullptr);
    if(cb>=0)sceKernelStartThread(cb,0,nullptr);
    pspDebugScreenInit();pspDebugScreenPrintf("Dad 'n Me - loading original game...\n");
    if(FILE* qa=std::fopen("LOG.QA","r")){verbose_log=true;std::fclose(qa);}
    logfile=std::fopen("PORT.LOG","w");
    if(logfile) std::setvbuf(logfile,nullptr,_IOFBF,65536);
    std::set_new_handler([] {
        gpu_lock();bool released=release_psp_texture();gpu_unlock();if(released)return;
        pspboot::mark("engine-heap-exhausted");
        const char message[]="Allocation failed after evicting all texture memory\n";
        int fd=sceIoOpen("OOM.LOG",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
        if(fd>=0){sceIoWrite(fd,message,sizeof(message)-1);sceIoClose(fd);}
        sceKernelExitGame();
    });
    if(logfile)std::fprintf(logfile,"before renderer heap_free=%u largest=%u\n",unsigned(mallinfo().fordblks),sceKernelMaxFreeMemSize());
    tu_timer::init_timer();gameswf::register_file_opener_callback(open_file);
    gameswf::register_log_callback(log_message);
    auto renderer=make_psp_renderer();renderer->open();
    pspboot::mark("renderer-ready");
    psp60::Recorder recorder(renderer,gpu_lock,gpu_unlock);gameswf::set_render_handler(&recorder);
    auto fonts=make_device_font_provider();gameswf::set_glyph_provider(fonts);set_psp_port_font(fonts,renderer);
    auto audio=make_psp_audio();
    pspboot::mark(audio->is_open()?"audio-ready":"audio-failed");
    if(!audio->is_open()){log_message(true,"Audio initialization failed\n");sceKernelExitGame();return 1;}
    gameswf::set_sound_handler(audio);
    gameswf::set_curve_max_pixel_error(.5f);
    gameswf::gc_ptr<gameswf::player> player=new gameswf::player;
    player->set_separate_thread(false);player->set_force_realtime_framerate(false);
    FrameCaptures captures;InputReplay replay;replay.load();
    bool benchmark_exit=false;
    if(replay.loaded)if(FILE* f=std::fopen("BENCHMARK.QA","r")){benchmark_exit=true;std::fclose(f);}
    SourceProfile source_profile;source_profile.init(benchmark_exit&&perf::enabled,replay.total);
    unsigned stall_tick=0,stall_us=0;
    if(replay.loaded)if(FILE* f=std::fopen("SOURCE_STALL.QA","r")){std::fscanf(f,"%u%u",&stall_tick,&stall_us);std::fclose(f);}
    unsigned replay_to_live=0;if(replay.loaded)if(FILE* f=std::fopen("REPLAY_TO_LIVE.QA","r")){std::fscanf(f,"%u",&replay_to_live);std::fclose(f);}
    unsigned trace_frames=0;FILE* trace=nullptr;
    if(FILE* config=std::fopen("TRACE.DNM","rb")){std::fread(&trace_frames,4,1,config);std::fclose(config);trace=std::fopen("ENGINE.CSV","w");}
    if(trace)std::fputs("tick,buttons,rng,root_frame,x,y,hero_frame,kid_x,kid_y,kid_health,kid_frame,pause\n",trace);
    if(replay.loaded){tu_random::seed_random(replay.seed);if(logfile)std::fprintf(logfile,"input replay seed=%u frames=%u\n",replay.seed,replay.total);}
    if(logfile)std::fprintf(logfile,"before SWF heap_free=%u\n",unsigned(mallinfo().fordblks));
    dnm_set_game_loading_callback(before_game_tags);
    auto root=player->load_file("source.swf");
    pspboot::mark(root?"source-loaded":"source-load-failed");
    if(logfile)std::fprintf(logfile,"after SWF heap_free=%u root=%p\n",unsigned(mallinfo().fordblks),root.get_ptr());
    if(!root) { log_message(true,"Failed to load source.swf\n");sceKernelExitGame();return 1; }
    root->set_display_viewport(59,0,362,272);
    sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    unsigned previous=0;bool paused=false;
    constexpr unsigned SceneSlots=DNM_SCENE_SLOTS;
    Scene scenes[SceneSlots];std::atomic<int> active{-1};std::atomic<unsigned> produced{0},consumed{0};
    SceUID permit=sceKernelCreateSema("30Hz source permit",0,SceneSlots,SceneSlots,nullptr);
    pspboot::mark("source-semaphore",permit);
    if(permit<0){sceKernelExitGame();return 1;}
    StaticLoading loading;LoadingInputGate input_gate;
    static_assert(sizeof(StaticLoading::Row)==80&&offsetof(StaticLoading::Row,resumed_us)==64&&offsetof(StaticLoading::Row,visible)==72);
    presentation_ring::loading_address(&loading);
    if(FILE* f=std::fopen("LOAD_CAPTURE.QA","r")){loading.capture=true;std::fclose(f);}
    loading.log=loading.capture||trace||perf::enabled;
    if(FILE* f=std::fopen("LOADING_LOG.QA","r")){loading.log=true;std::fclose(f);}
    worker_function=[&](){
        dnm::IntroPauseGuard intro_guard;dnm::ControlWatch control_watch;
        struct Binding { unsigned mask;gameswf::key::code key; };
        const Binding bindings[]={{PSP_CTRL_LEFT,gameswf::key::LEFT},{PSP_CTRL_RIGHT,gameswf::key::RIGHT},
            {PSP_CTRL_UP,gameswf::key::UP},{PSP_CTRL_DOWN,gameswf::key::DOWN},
            {PSP_CTRL_CROSS,gameswf::key::A},{PSP_CTRL_SQUARE,gameswf::key::S}};

      loading_request=[&](){
          unsigned id=loading.request(g_dnm.tick+1,1,sceKernelGetSystemTimeWide());while(running&&loading.acknowledged.load(std::memory_order_acquire)<id)sceKernelDelayThread(100);loading.began(sceKernelGetSystemTimeWide());
          if(!replay.loaded){SceCtrlData held{};sceCtrlPeekBufferPositive(&held,1);input_gate.block(previous,128,128);input_gate.block(held.Buttons,held.Lx,held.Ly);for(auto b:bindings)if(previous&b.mask)player->notify_key_event(b.key,false);previous=0;}
      };
      while(running){
        auto wait_begin=response_metrics::now();
        sceKernelWaitSema(permit,1,nullptr);if(!running)break;
        unsigned worker_wait=response_metrics::enabled?unsigned(response_metrics::now()-wait_begin):0;
        int slot=produced.load(std::memory_order_relaxed)%SceneSlots;
        Scene& scene=scenes[slot];unsigned logic_time=0;
        SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);
        const unsigned raw_lx=pad.Lx,raw_ly=pad.Ly;
        const auto sampled_at=sceKernelGetSystemTimeWide();
        auto profile_begin=source_profile.stamp();
        scene.sampled_us=response_metrics::enabled?sampled_at:0;scene.worker_wait_us=worker_wait;
        // Explicit QA-only handoff: deterministic source keys reach Game Over,
        // then real emulated pad events test the normal release/re-arm path.
        if(replay.loaded&&replay_to_live&&g_dnm.tick>=replay_to_live){
            for(auto b:bindings)if(previous&b.mask)player->notify_key_event(b.key,false);
            previous=0;replay.loaded=false;replay.done=false;replay_to_live=0;
        }
        unsigned ax=pad.Lx,ay=pad.Ly;if(!replay.loaded)input_gate.axes(ax,ay);pad.Lx=ax;pad.Ly=ay;
        unsigned buttons=pad.Buttons;
        // Menus use discrete D-pad selection. Keep analog movement in battle.
        if(root->get_current_frame()==2&&!paused){
            if(pad.Lx<80)buttons|=PSP_CTRL_LEFT;if(pad.Lx>176)buttons|=PSP_CTRL_RIGHT;
            if(pad.Ly<80)buttons|=PSP_CTRL_UP;if(pad.Ly>176)buttons|=PSP_CTRL_DOWN;
        }
        if(replay.loaded)buttons=replay.buttons(g_dnm.tick);else buttons=input_gate.filter(buttons);
        unsigned edge=buttons&~previous;
        bool resume=paused&&(edge&(PSP_CTRL_START|PSP_CTRL_CROSS|PSP_CTRL_CIRCLE));
        if((edge&PSP_CTRL_START)||resume){
            paused=!paused;pause_psp_audio(audio,paused);
            if(resume&&!replay.loaded){
                input_gate.block(buttons,pad.Lx,pad.Ly);buttons=input_gate.filter(buttons);
            }
        }
        if(!paused&&((buttons&~previous)&PSP_CTRL_CROSS))dnm::activate_continue(root.get_ptr());
        for(auto b:bindings)if((buttons^previous)&b.mask)player->notify_key_event(b.key,buttons&b.mask);
        previous=buttons;
        unsigned source_keys_down=0;
        if(!replay.loaded){
            gameswf::as_value kv;player->get_global()->get_member("Key",&kv);
            if(auto k=gameswf::cast_to<gameswf::as_key>(kv.to_object()))for(auto b:bindings)if(k->is_key_down(b.key))source_keys_down|=b.mask;
        }
        unsigned control_reason=0;
        auto profile_logic_begin=source_profile.stamp();
        {
            // Only the intro cleanup mutates GPU-owned resources here. Ordinary
            // logic may overlap the presentation thread's completed-list wait;
            // Recorder resource creation and allocation recovery keep their locks.
            if(!paused&&g_dnm.tick==2){gpu_lock();prepare_psp_tick(g_dnm.tick);gpu_unlock();}
            if(verbose_log && g_dnm.tick<6 && logfile)std::fprintf(logfile,"advance %u begin heap_free=%u used=%u\n",g_dnm.tick,unsigned(mallinfo().fordblks),unsigned(mallinfo().uordblks));
            if(!paused) {
                auto logic_begin=sceKernelGetSystemTimeWide();audio->advance(1.f/30);root->advance(1.f/30);if(loading.worker_id&&!replay.loaded)buttons=0;logic_time=unsigned(sceKernelGetSystemTimeWide()-logic_begin);++g_dnm.tick;
                auto game=gameswf::cast_to<gameswf::sprite_instance>(child(root->get_root_movie(),"game"));
                control_reason=intro_guard.step(root->get_current_frame()==2?game:nullptr);
                logic_time=unsigned(sceKernelGetSystemTimeWide()-logic_begin);
                if(trace && g_dnm.tick<=trace_frames) {
                    auto game=child(root->get_root_movie(),"game"),hero=child(game,"dude1"),kid=child(game,"kid1");
                    std::fprintf(trace,"%u,%u,%u,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",g_dnm.tick,buttons,tu_random::state_hash(),root->get_current_frame()+1,numeric(hero,"x"),numeric(hero,"y"),numeric(hero,"_currentframe"),numeric(kid,"x"),numeric(kid,"y"),numeric(kid,"health"),numeric(kid,"_currentframe"),numeric(game,"pause"));
                    if(g_dnm.tick==trace_frames){std::fclose(trace);trace=nullptr;}
                }
            }
            if(verbose_log && g_dnm.tick<=6 && logfile)std::fprintf(logfile,"advance %u end heap_free=%u used=%u\n",g_dnm.tick,unsigned(mallinfo().fordblks),unsigned(mallinfo().uordblks));
        }

        auto profile_logic_end=source_profile.stamp();
        if(!replay.loaded)control_watch.sample(root.get_ptr(),g_dnm.tick,pad.Buttons,buttons,source_keys_down,input_gate.held,input_gate.analog,raw_lx,raw_ly,
            sceKernelGetSystemTimeWide(),control_reason,(pad.Buttons&(PSP_CTRL_SELECT|PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER))==(PSP_CTRL_SELECT|PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER),paused);
        auto captured=sceKernelGetSystemTimeWide();
        recorder.record(scene.exact);gameswf::presentation::begin(g_dnm.tick,1.f);root->display();recorder.finish();
        scene.moving=gameswf::presentation::moving_samples;
        scene.capture_us=unsigned(sceKernelGetSystemTimeWide()-captured);
        auto profile_captured=source_profile.stamp();
        scene.tick=g_dnm.tick;scene.phase=root->get_current_frame()+1;scene.paused=paused;
        scene.continue_ready=dnm::continue_button(root.get_ptr())!=nullptr;
        scene.logic_us=logic_time;
        scene.warm_command=scene.warm_tile=0;
        g_dnm.buttons=buttons;g_dnm.phase=root->get_current_frame();g_dnm.replay_done=replay.done;
        if(g_dnm.tick%4==0) {
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
        if(g_dnm.tick==1)pspboot::mark("first-source-scene");
        // Confirm against this private VM state: the public sparse probe may
        // still contain checkpoint11 immediately after starting a new game.
        scene.ending_approach=scene.phase==3&&g_dnm.checkpoint==11&&
            numeric(child(root->get_root_movie(),"game"),"checkpoint")==11;
        // Spend remaining source lookahead on preparation, without adding
        // queue slots or delaying hot frames. Keep the existing4ms minimum;
        // a cold frame may use up to20ms, with2ms before its source deadline.
        // An indivisible tile decode can exceed this soft limit.
        if(!scene.paused&&scene.phase==3&&scene.tick>SceneSlots){
            auto now=sceKernelGetSystemTimeWide();
            auto source_deadline=sampled_at+1000000ull*DNM_SOURCE_LOOKAHEAD/30-2000;
            auto spare=source_deadline>now?source_deadline-now:0;
            auto deadline=now+std::min(20000ull,std::max(4000ull,spare));
            gpu_lock();prepare_psp_frame_resources(scene.exact,scene.warm_command,scene.warm_tile,deadline);gpu_unlock();
        }
        source_profile.append(scene.tick,scene.logic_us,scene.capture_us,profile_begin,profile_logic_begin,
            profile_logic_end,profile_captured,source_profile.stamp(),scene.exact.commands.size(),scene.exact.coordinates.size());
        if(stall_tick&&scene.tick==stall_tick){stall_tick=0;sceKernelDelayThread(stall_us);}
        scene.buttons=buttons;scene.published_us=response_metrics::now();
        produced.fetch_add(1,std::memory_order_release);
        if(loading.worker_id){
            if(!replay.loaded){SceCtrlData tail{};sceCtrlPeekBufferPositive(&tail,1);unsigned held=tail.Buttons;if(tail.Lx<80)held|=PSP_CTRL_LEFT;if(tail.Lx>176)held|=PSP_CTRL_RIGHT;if(tail.Ly<80)held|=PSP_CTRL_UP;if(tail.Ly>176)held|=PSP_CTRL_DOWN;input_gate.block(held,tail.Lx,tail.Ly);previous=0;}
            loading.ready(sceKernelGetSystemTimeWide());
        }
      }
      return 0;
    };
    int worker=sceKernelCreateThread("Dad 30Hz source",logic_worker,0x28,512*1024,PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU,nullptr);
    pspboot::mark("source-thread-create",worker);
    if(worker<0){log_message(true,"Cannot create source worker\n");sceKernelExitGame();return 1;}
    int worker_started=sceKernelStartThread(worker,0,nullptr);
    pspboot::mark("source-thread-start",worker_started);
    if(worker_started<0){sceKernelDeleteThread(worker);sceKernelExitGame();return 1;}
    sceKernelChangeThreadPriority(0,0x18);
    CapturePair pair_capture;unsigned subframe=0;bool fresh=false,boot_presented=false;
    FILE* timing=nullptr;if(perf::enabled){timing=std::fopen("PRESENT60.CSV","w");if(timing){std::setvbuf(timing,nullptr,_IOFBF,32768);std::fputs("presentation,source_tick,subframe,fresh,moving_samples,vcount,time_us,logic_us,capture_us\n",timing);}}
    unsigned shown_load=0,resume_load=0;bool resume_submitted=false;
    const auto ending_resources=dnm::first_ending_resources();
    unsigned ending_command=0,ending_tile=0;bool ending_warmed=false;
    // Keep the known opening cache/heap order, then retire the surplus permits
    // once. This bounds active+future/in-flight scenes without polling or ever
    // overwriting the immutable frame currently owned by the renderer.
    unsigned opening_permits_to_retire=SceneSlots-1-DNM_SOURCE_LOOKAHEAD;
    while(running&&produced.load(std::memory_order_acquire)<SceneSlots-1&&!loading.pending())sceKernelDelayThread(100);
    // Populate the texture cache from the already prepared opening scenes before
    // starting presentation. These draws stay in the back buffer: no source
    // advances, queue slots, input samples or display refreshes are consumed.
    if(running&&!loading.pending()){
        unsigned ready=produced.load(std::memory_order_acquire);
        gpu_lock();
        for(unsigned scene=0;scene<ready;++scene){
            scenes[scene%SceneSlots].exact.play(renderer,1.f);
            submit_psp_frame(nullptr);
        }
        gpu_unlock();
    }
    while(running){
        if(shown_load){if(loading.completed.load(std::memory_order_acquire)<shown_load||produced.load(std::memory_order_acquire)==consumed.load(std::memory_order_acquire)){sceKernelDelayThread(100);continue;}resume_load=shown_load;shown_load=0;resume_submitted=false;}
        if(!subframe&&loading.pending()&&produced.load(std::memory_order_acquire)==consumed.load(std::memory_order_acquire)){
            unsigned id=loading.requested.load(std::memory_order_acquire);int previous_scene=active.load(std::memory_order_acquire);
            if(previous_scene<0){running=false;break;}
            gpu_lock();scenes[previous_scene].exact.play(renderer,1.f);submit_psp_loading();gpu_unlock();present_psp_frame();
            gpu_lock();bool visible=confirm_psp_frame_visible();auto shown_at=sceKernelGetSystemTimeWide();unsigned shown_vcount=sceDisplayGetVcount();if(visible&&loading.capture){char name[48];std::snprintf(name,sizeof(name),"LOADING_%03u.RGBA",id);dump_psp_frame(name);}gpu_unlock();
            loading.shown(id,shown_at,shown_vcount,visible);if(visible)shown_load=id;continue;
        }
        if(subframe==0){
            unsigned read=consumed.load(std::memory_order_relaxed);
            fresh=produced.load(std::memory_order_acquire)>read;
            if(fresh){
                int old=active.exchange(read%SceneSlots,std::memory_order_acq_rel);consumed.store(read+1,std::memory_order_release);
                if(old>=0){if(opening_permits_to_retire)--opening_permits_to_retire;else sceKernelSignalSema(permit,1);}
            }
        }
        int slot=active.load(std::memory_order_acquire);
        if(slot<0){sceKernelDelayThread(100);continue;}
        const Scene& scene=scenes[slot];perf::begin();auto drawn=perf::now();
        unsigned response_draw=0,response_submit=0,draw_time=0;auto finished=perf::now();
        if(!scene.ending_approach){ending_command=ending_tile=0;ending_warmed=false;}
        if(fresh){
        gpu_lock();auto measured_draw=response_metrics::now();
        scene.exact.play(renderer,subframe||!fresh||scene.paused?1.f:.5f);
        response_draw=response_metrics::enabled?unsigned(response_metrics::now()-measured_draw):0;
        auto measured_submit=response_metrics::now();
        draw_time=perf::elapsed(drawn);finished=perf::now();
        submit_psp_frame(scene.paused?"PAUSED\n\n   > RESUME\n\n   D-PAD: SELECT\n   CROSS / START: CONFIRM\n   CIRCLE: BACK":(scene.continue_ready?"D-PAD: SELECT  CROSS: PLAY AGAIN":nullptr),!scene.paused&&scene.continue_ready);
        response_submit=response_metrics::enabled?unsigned(response_metrics::now()-measured_submit):0;
        gpu_unlock();
        // Prepare only already-published ending frames in unused refresh time.
        // Source lookahead is unchanged; a texture decode is indivisible.
        auto prior_present=psp_present_time();
        if(prior_present&&!loading.pending()&&!shown_load&&!resume_load){
            unsigned long long now=sceKernelGetSystemTimeWide();
            auto deadline=std::min(prior_present+11000,now+6000);
            unsigned next_scene=consumed.load(std::memory_order_relaxed),end=produced.load(std::memory_order_acquire);
            for(;next_scene<end&&sceKernelGetSystemTimeWide()<deadline;++next_scene){
                Scene& future=scenes[next_scene%SceneSlots];
                if(future.phase!=5||future.paused)continue;
                gpu_lock();prepare_psp_frame_resources(future.exact,future.warm_command,future.warm_tile,deadline);gpu_unlock();
            }
            // The authored final checkpoint precedes the ending transition.
            // Warm its first visible resources gradually in unused refresh time,
            // sharing the existing bounded LRU instead of adding input backlog.
            if(scene.ending_approach&&!scene.paused&&!ending_warmed){
                gpu_lock();ending_warmed=prepare_psp_frame_resources(ending_resources,ending_command,ending_tile,deadline);gpu_unlock();
            }
        }
        present_psp_frame();
        }else{
            // The previous exact scene is already visible and immutable. Keep
            // scanout on that front buffer, without redrawing it or swapping in
            // an older back buffer. This leaves CPU/GE time for the source VM.
            finished=perf::now();hold_psp_frame();
        }
        if(!boot_presented){pspboot::mark("first-scene-presented");boot_presented=true;}
        if(resume_load){if(!resume_submitted){loading.submitted(resume_load,psp_present_time());resume_submitted=true;}else{loading.resumed(resume_load,psp_present_time(),psp_present_vcount());resume_load=0;}}
        g_dnm.presented_tick=scene.tick;perf::present();++g_dnm.rendered;
        g_dnm_60.source_tick=scene.tick;g_dnm_60.presentations=g_dnm.rendered;
        g_dnm_60.midpoint=!subframe&&fresh;g_dnm_60.moving_samples=subframe || !fresh?0:scene.moving;g_dnm_60.vcount=psp_present_vcount();
        presentation_ring::append(scene.tick,subframe,fresh,subframe||!fresh?0:scene.moving,psp_present_vcount(),psp_present_time());
        response_metrics::append({scene.tick,scene.buttons,produced.load(std::memory_order_acquire)-consumed.load(std::memory_order_acquire),subframe,unsigned(fresh),scene.phase,scene.logic_us,scene.capture_us,response_draw,response_submit,scene.worker_wait_us,g_dnm.tick,scene.sampled_us,scene.published_us,psp_present_time()});
        if(timing){std::fprintf(timing,"%u,%u,%u,%u,%u,%u,%llu,%u,%u\n",g_dnm.rendered,scene.tick,subframe,unsigned(fresh),subframe || !fresh?0:scene.moving,psp_present_vcount(),psp_present_time(),scene.logic_us,scene.capture_us);if(g_dnm.rendered%120==0)std::fflush(timing);}
        gpu_lock();pair_capture.capture(scene.tick,subframe);captures.capture(scene.tick);gpu_unlock();
        perf::end(scene.tick,g_dnm.rendered,scene.phase,subframe?0:scene.logic_us,draw_time,perf::elapsed(finished),0,replay.done&&scene.tick>=replay.total&&subframe);
        // Explicit device benchmark only: finish its bounded capture and return
        // to XMB once the fixed input is presented. Ordinary play has no flag.
        if(benchmark_exit&&replay.done&&scene.tick>=replay.total&&subframe){
            if(timing){std::fclose(timing);timing=nullptr;}
            source_profile.flush();
            pspboot::mark("benchmark-complete");sceKernelExitGame();
        }
        subframe^=1;
    }
    if(timing)std::fclose(timing);running=false;sceKernelSignalSema(permit,1);sceKernelWaitThreadEnd(worker,nullptr);sceKernelDeleteThread(worker);
    worker_function={};loading_request={};dnm_set_game_loading_callback(nullptr);
    root=nullptr;player=nullptr;gameswf::set_sound_handler(nullptr);delete audio;
    gameswf::set_glyph_provider(nullptr);delete fonts;
    gameswf::set_render_handler(nullptr);delete renderer;
    if(trace)std::fclose(trace);
    if(logfile)std::fclose(logfile);
    sceKernelExitGame();return 0;
}
