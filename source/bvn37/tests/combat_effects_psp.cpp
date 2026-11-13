// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_effects_checks.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Combat Source QA",PSP_MODULE_USER,0,44);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" { struct CombatEffectsQA { unsigned status,error,definitions,queries,clips,frames,pixels,contacts,hashLow,hashHigh,geometry; } g_combatEffectsQa; }
int main(int argc,char** argv) {
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_combatEffectsQa.status=1;bvn::combat_effects_qa::Checks c;const bool pass=c.run(folder);
    g_combatEffectsQa={pass?2U:3U,c.error,c.definitions,c.queries,c.clips,c.frames,c.pixels,c.contacts,unsigned(c.hash),unsigned(c.hash>>32),c.geometry};
    sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
