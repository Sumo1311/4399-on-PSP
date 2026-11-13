// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_effects.hpp"
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Frame Effects QA",PSP_MODULE_USER,0,40);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);
extern "C"{struct FrameEffectsQA{volatile unsigned status,banks,cases,walks,puffs,tints,lifetimes,error;}g_frameEffectsQa;}
int main(int argc,char** argv){
 std::string launch=argc?argv[0]:"";const auto slash=launch.find_last_of('/');const auto folder=slash==std::string::npos?std::string{}:launch.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);pspDebugScreenInit();g_frameEffectsQa.status=1;
 bvn::frame_effects_qa::Checks c;const bool pass=c.run(folder,folder+"FRAMEFX.IN");
 g_frameEffectsQa={pass?2U:3U,c.banks,c.cases,c.walks,c.puffs,c.tints,c.lifetimes,c.error};
 pspDebugScreenPrintf("Frame effects: status %u banks %u cases %u walks %u puffs %u colors %u lifetime %u error %u\n",g_frameEffectsQa.status,c.banks,c.cases,c.walks,c.puffs,c.tints,c.lifetimes,c.error);
 sceKernelDcacheWritebackAll();for(;;){SceCtrlData p{};sceCtrlPeekBufferPositive(&p,1);if(p.Buttons&PSP_CTRL_CIRCLE)sceKernelExitGame();sceDisplayWaitVblankStart();}
}
