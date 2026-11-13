// SPDX-License-Identifier: GPL-3.0-or-later
#include "dispatch_transition.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Dispatch QA",PSP_MODULE_USER,0,57);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct DispatchTransitionQA{unsigned status,error,rootRows,timeoutRows,quickRows,assets,integration,pauses,hashLow,hashHigh;}g_dispatchTransitionQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_dispatchTransitionQa.status=1;bvn::dispatch_transition_qa::Checks c;const bool pass=c.run(path);
 g_dispatchTransitionQa={pass?2u:3u,c.error,c.rootRows,c.timeoutRows,c.quickRows,c.assets,c.integration,c.pauses,unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
