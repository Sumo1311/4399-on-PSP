// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_buff.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Combat Buff QA",PSP_MODULE_USER,0,46);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" { struct CombatBuffQA { unsigned status,error,states,views,integrated,holds,renewals,hashLow,hashHigh; } g_combatBuffQa; }
int main(int argc,char** argv) {
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_combatBuffQa.status=1;
    bvn::buff_qa::Checks c;const bool pass=c.run(folder);
    g_combatBuffQa={pass?2U:3U,c.error,c.states,c.views,c.integrated,c.holds,c.renewals,unsigned(c.hash),unsigned(c.hash>>32)};
    sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
