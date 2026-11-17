#include "source_vm.hpp"
#include "presentation_phase119.hpp"
#include "matrix_chain117_qa.hpp"
#include "boot_trace.hpp"
#include "static_loading.hpp"
#include "storage.hpp"
#include "audio_psp.hpp"
#include "render_psp.hpp"
#include "render_capture.hpp"
#include "capture_pair.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <new>
#include <exception>
#include <string>
#include <functional>
#include <atomic>
#include <algorithm>
#include <pspkernel.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include "source_profile.hpp"
#include "source_clock.hpp"
#include "scene_mailbox.hpp"
#include "gc_policy.hpp"
#include "gc_publication.hpp"
#include "picture_settings.hpp"
#if DE_WLAN
#include "wlan_frontend.hpp"
#endif
PSP_MODULE_INFO("Double Edged B13",PSP_MODULE_USER,0,2);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(40960);
PSP_HEAP_THRESHOLD_SIZE_KB(3072);
extern "C" int de_render_init();
extern "C" void de_render_startup_loading();
extern "C" int de_render_capture(const char*);
extern "C" void de_render_shutdown();
struct DebugState {uint32_t stage,frames,buttons,root_frame,rng,heap,peak,cpu_us;};
extern "C" {
__attribute__((used,externally_visible)) volatile DebugState g_double_edged_debug{};
struct PresentationProbe {unsigned source_tick,presentations,midpoint,fresh,vcount;};
__attribute__((used,externally_visible)) volatile PresentationProbe g_double_edged_60{};
}
namespace {
volatile bool exiting=false;
SceUID gpu_semaphore=-1;volatile int gpu_owner=-1;unsigned gpu_depth=0;
void gpu_lock(){int me=sceKernelGetThreadId();if(gpu_owner==me){++gpu_depth;return;}sceKernelWaitSema(gpu_semaphore,1,nullptr);gpu_owner=me;gpu_depth=1;}
void gpu_unlock(){if(--gpu_depth==0){gpu_owner=-1;sceKernelSignalSema(gpu_semaphore,1);}}
int exit_callback(int,int,void*){exiting=true;return 0;}
int callback_thread(SceSize,void*){int id=sceKernelCreateCallback("Double Edged exit",exit_callback,nullptr);sceKernelRegisterExitCallback(id);sceKernelSleepThreadCB();return 0;}
bool present(const char* file){FILE* f=std::fopen(file,"rb");if(!f)return false;std::fclose(f);return true;}
void failure(const char* text){if(FILE* f=std::fopen("ERROR.LOG","w")){std::fputs(text,f);std::fclose(f);}while(!exiting){gpu_lock();draw_psp_message(text);gpu_unlock();sceKernelDelayThreadCB(1000);}}
std::function<int()> worker_function;
std::function<void(unsigned)> loading_request;
JSValue loading_begin(JSContext* c,JSValueConst,int argc,JSValueConst* argv){uint32_t reason=0;if(argc&&JS_ToUint32(c,&reason,argv[0]))return JS_EXCEPTION;if(loading_request)loading_request(reason);return JS_UNDEFINED;}

int source_worker(SceSize,void*){return worker_function();}
struct Scene {
    psp60::Frame frame;
    unsigned source_tick=0,root_frame=0,errors=0,logic_us=0,capture_us=0,gc_us=0,heap_peak=0;
    uint64_t sample_us=0,scheduled_us=0;
    float pointer_x=0,pointer_y=0;
    bool pointer=false,help=false,smooth_picture=true;
    unsigned picture_epoch=0;
};
struct VisibleScene {unsigned tick=0;bool fresh=false;uint64_t sample_us=0;};
struct PresentSample {
    unsigned flags,vcount,visible_tick,visible_flags;
    uint64_t time_us,sample_us,scheduled_us,visible_us,visible_sample_us;
};
static_assert(sizeof(PresentSample)==56);
struct PresentCapture {
    std::unique_ptr<PresentSample[]> rows;
    unsigned used=0,capacity=0;bool bounded=false,overflow=false;
    bool init(bool enabled,unsigned limit,bool replay){
        if(!enabled)return true;bounded=replay;
        capacity=replay?std::min(65536u,std::max(8192u,limit*3)):36000u;
        rows.reset(new(std::nothrow) PresentSample[capacity]);return bool(rows);
    }
    void append(const Scene& scene,unsigned subframe,bool fresh,bool submitted,const VisibleScene& visible,bool changed){
        if(!rows)return;
        if(used==capacity){if(bounded)overflow=true;return;}
        rows[used++]={(scene.source_tick<<4)|(unsigned(submitted)<<3)|(unsigned(scene.help)<<2)|(subframe<<1)|unsigned(fresh),psp_present_vcount(),visible.tick,unsigned(changed)|(unsigned(psp_latch_confirmed())<<1),psp_present_time(),scene.sample_us,scene.scheduled_us,psp_latch_time(),visible.sample_us};
    }
    bool write()const{
        if(!rows)return true;
        FILE* file=std::fopen("PRESENT60.CSV","w");if(!file)return false;
        std::setvbuf(file,nullptr,_IOFBF,32768);
        bool valid=std::fprintf(file,"presentation,source_tick,subframe,fresh,vcount,time_us,help,submitted,sample_us,scheduled_us,visible_source_tick,visible_fresh,visible_confirmed,visible_time_us,visible_sample_us\n")>0;
        for(unsigned i=0;i<used&&valid;++i){auto& r=rows[i];valid=std::fprintf(file,"%u,%u,%u,%u,%u,%llu,%u,%u,%llu,%llu,%u,%u,%u,%llu,%llu\n",i+1,r.flags>>4,(r.flags>>1)&1,r.flags&1,r.vcount,(unsigned long long)r.time_us,(r.flags>>2)&1,(r.flags>>3)&1,(unsigned long long)r.sample_us,(unsigned long long)r.scheduled_us,r.visible_tick,r.visible_flags&1,(r.visible_flags>>1)&1,(unsigned long long)r.visible_us,(unsigned long long)r.visible_sample_us)>0;}
        return std::fclose(file)==0&&valid;
    }
};
void observation(SourceVM& vm,JSValueConst snapshot,FILE* trace){
    auto c=vm.context();JSValue value=JS_Call(c,snapshot,JS_UNDEFINED,0,nullptr);
    if(JS_IsException(value)){vm.capture_exception();JS_FreeValue(c,value);return;}
    if(trace){if(const char* text=JS_ToCString(c,value)){std::fprintf(trace,"%s\n",text);JS_FreeCString(c,text);}}
    JS_FreeValue(c,value);
}
}
int main(){
    pspboot::begin();
    std::set_terminate([]{pspboot::mark("uncaught-cpp-exception");sceKernelExitGame();for(;;)sceKernelDelayThread(100000);});
    int cb=sceKernelCreateThread("Double Edged callbacks",callback_thread,0x11,4096,PSP_THREAD_ATTR_USER,nullptr);if(cb>=0)sceKernelStartThread(cb,0,nullptr);
    scePowerSetClockFrequency(333,333,166);sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    gpu_semaphore=sceKernelCreateSema("Double Edged GPU resources",0,1,1,nullptr);
    pspboot::mark("gpu-semaphore",gpu_semaphore);
    if(gpu_semaphore<0){sceKernelExitGame();return 1;}
    draw_psp_boot_loading();
    // The SDK console is available before the asset/GE renderer. Let this
    // static frame scan out before beginning cold resource initialization.
    sceDisplayWaitVblankStart();sceDisplayWaitVblankStart();
    if(present("LOAD_CAPTURE.QA")){void* shown=nullptr;int stride=0,format=0;if(sceDisplayGetFrameBuf(&shown,&stride,&format,0)>=0&&shown&&stride>=480)if(FILE* f=std::fopen("BOOT_LOADING.RGBA","wb")){auto pixels=static_cast<const uint32_t*>(shown);for(int y=0;y<272;y++)std::fwrite(pixels+y*stride,4,480,f);std::fclose(f);}}
    bool qa=present("TRACE.QA"),replay=qa&&present("REPLAY.QA");
    bool measure=present("PERF.QA");
    unsigned input_profile_limit=0;if(FILE* f=std::fopen("INPUT_PROFILE.QA","r")){std::fscanf(f,"%u",&input_profile_limit);std::fclose(f);if(input_profile_limit>65536)input_profile_limit=0;}
    const bool input_profile=input_profile_limit>0;
    set_psp_latch_probe(qa||measure||input_profile);
    FILE* trace=qa?std::fopen("FRONTEND.JSONL","w"):nullptr;
    uint32_t seed=uint32_t(sceKernelGetSystemTimeWide());if(FILE* f=std::fopen("SEED.QA","r")){unsigned long n=0;if(std::fscanf(f,"%lu",&n)==1)seed=uint32_t(n);std::fclose(f);}
    bool startup_picture_mode=depicture::load();
    if(qa&&present("FAST_FILTERS.QA"))startup_picture_mode=true;
    if(qa&&present("ORIGINAL_FILTERS.QA"))startup_picture_mode=false;
    set_fast_color_filters(startup_picture_mode);
    de_render_startup_loading();
    if(de_render_init()){failure("Cannot load original drawing resources.");sceKernelExitGame();return 1;}
    pspboot::mark("renderer-ready");
    de_enable_capture(gpu_lock,gpu_unlock);
    constexpr unsigned SceneSlots=DE_SCENE_SLOTS;
    Scene scenes[SceneSlots];std::atomic<unsigned> produced{0},consumed{0};std::atomic<int> active{-1};
    SceneMailbox<SceneSlots> mailbox;
    std::atomic<bool> failed{false},source_done{false};std::string worker_error;
    std::unique_ptr<SourceVM> vm;
#if DE_WLAN
    // The worker owns both the VM and transport. The renderer consumes only
    // immutable scenes and these scalar release/acquire publications.
    std::atomic<unsigned> link_error_status{0},link_final_checksum{0},link_role{0};
#endif
    uint32_t replay_limit=0;
    SourceCapture source_timing;
    const bool profile_source=(qa&&replay&&present("SOURCE_PROFILE.QA"))||input_profile;
    SceUID permit=sceKernelCreateSema("Double Edged source permit",0,SceneSlots,SceneSlots,nullptr);
    pspboot::mark("source-semaphore",permit);
    if(permit<0){sceKernelExitGame();return 1;}
    StaticLoading loading;LoadingInputGate input_gate;
    loading.capture=present("LOAD_CAPTURE.QA");loading.log=loading.capture||qa||measure||present("LOADING_LOG.QA");
    worker_function=[&](){
        unsigned early_gc_publications=0;
        unsigned frames=0,last_buttons=0,picture_epoch=0;bool picture_mode=startup_picture_mode;
        loading_request=[&](unsigned reason){if(loading.worker_id)return;unsigned id=loading.request(frames+1,reason,sceKernelGetSystemTimeWide());while(!exiting&&loading.acknowledged.load(std::memory_order_acquire)<id)sceKernelDelayThread(100);loading.began(sceKernelGetSystemTimeWide());};
#if !DE_WLAN
        loading_request(0);
#endif

        // Construct the QuickJS runtime on the thread that owns every script
        // call, so its native stack limit refers to this 768 KiB stack.
        vm=std::make_unique<SourceVM>();auto c=vm->context();
        auto fail=[&](const std::string& message){worker_error=message;failed.store(true,std::memory_order_release);};
        if(!c){fail(vm->error());return 1;}
        de_storage_bind(c);de_bind_render_mirror(c);
        JSValue load_global=JS_GetGlobalObject(c);JS_SetPropertyStr(c,load_global,"nativeBeginStaticLoading",JS_NewCFunction(c,loading_begin,"nativeBeginStaticLoading",1));JS_FreeValue(c,load_global);
#if DE_WLAN
        if(replay){fail("Use the solo package for an isolated source replay.");return 1;}
        g_double_edged_debug.stage=1;
        doubleedged::WlanFrontend wire;
        // No source scene has been published yet. The main thread is waiting
        // in prefill; serialize the lobby's GE use with the normal GPU lock.
        gpu_lock();bool connected=wire.connect(*vm,qa,exiting);gpu_unlock();
        if(!connected){if(!vm->error().empty())fail(vm->error());else exiting=true;return 0;}
        link_role.store(wire.session.host()?1:2,std::memory_order_release);
        seed=wire.session.setup().seed;
        if(!wire.apply_profile(*vm)||!vm->eval_file("link_clock.js")){fail(vm->error());return 1;}
        loading_request(0);
        auto lose_link=[&](){
            de_audio_pause(true);wire.probe(g_double_edged_link.checksum);
            if(FILE* f=std::fopen("LINK_ERROR.JSON","w")){
                std::fprintf(f,"{\"status\":%u,\"error\":%d,\"source_frames\":%u}\n",unsigned(wire.session.status()),wire.session.error(),frames);std::fclose(f);
            }
            link_error_status.store(unsigned(wire.session.status()),std::memory_order_release);
        };
#endif
        if(de_audio_bind(c)){fail("Cannot load original audio bank.");return 1;}
        pspboot::mark("audio-ready");
        if((replay&&!vm->eval_file("qa-date.js"))||!vm->initialize(seed,!qa&&!DE_WLAN)||!vm->eval_file("pad.js")||(replay&&!vm->eval_file("fixture.js"))){fail(vm->error());return 1;}
#if DE_WLAN
        if(!vm->eval_file("link_input.js")||(qa&&present("observation.js")&&!vm->eval_file("observation.js"))){fail(vm->error());return 1;}
#endif
        pspboot::mark("source-initialized");
        JSValue global=JS_GetGlobalObject(c),step=JS_GetPropertyStr(c,global,DE_WLAN?"pspLinkStep":replay?"sceneStep":"pspStep"),snapshot=JS_GetPropertyStr(c,global,"pspSnapshot"),input=JS_GetPropertyStr(c,global,"PSPInput"),runtime=JS_GetPropertyStr(c,global,"FlashRuntime");
        if(replay){JSValue n=JS_GetPropertyStr(c,global,"sceneFrameLimit");JS_ToUint32(c,&replay_limit,n);JS_FreeValue(c,n);if(!replay_limit||replay_limit>1000000){fail("Invalid replay frame count.");return 1;}}
        if(input_profile&&!replay)replay_limit=input_profile_limit;
#if DE_WLAN
        if(qa&&present("observation.js")){JS_FreeValue(c,snapshot);snapshot=JS_GetPropertyStr(c,global,"pspLinkObservation");}
        unsigned link_limit=0;if(qa){if(FILE* f=std::fopen("WLAN_FRAMES.QA","r")){std::fscanf(f,"%u",&link_limit);std::fclose(f);}}
        if(link_limit>65536){fail("Invalid bounded WLAN fixture length.");return 1;}
        if(link_limit)replay_limit=link_limit;
        if(!wire.checksum(*vm)){if(!vm->error().empty())fail(vm->error());else lose_link();return 1;}
        const bool bounded=replay||input_profile||link_limit;
#else
        const bool bounded=replay||input_profile;
#endif
        if(!source_timing.init(profile_source,replay_limit)){fail("Cannot allocate bounded source diagnostic.");return 1;}
        bool help=false;g_double_edged_debug.stage=2;
        if(!replay){SceCtrlData held{};sceCtrlPeekBufferPositive(&held,1);input_gate.block(held.Buttons,held.Lx,held.Ly);if(!vm->eval("PSPInput.afterLoading();","startup-input-release")){fail(vm->error());return 1;}}
        SourceClock clock;SourceGcPolicy gc_policy;bool deferred_last=false;
#if DE_ADAPTIVE_GC
        vm->collect();
#endif
        gc_policy.reset(frames,SourceVM::heap_bytes());
        loading.ready(sceKernelGetSystemTimeWide());clock.reset(sceKernelGetSystemTimeWide());
        while(!exiting){
#if DE_REALTIME_FRONTEND
            while(!exiting){auto delay=clock.wait(sceKernelGetSystemTimeWide());if(!delay)break;sceKernelDelayThread(unsigned(std::min<uint64_t>(delay,1000)));}
            if(exiting)break;
            int available=mailbox.acquire();if(available<0){sceKernelDelayThread(100);continue;}
            unsigned slot=unsigned(available);
#else
            sceKernelWaitSema(permit,1,nullptr);if(exiting)break;
            unsigned slot=produced.load(std::memory_order_relaxed)%SceneSlots;
#endif
            auto& scene=scenes[slot];
            auto began=sceKernelGetSystemTimeWide();SceCtrlData pad{};
            scene.sample_us=began;scene.scheduled_us=DE_REALTIME_FRONTEND?clock.next():0;
            bool loading_visible=false;unsigned resume_buttons=0;
            if(!replay){
                // Observing the screen inactive acquires the presentation's
                // prior resume-mask publication. Poll the pad after consuming
                // that mask: an older neutral sample must not re-arm its keys.
                loading_visible=loading.screen_active.load(std::memory_order_acquire);
                resume_buttons=loading.take_resume_buttons();
            }
            sceCtrlPeekBufferPositive(&pad,1);
            if(!replay){
                if(resume_buttons)input_gate.block(resume_buttons,128,128);
                if(loading_visible)input_gate.block(pad.Buttons,pad.Lx,pad.Ly);
                pad.Buttons=input_gate.filter(pad.Buttons);unsigned ax=pad.Lx,ay=pad.Ly;input_gate.axes(ax,ay);pad.Lx=ax;pad.Ly=ay;
            }
            unsigned edge=pad.Buttons&~last_buttons;last_buttons=pad.Buttons;g_double_edged_debug.buttons=pad.Buttons;
            constexpr unsigned picture_buttons=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER;
            if(!replay&&(pad.Buttons&picture_buttons)==picture_buttons&&(edge&picture_buttons)){
                picture_mode=!picture_mode;++picture_epoch;
                pspboot::mark("picture-save",depicture::save(picture_mode)?0:-1);
            }
            scene.smooth_picture=picture_mode;scene.picture_epoch=picture_epoch;
            if(!DE_WLAN&&!replay&&(edge&PSP_CTRL_SELECT)){help=!help;de_audio_pause(help);if(help)vm->eval("PSPInput.release();","release");}
            scene.help=help;
            if(help){scene.source_tick=frames;scene.logic_us=scene.capture_us=0;
#if DE_REALTIME_FRONTEND
                mailbox.publish(slot);clock.reset(sceKernelGetSystemTimeWide());clock.advance();
#endif
                produced.fetch_add(1,std::memory_order_release);continue;}
#if DE_WLAN
            wire.session.pump(sceKernelGetSystemTimeWide());wire.sample(pad);wire.probe(g_double_edged_link.checksum);
            if(!wire.session.active()){
#if DE_REALTIME_FRONTEND
                mailbox.cancel(slot);
#else
                sceKernelSignalSema(permit,1);
#endif
                lose_link();break;
            }
            doubleedged::WlanInput first,second;
            if(!wire.session.take_frame(first,second)){
                // Network waiting advances no source frame. Continue sampling
                // edges, release the unfilled scene and resume at the original
                // 30Hz cadence when both inputs become available.
#if DE_REALTIME_FRONTEND
                mailbox.cancel(slot);clock.reset(sceKernelGetSystemTimeWide());
#else
                sceKernelSignalSema(permit,1);
#endif
                sceKernelDelayThread(500);continue;
            }
#endif
            de_audio_begin_frame(frames+1);
            bool valid=true;
            if(replay){JSValue value=JS_Call(c,step,JS_UNDEFINED,0,nullptr);valid=!JS_IsException(value);if(!valid)vm->capture_exception();else if(trace){if(const char* text=JS_ToCString(c,value)){std::fprintf(trace,"%s\n",text);JS_FreeCString(c,text);}else valid=false;}JS_FreeValue(c,value);}
            else {
#if DE_WLAN
                JSValue args[]={JS_NewUint32(c,first.keys),JS_NewUint32(c,first.analog_x),JS_NewUint32(c,first.analog_y),JS_NewUint32(c,second.keys)};
                valid=vm->call(step,4,args);
#else
                JSValue args[]={JS_NewUint32(c,pad.Buttons),JS_NewInt32(c,pad.Lx),JS_NewInt32(c,pad.Ly)};
                valid=vm->call(step,3,args);
#endif
                for(auto v:args)JS_FreeValue(c,v);
            }
            if(!valid){fail(vm->error());break;}
            if(loading.worker_id&&!replay){SceCtrlData tail{};sceCtrlPeekBufferPositive(&tail,1);input_gate.block(pad.Buttons,pad.Lx,pad.Ly);input_gate.block(tail.Buttons,tail.Lx,tail.Ly);last_buttons=0;if(!vm->eval("PSPInput.afterLoading();","loading-input-release")){fail(vm->error());break;}}
            scene.logic_us=unsigned(sceKernelGetSystemTimeWide()-began);
            JSValue x=JS_GetPropertyStr(c,input,"x"),y=JS_GetPropertyStr(c,input,"y"),pointer=JS_GetPropertyStr(c,input,"pointer");double px=0,py=0;JS_ToFloat64(c,&px,x);JS_ToFloat64(c,&py,y);scene.pointer_x=float(53+px*.68);scene.pointer_y=float(py*.68);scene.pointer=JS_ToBool(c,pointer);JS_FreeValue(c,x);JS_FreeValue(c,y);JS_FreeValue(c,pointer);
            const bool was_loading=loading.worker_id!=0;
            bool capture_scene=true;
#if DE_REALTIME_FRONTEND && DE_LATE_CAPTURE
            capture_scene=deferred_last||clock.lag(sceKernelGetSystemTimeWide())<50000;
            if(!frames||was_loading||(bounded&&frames+1==replay_limit))capture_scene=true;
#endif
            auto captured=sceKernelGetSystemTimeWide();if(capture_scene&&de_capture_source(c,scene.frame,picture_mode)){fail("Cannot extract an original drawing resource.");break;}
#if DE_PREPARE_TEXTURES
            if(capture_scene)prepare_psp_capture(scene.frame,gpu_lock,gpu_unlock);
#endif
            scene.capture_us=unsigned(sceKernelGetSystemTimeWide()-captured);
            ++frames;
#if DE_WLAN
            if(!wire.checksum(*vm)){if(!vm->error().empty())fail(vm->error());else lose_link();
#if DE_REALTIME_FRONTEND
                mailbox.cancel(slot);
#else
                sceKernelSignalSema(permit,1);
#endif
                break;
            }
            link_final_checksum.store(g_double_edged_link.checksum,std::memory_order_release);
#endif
            scene.gc_us=0;unsigned gc_before=0,gc_after=0,gc_reason=0;
#if DE_ADAPTIVE_GC
            gc_reason=was_loading?3:gc_policy.reason(frames,SourceVM::heap_bytes());
#else
            gc_reason=frames%30==DE_GC_PHASE?4:0;
#endif
            auto finalize_scene=[&](){
            scene.source_tick=frames;scene.heap_peak=SourceVM::peak_bytes();
            JSValue root=JS_GetPropertyStr(c,runtime,"root"),frame=JS_GetPropertyStr(c,root,"currentFrame"),rng=JS_GetPropertyStr(c,runtime,"rng"),errors=JS_GetPropertyStr(c,runtime,"scriptErrorCount");uint32_t n=0;
            JS_ToUint32(c,&n,frame);scene.root_frame=n;g_double_edged_debug.root_frame=n;JS_ToUint32(c,&n,rng);g_double_edged_debug.rng=n;JS_ToUint32(c,&n,errors);scene.errors=n;JS_FreeValue(c,root);JS_FreeValue(c,frame);JS_FreeValue(c,rng);JS_FreeValue(c,errors);
            };
            bool early_published=false;
#if DE_REALTIME_FRONTEND
            early_published=depublication::before_collection(gc_reason,capture_scene,was_loading,
                bounded&&frames==replay_limit,finalize_scene,[&](){
                    mailbox.publish(slot);produced.fetch_add(1,std::memory_order_release);++early_gc_publications;
                });
#endif
            unsigned step_gc_us=0;
            if(gc_reason){gc_before=SourceVM::heap_bytes();auto gc_begin=sceKernelGetSystemTimeWide();vm->collect();step_gc_us=unsigned(sceKernelGetSystemTimeWide()-gc_begin);gc_after=SourceVM::heap_bytes();gc_policy.reset(frames,gc_after);}
            if(trace&&!replay)observation(*vm,snapshot,trace);else if(!trace){JSValue events=JS_GetPropertyStr(c,runtime,"audioEvents");JS_SetPropertyStr(c,events,"length",JS_NewInt32(c,0));JS_FreeValue(c,events);}
            if(!early_published){finalize_scene();scene.gc_us=step_gc_us;}
            g_double_edged_debug.frames=frames;g_double_edged_debug.heap=SourceVM::heap_bytes();g_double_edged_debug.peak=scene.heap_peak;g_double_edged_debug.cpu_us=unsigned(sceKernelGetSystemTimeWide()-began);
            if(trace&&frames%30==0)std::fflush(trace);
            if(profile_source)source_timing.append({frames,scene.logic_us,scene.capture_us,step_gc_us,unsigned(sceKernelGetSystemTimeWide()-began),unsigned(SourceVM::heap_bytes()),scene.sample_us,scene.scheduled_us,gc_before,gc_after,gc_reason,unsigned(capture_scene)});
            if(frames==1)pspboot::mark("first-source-scene");
#if DE_REALTIME_FRONTEND
            if(!early_published){if(capture_scene)mailbox.publish(slot);else mailbox.cancel(slot);}
            deferred_last=!capture_scene;
            if(was_loading)clock.reset(sceKernelGetSystemTimeWide());clock.advance();
#endif
            if(!early_published)produced.fetch_add(1,std::memory_order_release);
            if(was_loading)loading.ready(sceKernelGetSystemTimeWide());
            if(bounded&&frames==replay_limit){
                if(trace)std::fflush(trace);
                // Read once after the last scene has been published. This QA
                // metadata does not add work to measured gameplay frames.
                JSValue batches=JS_GetPropertyStr(c,runtime,"depthOrderBatches"),sorts=JS_GetPropertyStr(c,runtime,"nativeDepthSorts");
                uint32_t batch_count=0,sort_count=0;JS_ToUint32(c,&batch_count,batches);JS_ToUint32(c,&sort_count,sorts);JS_FreeValue(c,batches);JS_FreeValue(c,sorts);
                {JSValue fn=JS_GetPropertyStr(c,global,"nativeWorldBuildStats"),value=JS_Call(c,fn,JS_UNDEFINED,0,nullptr),json=JS_JSONStringify(c,value,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,json)){if(FILE* f=std::fopen("CHAIN117.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,json);JS_FreeValue(c,value);JS_FreeValue(c,fn);}
                if(present("MATRIX117.QA")&&!de_matrix_qa117("MATRIX117.JSON")){fail("Matrix numeric parity failed.");return 1;}
                {JSValue fn=JS_GetPropertyStr(c,global,"nativeDisplaySuffixStats"),value=JS_Call(c,fn,JS_UNDEFINED,0,nullptr),json=JS_JSONStringify(c,value,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,json)){if(FILE* f=std::fopen("SUFFIX111.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,json);JS_FreeValue(c,value);JS_FreeValue(c,fn);}
                if(FILE* f=std::fopen("ARRAY123.JSON","w")){std::fprintf(f,"{\"array_named_shortcuts\":%u}\n",JS_GetArrayNamedShortcuts106());std::fclose(f);}
                {JSValue g130=JS_GetGlobalObject(c),fn130=JS_GetPropertyStr(c,g130,"nativeGotoNoopStats"),v130=JS_Call(c,fn130,JS_UNDEFINED,0,nullptr),json130=JS_JSONStringify(c,v130,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,json130)){if(FILE* f=std::fopen("GOTO130.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,json130);JS_FreeValue(c,v130);JS_FreeValue(c,fn130);JS_FreeValue(c,g130);}
                if(FILE* f=std::fopen("SOURCE_BACKEND.JSON","w")){std::fprintf(f,"{\"scope\":\"Backend counters collected after the final scene was published\",\"frames\":%u,\"depth_order_batches\":%u,\"native_depth_sorts\":%u,\"native_slice_bulk_copies\":%u,\"native_slice_const_copies\":%u,\"audio_status_atomic_reads\":%u,\"scalar_bytecodes_parked_at_init\":%u,\"intrinsic_instanceof_fast_calls\":%llu,\"intrinsic_instanceof_general_calls\":%llu}\n",frames,batch_count,sort_count,JS_GetSliceBulkCopyCount(JS_GetRuntime(c)),JS_GetSliceBulkConstCopyCount(JS_GetRuntime(c)),de_audio_query_count(),vm->parked_bytecodes(),(unsigned long long)JS_GetIntrinsicInstanceofFast101(JS_GetRuntime(c)),(unsigned long long)JS_GetIntrinsicInstanceofGeneral101(JS_GetRuntime(c)));std::fclose(f);}
                if(FILE* f=std::fopen("GC_PUBLICATION_STATS.JSON","w")){std::fprintf(f,"{\"completed_scene_publications_before_collection\":%u}\n",early_gc_publications);std::fclose(f);}
                JSValue world_stats=JS_GetPropertyStr(c,global,"nativeWorldBoundsStats"),world_record=JS_Call(c,world_stats,JS_UNDEFINED,0,nullptr),world_json=JS_JSONStringify(c,world_record,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,world_json)){if(FILE* f=std::fopen("WORLD_CACHE_STATS.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,world_json);JS_FreeValue(c,world_record);JS_FreeValue(c,world_stats);
                JSValue cpu_stats=JS_GetPropertyStr(c,global,"nativeCpuCollisionStats"),cpu_record=JS_Call(c,cpu_stats,JS_UNDEFINED,0,nullptr),cpu_json=JS_JSONStringify(c,cpu_record,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,cpu_json)){if(FILE* f=std::fopen("CPU_COLLISION_STATS.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,cpu_json);JS_FreeValue(c,cpu_record);JS_FreeValue(c,cpu_stats);
                JSValue build_stats=JS_GetPropertyStr(c,global,"nativeWorldBuildStats"),build_record=JS_Call(c,build_stats,JS_UNDEFINED,0,nullptr),build_json=JS_JSONStringify(c,build_record,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,build_json)){if(FILE* f=std::fopen("WORLD_BUILD_STATS.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,build_json);JS_FreeValue(c,build_record);JS_FreeValue(c,build_stats);
                if(FILE* f=std::fopen("PROTOTYPE_MISS_STATS.JSON","w")){std::fprintf(f,"{\"hits\":%u,\"scans\":%u}\n",JS_GetPrototypeMissCacheHits(JS_GetRuntime(c)),JS_GetPrototypeMissCacheScans(JS_GetRuntime(c)));std::fclose(f);}
                JSValue rock_stats=JS_GetPropertyStr(c,global,"nativeRockCollisionStats"),rock_record=JS_Call(c,rock_stats,JS_UNDEFINED,0,nullptr),rock_json=JS_JSONStringify(c,rock_record,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,rock_json)){if(FILE* f=std::fopen("ROCK_COLLISION_STATS.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,rock_json);JS_FreeValue(c,rock_record);JS_FreeValue(c,rock_stats);
                JSValue display_stats=JS_GetPropertyStr(c,global,"nativeDisplayPrefixStats"),display_record=JS_Call(c,display_stats,JS_UNDEFINED,0,nullptr),display_json=JS_JSONStringify(c,display_record,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,display_json)){if(FILE* f=std::fopen("DISPLAY_PREFIX_STATS.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,display_json);JS_FreeValue(c,display_record);JS_FreeValue(c,display_stats);
                JSValue invalid_stats=JS_GetPropertyStr(c,global,"nativeWorldInvalidationStats"),invalid_record=JS_Call(c,invalid_stats,JS_UNDEFINED,0,nullptr),invalid_json=JS_JSONStringify(c,invalid_record,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,invalid_json)){if(FILE* f=std::fopen("WORLD_INVALIDATION_STATS.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,invalid_json);JS_FreeValue(c,invalid_record);JS_FreeValue(c,invalid_stats);
                JSValue rounding_stats=JS_GetPropertyStr(c,global,"nativeSourceRoundingStats"),rounding_record=JS_Call(c,rounding_stats,JS_UNDEFINED,0,nullptr),rounding_json=JS_JSONStringify(c,rounding_record,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,rounding_json)){if(FILE* f=std::fopen("SOURCE_ROUNDING_STATS.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,rounding_json);JS_FreeValue(c,rounding_record);JS_FreeValue(c,rounding_stats);
                JSValue timeline_stats=JS_GetPropertyStr(c,global,"nativeTimelineWalkerStats"),timeline_record=JS_Call(c,timeline_stats,JS_UNDEFINED,0,nullptr),timeline_json=JS_JSONStringify(c,timeline_record,JS_UNDEFINED,JS_UNDEFINED);if(const char* text=JS_ToCString(c,timeline_json)){if(FILE* f=std::fopen("TIMELINE_WALKER_STATS.JSON","w")){std::fprintf(f,"%s\n",text);std::fclose(f);}JS_FreeCString(c,text);}JS_FreeValue(c,timeline_json);JS_FreeValue(c,timeline_record);JS_FreeValue(c,timeline_stats);
#if DE_WLAN
                // Keep pumping until the peer acknowledges this exact final
                // state. The renderer must not report a one-sided success.
                while(!exiting&&wire.session.active()&&!wire.session.confirmed(frames)){
                    wire.session.pump(sceKernelGetSystemTimeWide());wire.session.announce_checksum();wire.probe(g_double_edged_link.checksum);sceKernelDelayThread(500);
                }
                if(!exiting&&!wire.session.active()){lose_link();break;}
                source_done.store(true,std::memory_order_release);
                // The two QA peers finish asynchronously. Keep the socket
                // alive and send final checkpoints until the run is closed.
                while(!exiting){wire.session.pump(sceKernelGetSystemTimeWide());wire.session.announce_checksum();sceKernelDelayThread(1000);}
                break;
#else
                source_done.store(true,std::memory_order_release);break;
#endif
            }
        }
        JS_FreeValue(c,runtime);JS_FreeValue(c,input);JS_FreeValue(c,snapshot);JS_FreeValue(c,step);JS_FreeValue(c,global);
        return failed.load()?1:0;
    };
    int worker=sceKernelCreateThread("Double Edged 30Hz source",source_worker,0x28,768*1024,PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU,nullptr);
    pspboot::mark("source-thread-create",worker);
    if(worker<0){failure("Cannot create original-script worker.");sceKernelExitGame();return 1;}
    int worker_started=sceKernelStartThread(worker,0,nullptr);pspboot::mark("source-thread-start",worker_started);
    if(worker_started<0){sceKernelDeleteThread(worker);sceKernelExitGame();return 1;}
    sceKernelChangeThreadPriority(0,0x18);
    auto show_loading=[&](){
        unsigned id=loading.requested.load(std::memory_order_acquire);
        gpu_lock();set_psp_pointer(0,0,false);
        draw_psp_loading();
        bool visible=confirm_psp_frame_visible();auto shown_at=sceKernelGetSystemTimeWide();unsigned shown_vcount=sceDisplayGetVcount();
        if(visible&&loading.capture){char name[48];std::snprintf(name,sizeof(name),"LOADING_%03u.RGBA",id);de_render_capture(name);}gpu_unlock();
        loading.shown(id,shown_at,shown_vcount,visible);return visible?id:0;
    };
    unsigned shown_load=0,resume_load=0;bool resume_submitted=false;
    auto sample_resume_buttons=[](void* context){SceCtrlData held{};sceCtrlPeekBufferPositive(&held,1);static_cast<StaticLoading*>(context)->record_resume_buttons(held.Buttons);};
    while(!exiting&&!failed.load(std::memory_order_acquire)
#if DE_WLAN
        &&!link_error_status.load(std::memory_order_acquire)
#endif
        &&produced.load(std::memory_order_acquire)<(DE_REALTIME_FRONTEND?1:SceneSlots-1)&&!source_done.load()){
        if(loading.pending()){if(produced.load(std::memory_order_acquire))break;shown_load=show_loading();}
        sceKernelDelayThread(100);
    }
    PresentCapture timing;if(!timing.init(measure||qa||input_profile,replay_limit,replay||input_profile||(DE_WLAN&&qa&&replay_limit))){failure("Cannot allocate bounded frame log.");return 1;}
    dephase119::Stats phase119;
    unsigned presentations=0,subframe=0,rendered_picture_epoch=0;bool fresh=false,reported=false,boot_presented=false;CapturePair pair_capture;
    VisibleScene pending_visible{},visible{};bool visibility_pending=false;
    while(!exiting&&!failed.load(std::memory_order_acquire)){
#if DE_WLAN
        if(unsigned status=link_error_status.load(std::memory_order_acquire)){
            char message[256];std::snprintf(message,sizeof(message),"DOUBLE EDGED - TWO PSP\n\n%s\n\nCIRCLE / HOME: EXIT",doubleedged::WlanFrontend::status_text(doubleedged::WlanStatus(status)));
            gpu_lock();draw_psp_message(message);gpu_unlock();SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);if(pad.Buttons&PSP_CTRL_CIRCLE)exiting=true;sceKernelDelayThreadCB(1000);continue;
        }
#endif
        if(shown_load){
#if DE_REALTIME_FRONTEND
            if(loading.completed.load(std::memory_order_acquire)<shown_load||!mailbox.pending()){sceKernelDelayThread(100);continue;}
#else
            unsigned ready=produced.load(std::memory_order_acquire)-consumed.load(std::memory_order_acquire);
            // Restore the existing bounded pipeline before exposing the first
            // new scene. A following menu request or replay EOF must be allowed
            // through even when fewer scenes are available, avoiding deadlock.
            if(loading.completed.load(std::memory_order_acquire)<shown_load||!ready||
               (ready<SceneSlots-1&&!loading.pending()&&!source_done.load(std::memory_order_acquire))){sceKernelDelayThread(100);continue;}
#endif
            resume_load=shown_load;shown_load=0;resume_submitted=false;
        }
#if DE_REALTIME_FRONTEND
        // A due interpolation endpoint is always submitted on its old path.
        // Only a would-be hold may briefly yield for a soon completed scene.
        int phase_slot119=active.load(std::memory_order_acquire);
        bool phase_endpoint119=fresh&&!subframe;
        bool phase_loading119=resume_load||loading.pending()||loading.screen_active.load(std::memory_order_acquire);
        if(phase_slot119>=0&&!phase_endpoint119&&!phase_loading119&&!scenes[phase_slot119].help&&!mailbox.pending()&&!source_done.load(std::memory_order_acquire)){
            unsigned delay=dephase119::wait_us(sceKernelGetSystemTimeWide(),psp_present_time(),true,false,false,false,false,false);
            if(delay){sceKernelDelayThread(delay);phase119.record(delay,mailbox.pending(),phase_endpoint119,phase_loading119);}
        }
        if(loading.pending()&&!mailbox.pending()){shown_load=show_loading();pending_visible={};visible={};visibility_pending=false;continue;}
        int latest=mailbox.take();
        bool endpoint=false;
        if(latest>=0){int old=active.exchange(latest,std::memory_order_acq_rel);if(old>=0)mailbox.release(unsigned(old));subframe=0;fresh=true;}
        else{endpoint=fresh&&!subframe;subframe=1;fresh=endpoint;}
        const bool submit=latest>=0||endpoint;
#else
        if(!subframe&&loading.pending()&&produced.load(std::memory_order_acquire)==consumed.load(std::memory_order_acquire)){shown_load=show_loading();continue;}
        if(!subframe){unsigned read=consumed.load(std::memory_order_relaxed);fresh=produced.load(std::memory_order_acquire)>read;
            if(fresh){int old=active.exchange(read%SceneSlots,std::memory_order_acq_rel);consumed.store(read+1,std::memory_order_release);if(old>=0)sceKernelSignalSema(permit,1);}}
        const bool submit=true;
#endif
        int slot=active.load(std::memory_order_acquire);if(slot<0){sceKernelDelayThread(100);continue;}const auto& scene=scenes[slot];
        gpu_lock();
        if(fast_color_filters()!=scene.smooth_picture)set_fast_color_filters(scene.smooth_picture);
        if(rendered_picture_epoch!=scene.picture_epoch){rendered_picture_epoch=scene.picture_epoch;notify_psp_picture_mode();}
        set_psp_pointer(scene.pointer_x,scene.pointer_y,scene.pointer);
        if(!submit){gpu_unlock();hold_psp_frame();gpu_lock();}
        else if(scene.help){char message[400];std::snprintf(message,sizeof(message),"DOUBLE EDGED\n\nD-PAD: MOVE / CHOOSE\nSQUARE: ATTACK / PICK UP / THROW\nCROSS: JUMP / CONFIRM\nSQUARE + CROSS: SPECIAL\nSTART: PAUSE / CONTINUE\nCIRCLE: BACK\nL + R: PICTURE - %s\nSELECT: CLOSE HELP",scene.smooth_picture?"SMOOTH":"ORIGINAL");draw_psp_message(message,resume_load&&!resume_submitted?+sample_resume_buttons:nullptr,&loading);}
        else {de_play_capture(scene.frame,subframe||!fresh?1.f:.5f);gpu_unlock();present_psp_frame(resume_load&&!resume_submitted?+sample_resume_buttons:nullptr,&loading);gpu_lock();}
        if(!boot_presented){pspboot::mark("first-scene-presented");boot_presented=true;}
        if(submit)pair_capture.capture(scene.source_tick,subframe);gpu_unlock();
        bool visible_changed=false;
        if(visibility_pending&&psp_latch_confirmed()){visible=pending_visible;visible_changed=visible.fresh;visibility_pending=false;}
        if(submit){pending_visible={scene.source_tick,fresh,scene.sample_us};visibility_pending=true;}
        if(resume_load){
            if(!resume_submitted){loading.submitted(resume_load,psp_present_time());resume_submitted=true;}
            if(confirm_psp_frame_visible(false)){loading.resumed(resume_load,psp_present_time(),psp_present_vcount());resume_load=0;}
        }
        ++presentations;g_double_edged_60.source_tick=scene.source_tick;g_double_edged_60.presentations=presentations;g_double_edged_60.midpoint=!subframe&&fresh;g_double_edged_60.fresh=fresh;g_double_edged_60.vcount=psp_present_vcount();
        timing.append(scene,subframe,fresh,submit,visible,visible_changed);
        if((replay||input_profile||(DE_WLAN&&qa&&replay_limit))&&source_done.load(std::memory_order_acquire)&&scene.source_tick==replay_limit&&subframe&&!reported){
            bool timing_ok=phase119.write("PHASE119.JSON")&&timing.write()&&!timing.overflow&&source_timing.write();de_audio_flush();de_flush_render_audit();gpu_lock();flush_psp_media_stats();int captured=de_render_capture("FINAL.RGBA");gpu_unlock();if(!timing_ok)captured=1;
#if DE_WLAN
            if(FILE* f=std::fopen("WLAN_RESULT.JSON","w")){
                std::fprintf(f,"{\"scope\":\"Threaded two-instance lockstep; no physical PSP proof\",\"frames\":%u,\"confirmed\":%u,\"role\":%u,\"error\":%d,\"checksum\":%u}\n",scene.source_tick,replay_limit,link_role.load(std::memory_order_acquire),captured?1:0,link_final_checksum.load(std::memory_order_acquire));std::fclose(f);
            }
#endif
            if(FILE* f=std::fopen("FRONTEND_RESULT.JSON","w")){std::fprintf(f,"{\"scope\":\"Input-only source replay with immutable native command snapshots; physical PSP performance remains untested\",\"frames\":%u,\"presentations\":%u,\"error\":%d,\"script_error_count\":%u,\"heap_peak\":%u,\"timing_samples\":%u,\"timing_capacity\":%u,\"timing_overflow\":%s}\n",scene.source_tick,presentations,captured?1:0,scene.errors,scene.heap_peak,timing.used,timing.capacity,timing.overflow?"true":"false");std::fclose(f);}reported=true;g_double_edged_debug.stage=4;
            while(!exiting)sceKernelDelayThreadCB(100000);break;
        }
#if !DE_REALTIME_FRONTEND
        subframe^=1;
#endif
    }
    if(!reported)timing.write();exiting=true;sceKernelSignalSema(permit,1);sceKernelWaitThreadEnd(worker,nullptr);sceKernelDeleteThread(worker);worker_function={};loading_request={};
    if(trace)std::fclose(trace);if(failed.load()){failure(worker_error.c_str());}
    if(vm&&vm->context())JS_UpdateStackTop(JS_GetRuntime(vm->context()));
    de_render_shutdown();de_audio_shutdown();vm.reset();sceKernelExitGame();return 0;
}
