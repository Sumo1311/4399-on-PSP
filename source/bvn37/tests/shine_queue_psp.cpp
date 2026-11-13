// SPDX-License-Identifier: GPL-3.0-or-later
#include "shine_queue.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Shine Queue QA",PSP_MODULE_USER,0,42);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);
extern "C"{struct ShineQueueQA{unsigned status,error,scenarios,states,emitted,maxLayers,emptied,hashLow,hashHigh;}g_shineQueueQa;}
int main(int argc,char** argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_shineQueueQa.status=1;bvn::shine_qa::Checks c;const bool pass=c.run(folder);
    g_shineQueueQa={pass?2U:3U,c.error,c.scenarios,c.states,c.emitted,c.maxLayers,c.emptied,unsigned(c.hash),unsigned(c.hash>>32)};sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
