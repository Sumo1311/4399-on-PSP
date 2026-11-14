#pragma once
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <malloc.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>
namespace aow {
extern "C" void boot10_record(const char*);
extern "C" void boot10_release_display(void);
// A small startup-only record for a physical PSP. No runtime-frame logging.
inline void bootTrace(const char* stage){
 char line[320];
 auto mem=mallinfo();int n=std::snprintf(line,sizeof(line),"%llu %s heap_used=%u heap_arena_free=%u system_free=%u max_block=%u\n",(unsigned long long)sceKernelGetSystemTimeWide(),stage,unsigned(mem.uordblks),unsigned(mem.fordblks),unsigned(sceKernelTotalFreeMemSize()),unsigned(sceKernelMaxFreeMemSize()));
 if(n>0)boot10_record(line);
}
}
