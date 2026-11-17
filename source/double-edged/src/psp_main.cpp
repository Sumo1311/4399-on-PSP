#include "source_vm.hpp"
#include "storage.hpp"
#include "audio_psp.hpp"
#include "render_psp.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspdebug.h>
#if DE_WLAN
#include "wlan_frontend.hpp"
#endif
PSP_MODULE_INFO("Double Edged Development",PSP_MODULE_USER,0,1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(768);
PSP_HEAP_SIZE_KB(-1024);
extern "C" int de_render_init();
extern "C" int de_render_frame(JSContext* context);
extern "C" int de_render_capture(const char* path);
extern "C" void de_render_shutdown();
struct DebugState {uint32_t stage,frames,buttons,root_frame,rng,heap,peak,cpu_us;};
extern "C" {
__attribute__((used,externally_visible)) volatile DebugState g_double_edged_debug{};
}
namespace {
volatile bool exiting=false;
int exit_callback(int,int,void*){exiting=true;return 0;}
int callback_thread(SceSize,void*){int id=sceKernelCreateCallback("Double Edged exit",exit_callback,nullptr);sceKernelRegisterExitCallback(id);sceKernelSleepThreadCB();return 0;}
void callbacks(){int id=sceKernelCreateThread("Double Edged callbacks",callback_thread,0x11,4096,PSP_THREAD_ATTR_USER,nullptr);if(id>=0)sceKernelStartThread(id,0,nullptr);}
bool present(const char* file){FILE* f=std::fopen(file,"rb");if(!f)return false;std::fclose(f);return true;}
void failure(const char* message){FILE* f=std::fopen("ERROR.LOG","w");if(f){std::fputs(message,f);std::fclose(f);}pspDebugScreenInit();pspDebugScreenPrintf("Double Edged could not continue.\n\n%s\n\nSee ERROR.LOG. Press HOME to exit.\n",message);while(!exiting)sceKernelDelayThreadCB(100000);}
void debug_snapshot(SourceVM& vm,JSValueConst snapshot,FILE* trace){
    auto c=vm.context();JSValue value=JS_Call(c,snapshot,JS_UNDEFINED,0,nullptr);
    if(JS_IsException(value)){vm.capture_exception();JS_FreeValue(c,value);return;}
    if(trace){const char* text=JS_ToCString(c,value);if(text){std::fprintf(trace,"%s\n",text);JS_FreeCString(c,text);}}
    JS_FreeValue(c,value);
}
bool load_picture_mode(){
    char value[20]{};FILE* file=std::fopen("SAVE/PICTURE.CFG","rb");if(!file)return true;
    std::fgets(value,sizeof(value),file);std::fclose(file);
    return std::strcmp(value,"ORIGINAL\n")!=0;
}
void save_picture_mode(){
    mkdir("SAVE",0777);if(FILE* file=std::fopen("SAVE/PICTURE.CFG","wb")){
        std::fputs(fast_color_filters()?"SMOOTH\n":"ORIGINAL\n",file);std::fclose(file);
    }
}
}
int main(){
    callbacks();scePowerSetClockFrequency(333,333,166);sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    pspDebugScreenInit();pspDebugScreenPrintf("Double Edged\n\nLoading original game resources...\n");
    bool qa=present("TRACE.QA"),replay=qa&&present("REPLAY.QA");FILE* trace=qa?std::fopen("FRONTEND.JSONL","w"):nullptr;
    set_fast_color_filters(load_picture_mode());
    if(qa&&present("FAST_FILTERS.QA"))set_fast_color_filters(true);
    if(qa&&present("ORIGINAL_FILTERS.QA"))set_fast_color_filters(false);
    uint32_t seed=uint32_t(sceKernelGetSystemTimeWide());if(FILE* f=std::fopen("SEED.QA","r")){unsigned long value=0;if(std::fscanf(f,"%lu",&value)==1)seed=uint32_t(value);std::fclose(f);}
    SourceVM vm;auto c=vm.context();g_double_edged_debug.stage=1;
    if(!c){failure(vm.error().c_str());sceKernelExitGame();return 1;}
    de_storage_bind(c);
#if DE_WLAN
    if(replay){failure("Use the solo package for an isolated source replay.");return 1;}
    if(de_render_init()){failure("Cannot open original drawing resources.");return 1;}
    doubleedged::WlanFrontend wire;
    if(!wire.connect(vm,qa,exiting)){if(!vm.error().empty())failure(vm.error().c_str());de_render_shutdown();sceKernelExitGame();return 0;}
    seed=wire.session.setup().seed;
    if(!wire.apply_profile(vm)||!vm.eval_file("link_clock.js")){failure(vm.error().c_str());return 1;}
#endif
    if(de_audio_bind(c)){failure("Cannot open original audio bank.");sceKernelExitGame();return 1;}
    if((replay&&!vm.eval_file("qa-date.js"))||!vm.initialize(seed,!qa&&!DE_WLAN)||!vm.eval_file("pad.js")||(replay&&!vm.eval_file("fixture.js"))){failure(vm.error().c_str());de_audio_shutdown();sceKernelExitGame();return 1;}
#if DE_WLAN
    if(!vm.eval_file("link_input.js")||(qa&&present("observation.js")&&!vm.eval_file("observation.js"))){failure(vm.error().c_str());return 1;}
#else
    if(de_render_init()){failure("Cannot open original drawing resources.");de_audio_shutdown();sceKernelExitGame();return 1;}
#endif
    JSValue global=JS_GetGlobalObject(c),step=JS_GetPropertyStr(c,global,DE_WLAN?"pspLinkStep":replay?"sceneStep":"pspStep"),snapshot=JS_GetPropertyStr(c,global,"pspSnapshot"),input=JS_GetPropertyStr(c,global,"PSPInput"),runtime=JS_GetPropertyStr(c,global,"FlashRuntime");
#if DE_WLAN
    if(qa&&present("observation.js")){JS_FreeValue(c,snapshot);snapshot=JS_GetPropertyStr(c,global,"pspLinkObservation");}
    if(!wire.checksum(vm)){failure(vm.error().c_str());return 1;}
    unsigned link_limit=0;bool link_reported=false;if(qa){if(FILE* f=std::fopen("WLAN_FRAMES.QA","r")){std::fscanf(f,"%u",&link_limit);std::fclose(f);}}
#endif
    uint32_t replay_limit=0;if(replay){JSValue limit=JS_GetPropertyStr(c,global,"sceneFrameLimit");JS_ToUint32(c,&replay_limit,limit);JS_FreeValue(c,limit);if(!replay_limit||replay_limit>1000000){failure("Invalid isolated replay length.");return 1;}}
    bool help=false;unsigned last_buttons=0,frames=0;g_double_edged_debug.stage=2;
    while(!exiting){
        uint64_t started=sceKernelGetSystemTimeWide();SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);unsigned edge=pad.Buttons&~last_buttons;last_buttons=pad.Buttons;g_double_edged_debug.buttons=pad.Buttons;
        constexpr unsigned picture_buttons=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER;
        if(!replay&&(pad.Buttons&picture_buttons)==picture_buttons&&(edge&picture_buttons)){
            set_fast_color_filters(!fast_color_filters());save_picture_mode();notify_psp_picture_mode();
        }
        if(!DE_WLAN&&!replay&&(edge&PSP_CTRL_SELECT)){help=!help;de_audio_pause(help);if(help)vm.eval("PSPInput.release();","release");}
        if(help){
            char message[400];std::snprintf(message,sizeof(message),"DOUBLE EDGED\n\nD-PAD: MOVE / CHOOSE\nSQUARE: ATTACK / PICK UP / THROW\nCROSS: JUMP / CONFIRM\nSQUARE + CROSS: SPECIAL\nSTART: PAUSE / CONTINUE\nANALOG: POINTER   CIRCLE: BACK\nL + R: PICTURE - %s\nSELECT: CLOSE HELP",fast_color_filters()?"SMOOTH":"ORIGINAL");draw_psp_message(message);
            sceKernelDelayThread(33333);continue;
        }
        unsigned buttons=pad.Buttons;
#if DE_WLAN
        wire.session.pump(started);wire.sample(pad);wire.probe(g_double_edged_link.checksum);
        if(!wire.session.active()){
            de_audio_pause(true);FILE* f=std::fopen("LINK_ERROR.JSON","w");if(f){std::fprintf(f,"{\"status\":%u,\"error\":%d,\"source_frames\":%u}\n",unsigned(wire.session.status()),wire.session.error(),frames);std::fclose(f);}
            while(!exiting){char text[240];std::snprintf(text,sizeof(text),"DOUBLE EDGED - TWO PSP\n\n%s\n\nPRESS HOME TO EXIT",doubleedged::WlanFrontend::status_text(wire.session.status()));draw_psp_message(text);sceKernelDelayThreadCB(33333);}break;
        }
        if(link_limit&&frames>=link_limit){
            if(wire.session.confirmed(link_limit)&&!link_reported){if(trace)std::fflush(trace);de_audio_flush();int captured=de_render_capture("FINAL.RGBA");FILE* f=std::fopen("WLAN_RESULT.JSON","w");if(f){std::fprintf(f,"{\"scope\":\"Private two-instance lockstep test; no physical PSP proof\",\"frames\":%u,\"confirmed\":%u,\"role\":%u,\"error\":%d,\"checksum\":%u}\n",frames,link_limit,wire.session.host()?1:2,captured?1:0,g_double_edged_link.checksum);std::fclose(f);}link_reported=true;}
            wire.session.announce_checksum();sceKernelDelayThreadCB(1000);continue;
        }
        doubleedged::WlanInput first,second;if(!wire.session.take_frame(first,second)){sceKernelDelayThreadCB(500);continue;}
        JSValue args[]={JS_NewUint32(c,first.keys),JS_NewUint32(c,first.analog_x),JS_NewUint32(c,first.analog_y),JS_NewUint32(c,second.keys)};
#else
        JSValue args[]={JS_NewUint32(c,buttons),JS_NewInt32(c,pad.Lx),JS_NewInt32(c,pad.Ly)};
#endif
        de_audio_begin_frame(frames+1);
        bool valid;
        if(replay){JSValue result=JS_Call(c,step,JS_UNDEFINED,0,nullptr);valid=!JS_IsException(result);if(!valid)vm.capture_exception();else if(trace){const char* text=JS_ToCString(c,result);if(text){std::fprintf(trace,"%s\n",text);JS_FreeCString(c,text);}else valid=false;}JS_FreeValue(c,result);}
        else valid=vm.call(step,DE_WLAN?4:3,args);
        for(auto value:args)JS_FreeValue(c,value);if(!valid){failure(vm.error().c_str());break;}
        JSValue x=JS_GetPropertyStr(c,input,"x"),y=JS_GetPropertyStr(c,input,"y"),pointer=JS_GetPropertyStr(c,input,"pointer");double px=0,py=0;JS_ToFloat64(c,&px,x);JS_ToFloat64(c,&py,y);set_psp_pointer(float(53+px*.68),float(py*.68),JS_ToBool(c,pointer));JS_FreeValue(c,x);JS_FreeValue(c,y);JS_FreeValue(c,pointer);
        if(de_render_frame(c)){failure("A drawing resource could not be rendered.");break;}
        ++frames;if(frames%30==0)vm.collect();
#if DE_WLAN
        if(!wire.checksum(vm)){if(!vm.error().empty()){failure(vm.error().c_str());break;}}
#endif
        if(trace&&!replay)debug_snapshot(vm,snapshot,trace);else if(!trace){JSValue events=JS_GetPropertyStr(c,runtime,"audioEvents");JS_SetPropertyStr(c,events,"length",JS_NewInt32(c,0));JS_FreeValue(c,events);}
        g_double_edged_debug.frames=frames;g_double_edged_debug.heap=SourceVM::heap_bytes();g_double_edged_debug.peak=SourceVM::peak_bytes();g_double_edged_debug.cpu_us=unsigned(sceKernelGetSystemTimeWide()-started);
        if(frames%30==0){JSValue root=JS_GetPropertyStr(c,runtime,"root"),frame=JS_GetPropertyStr(c,root,"currentFrame"),rng=JS_GetPropertyStr(c,runtime,"rng");uint32_t n=0;JS_ToUint32(c,&n,frame);g_double_edged_debug.root_frame=n;JS_ToUint32(c,&n,rng);g_double_edged_debug.rng=n;JS_FreeValue(c,frame);JS_FreeValue(c,root);JS_FreeValue(c,rng);if(trace)std::fflush(trace);}
        if(replay&&frames==replay_limit){
            if(trace)std::fflush(trace);uint32_t errors=0;JSValue value=JS_GetPropertyStr(c,runtime,"scriptErrorCount");JS_ToUint32(c,&errors,value);JS_FreeValue(c,value);
            de_audio_flush();int captured=de_render_capture("FINAL.RGBA");FILE* result=std::fopen("FRONTEND_RESULT.JSON","w");if(result){std::fprintf(result,"{\"scope\":\"Private ordinary-input replay with native storage; separate from actual pad qualification\",\"frames\":%u,\"error\":%d,\"script_error_count\":%u,\"heap_peak\":%u}\n",frames,captured?1:0,unsigned(errors),unsigned(SourceVM::peak_bytes()));std::fclose(result);}
            g_double_edged_debug.stage=4;while(!exiting)sceKernelDelayThreadCB(100000);break;
        }
        uint64_t elapsed=sceKernelGetSystemTimeWide()-started;if(elapsed<33333)sceKernelDelayThread(unsigned(33333-elapsed));
    }
    g_double_edged_debug.stage=3;if(trace)std::fclose(trace);
    JS_FreeValue(c,runtime);JS_FreeValue(c,input);JS_FreeValue(c,snapshot);JS_FreeValue(c,step);JS_FreeValue(c,global);de_render_shutdown();de_audio_shutdown();sceKernelExitGame();return 0;
}
