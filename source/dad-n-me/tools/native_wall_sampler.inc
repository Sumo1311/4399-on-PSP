// Task-local PPSSPP instrumentation. Read-only Mach self-copy: no debugger
// requests, emulation pauses, process attachment, or changes to PSP RAM.
#include <pthread.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "presentation_ring_reader.inc"
#include "response_ring_reader.inc"

static uint64_t wall_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC_RAW,&t);return(uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void* wall_worker(void* unused){
    const char* output=getenv("PSP_WALL_OUTPUT");
    const char* stop=getenv("PSP_WALL_STOP");
    const char* summary=getenv("PSP_WALL_SUMMARY");
    uint64_t address=strtoull(getenv("PSP_WALL_PROBE_ADDRESS"),NULL,0);
    unsigned offset=atoi(getenv("PSP_WALL_PROBE_OFFSET"));
    unsigned period=atoi(getenv("PSP_WALL_PERIOD_US"));
    const char* every_text=getenv("PSP_WALL_SNAPSHOT_EVERY");
    unsigned snapshot_every=every_text?atoi(every_text):32;if(!snapshot_every)snapshot_every=32;
    const char* snapshot=getenv("PSP_WALL_SNAPSHOT");
    const char* link_text=getenv("PSP_WALL_LINK_ADDRESS");
    uint64_t link=link_text?strtoull(link_text,NULL,0):0;
    char temporary[4096];if(snapshot)snprintf(temporary,sizeof(temporary),"%s.tmp",snapshot);
    uintptr_t symbol=strtoull(getenv("PSP_WALL_BASE_SYMBOL_VMADDR"),NULL,0);
    uint8_t** memory_base=(uint8_t**)dlsym(RTLD_DEFAULT,"_ZN6Memory4baseE");
    if(!memory_base&&symbol)memory_base=(uint8_t**)(symbol+_dyld_get_image_vmaddr_slide(0));
    FILE* f=fopen(output,"wx");if(!f)return NULL;setvbuf(f,NULL,_IOFBF,65536);
    fputs("lower_ms,upper_ms,request_ms,response_ms,tick,rendered,presented_tick,phase\n",f);fflush(f);
    uint64_t origin=wall_ns(),previous=origin,polls=0,changes=0,failures=0,max_read=0;
    RingReader ring=ring_open();ResponseReader response=response_open();
    uint32_t old_tick=~0u,old_rendered=~0u,old_presented=~0u;
    struct timespec pause={period/1000000,(long)(period%1000000)*1000};
    while(memory_base&&access(stop,F_OK)!=0){
        uint32_t words[32];mach_vm_size_t copied=0;
        uint64_t before=wall_ns();uint8_t* base=*memory_base;
        kern_return_t result=base?mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)(base+address),offset+4,(mach_vm_address_t)words,&copied):KERN_INVALID_ADDRESS;
        uint64_t after=wall_ns();++polls;if(after-before>max_read)max_read=after-before;
        if(result==KERN_SUCCESS&&copied==offset+4){
            uint32_t tick=words[0],rendered=words[1],presented=words[offset/4];
            if(tick!=old_tick||rendered!=old_rendered||presented!=old_presented){
                fprintf(f,"%.6f,%.6f,%.6f,%.6f,%u,%u,%u,%u\n",(previous-origin)/1e6,(after-origin)/1e6,(before-origin)/1e6,(after-origin)/1e6,tick,rendered,presented,words[3]);
                old_tick=tick;old_rendered=rendered;old_presented=presented;++changes;
                if(changes%64==0)fflush(f);
            }
        }else ++failures;
        if(snapshot&&base&&polls%snapshot_every==0){
            // Addressed records keep distant main/link symbols independent.
            // Each header is guest address + byte length, followed by its bytes.
            uint32_t packet[48];packet[0]=(uint32_t)address;packet[1]=offset+4;
            mach_vm_size_t length=0;
            unsigned count=2+packet[1]/4;
            bool valid=mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)(base+address),packet[1],(mach_vm_address_t)(packet+2),&length)==KERN_SUCCESS&&length==packet[1];
            if(valid&&link){
                packet[count]=(uint32_t)link;packet[count+1]=24;length=0;
                valid=mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)(base+link),24,(mach_vm_address_t)(packet+count+2),&length)==KERN_SUCCESS&&length==24;
                count+=8;
            }
            if(valid){
                int descriptor=open(temporary,O_WRONLY|O_CREAT|O_TRUNC,0600);
                if(descriptor>=0){ssize_t wanted=count*4;ssize_t written=write(descriptor,packet,wanted);close(descriptor);if(written==wanted)rename(temporary,snapshot);}
            }
        }
        ring_poll(&ring,base);response_poll(&response,base);
        previous=before;nanosleep(&pause,NULL);
    }
    ring_close(&ring,memory_base?*memory_base:NULL);response_close(&response,memory_base?*memory_base:NULL);
    fclose(f);
    FILE* report=fopen(summary,"w");if(report){
        fprintf(report,"{\"status\":\"recorded\",\"clock\":\"host CLOCK_MONOTONIC_RAW\",\"backend\":\"read-only Mach self-copy inside isolated emulator, no debugger polling\",\"requested_period_ms\":%.3f,\"probe_offset\":%u,\"polls\":%llu,\"changed_samples\":%llu,\"unmapped_reads\":%llu,\"maximum_selfcopy_ms\":%.6f,\"memory_symbol_resolved\":%s,\"elapsed_seconds\":%.6f}\n",period/1000.0,offset,polls,changes,failures,max_read/1e6,memory_base?"true":"false",(wall_ns()-origin)/1e9);fclose(report);
    }
    return NULL;
}
__attribute__((constructor))static void start_wall_worker(void){
    if(!getenv("PSP_WALL_OUTPUT"))return;
    pthread_t thread;if(pthread_create(&thread,NULL,wall_worker,NULL)==0)pthread_detach(thread);
}
