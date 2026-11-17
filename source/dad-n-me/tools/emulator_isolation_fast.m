// Explicit diagnostic-only speed control for the isolated SDL emulator.
// Normal runs compile emulator_isolation.m and never include this code.
#include "emulator_isolation.m"
#include <SDL2/SDL_events.h>

static void* diagnostic_speed_worker(void* unused){
    unsigned until=(unsigned)strtoul(getenv("PSP_DIAG_FAST_UNTIL"),NULL,0);
    uintptr_t symbol=strtoull(getenv("PSP_WALL_BASE_SYMBOL_VMADDR"),NULL,0);
    uint64_t address=strtoull(getenv("PSP_WALL_PROBE_ADDRESS"),NULL,0);
    uint8_t** memory_base=(uint8_t**)dlsym(RTLD_DEFAULT,"_ZN6Memory4baseE");
    if(!memory_base&&symbol)memory_base=(uint8_t**)(symbol+_dyld_get_image_vmaddr_slide(0));
    int(*push)(SDL_Event*)=(int(*)(SDL_Event*))dlsym(RTLD_DEFAULT,"SDL_PushEvent");
    const char* stop=getenv("PSP_WALL_STOP");
    FILE* log=fopen(getenv("PSP_DIAG_FAST_LOG"),"w");
    if(!log)return NULL;
    if(!push||!memory_base){fprintf(log,"{\"error\":\"SDL/event or memory symbol missing\"}\n");fclose(log);return NULL;}
    bool down=false;
    while(access(stop,F_OK)!=0){
        uint32_t tick=0;mach_vm_size_t length=0;uint8_t* base=*memory_base;
        bool ready=base&&mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)(base+address),4,(mach_vm_address_t)&tick,&length)==KERN_SUCCESS&&length==4;
        if(ready&&tick>=8&&((!down&&tick<until)||(down&&tick>=until))){
            SDL_Event event;memset(&event,0,sizeof(event));
            event.type=down?SDL_KEYUP:SDL_KEYDOWN;event.key.state=down?SDL_RELEASED:SDL_PRESSED;
            event.key.keysym.scancode=SDL_SCANCODE_TAB;event.key.keysym.sym=SDLK_TAB;
            int result=push(&event);
            fprintf(log,"{\"tick\":%u,\"wall_ns\":%llu,\"action\":\"%s\",\"SDL_PushEvent\":%d}\n",tick,wall_ns(),down?"release-fast-forward":"hold-fast-forward",result);fflush(log);
            if(result!=1)break;
            if(down)break;down=true;
        }
        usleep(2000);
    }
    fclose(log);return NULL;
}
__attribute__((constructor))static void diagnostic_speed_start(void){
    if(!getenv("PSP_DIAG_FAST_UNTIL")||!getenv("PSP_WALL_OUTPUT"))return;
    pthread_t thread;if(pthread_create(&thread,NULL,diagnostic_speed_worker,NULL)==0)pthread_detach(thread);
}
