// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_status.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Combat Status QA",PSP_MODULE_USER,0,44);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C"{struct CombatStatusQA{unsigned status,error,states,colors,views,callbacks,zero,integrated,hashLow,hashHigh,defenses;}g_combatStatusQa;}
int main(int argc,char**argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_combatStatusQa.status=1;bvn::status_qa::Checks c;const bool pass=c.run(folder);
    g_combatStatusQa={pass?2U:3U,c.error,c.states,c.colors,c.views,c.callbacks,c.zeroAngles,c.integrated,unsigned(c.hash),unsigned(c.hash>>32),c.defenses};
    sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
