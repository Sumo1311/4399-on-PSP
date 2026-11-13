// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller_session.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Session QA",PSP_MODULE_USER,0,54);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct ControllerSessionQA{unsigned status,error,reference,counters,duals,musou,ghosts,bossUi,manual,bossAi,banks,ends,authored,transforms,hashLow,hashHigh;}g_controllerSessionQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_controllerSessionQa.status=1;bvn::controller_session_qa::Checks c;const bool pass=c.run(path);
 g_controllerSessionQa={pass?2u:3u,c.error,c.referenceRows,c.counterRows,c.duals,c.musou,c.ghosts,c.bossUi,c.manual,c.bossAi,c.typedBanks,c.typedEnds,c.authoredGhosts,c.transformations,unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
