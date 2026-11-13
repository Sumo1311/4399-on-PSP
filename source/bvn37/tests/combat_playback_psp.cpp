// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat_playback.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Combat Playback QA",PSP_MODULE_USER,0,43);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct CombatPlaybackQA {unsigned status,error,shake,caps,lifetimes,hashLow,hashHigh,caches,freezes;}g_combatPlaybackQa;}
int main(int argc,char** argv){
    std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_combatPlaybackQa.status=1;bvn::combat_playback_qa::Checks c;const bool pass=c.run(folder);
    g_combatPlaybackQa={pass?2U:3U,c.error,c.shakeStates,c.caps,c.lifetimes,unsigned(c.hash),unsigned(c.hash>>32),c.caches,c.freezes};
    sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
