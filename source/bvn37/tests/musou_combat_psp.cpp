// SPDX-License-Identifier: GPL-3.0-or-later
#include "musou_combat.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Musou Combat QA",PSP_MODULE_USER,0,45);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" { struct MusouCombatProbeQA { unsigned status,error,styles,guards,freezes,battles,held,effects,bosses,projectiles,hashLow,hashHigh,fades,switches,deaths; } g_musouCombatProbeQa; }
int main(int argc,char** argv) {
    std::string folder=argc?argv[0]:""; const auto slash=folder.find_last_of('/');
    folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166); g_musouCombatProbeQa.status=1;
    bvn::musou_combat_qa::Checks c; const bool pass=c.run(folder);
    g_musouCombatProbeQa={pass?2U:3U,c.error,c.styles,c.guards,c.freezes,c.battles,c.held,c.effects,c.bosses,c.projectiles,unsigned(c.hash),unsigned(c.hash>>32),c.fades,c.switches,c.deaths};
    sceKernelDcacheWritebackAll(); for(;;) sceDisplayWaitVblankStart();
}
