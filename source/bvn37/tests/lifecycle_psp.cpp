// SPDX-License-Identifier: GPL-3.0-or-later
#include "lifecycle.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Lifecycle QA",PSP_MODULE_USER,0,64);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(12288);
extern "C" {struct LifecycleQA {
 unsigned status,error,rows,alphaRows,races,fades,actors,failedRow,failedColumn,actual,expected,hashLow,hashHigh;
}g_lifecycleQa;}
int main(int argc,char** argv){
 std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_lifecycleQa.status=1;bvn::lifecycle_qa::Checks c;const bool pass=c.run(folder);
 g_lifecycleQa={pass?2u:3u,c.error,c.rows,c.alphaRows,c.races,c.fades,c.actors,c.failedRow,c.failedColumn,c.actual,c.expected,unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
