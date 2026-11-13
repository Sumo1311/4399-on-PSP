// SPDX-License-Identifier: GPL-3.0-or-later
#include "reaction_lifecycle.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Reaction QA",PSP_MODULE_USER,0,59);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(10240);
extern "C" {struct ReactionLifecycleQA{unsigned status,error,referenceRows,actors,banks,forced,versus,musou,pauses,failedRow,failedColumn;float actual,expected;unsigned hashLow,hashHigh;}g_reactionLifecycleQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_reactionLifecycleQa.status=1;bvn::reaction_lifecycle_qa::Checks c;const bool pass=c.run(path);
 g_reactionLifecycleQa={pass?2u:3u,c.error,c.referenceRows,c.actors,c.banks,c.forced,c.versus,c.musou,c.pauses,c.failedRow,c.failedColumn,float(c.actual),float(c.expected),unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
