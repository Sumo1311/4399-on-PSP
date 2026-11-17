#pragma once
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <malloc.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
namespace pspboot {
inline char log_path[768]="BOOT_D27_03.LOG";
inline void mark(const char* stage,int code=0){
    char line[384];auto heap=mallinfo();
    int n=std::snprintf(line,sizeof(line),"%llu %s code=0x%08x heap_used=%u heap_arena_free=%u system_free=%u max_block=%u\n",
        (unsigned long long)sceKernelGetSystemTimeWide(),stage,(unsigned)code,
        (unsigned)heap.uordblks,(unsigned)heap.fordblks,
        (unsigned)sceKernelTotalFreeMemSize(),(unsigned)sceKernelMaxFreeMemSize());
    int fd=sceIoOpen(log_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
    if(fd>=0){if(n>0)sceIoWrite(fd,line,n<(int)sizeof(line)?n:(int)sizeof(line)-1);sceIoClose(fd);}
}
inline void begin(){
    char directory[512];if(getcwd(directory,sizeof(directory)))std::snprintf(log_path,sizeof(log_path),"%s/BOOT_D27_03.LOG",directory);
    int fd=sceIoOpen(log_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
    if(fd>=0)sceIoClose(fd);mark("main-entered");
    char cwd[512];if(getcwd(cwd,sizeof(cwd))){fd=sceIoOpen(log_path,PSP_O_WRONLY|PSP_O_APPEND,0666);if(fd>=0){sceIoWrite(fd,cwd,std::strlen(cwd));sceIoWrite(fd,"\n",1);sceIoClose(fd);}}
}
}
