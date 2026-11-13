// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspiofilemgr.h>
PSP_MODULE_INFO("BVN Settings QA",PSP_MODULE_USER,0,42);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);
extern "C"{struct SettingsQA{volatile unsigned status,error,variants,saves,rows,rounds,classicActions,standardActions,hashLow,hashHigh,tapCases;}g_settingsQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);pspDebugScreenInit();g_settingsQa.status=1;
 sceIoMkdir("ms0:/PSP/SAVEDATA",0777);sceIoMkdir("ms0:/PSP/SAVEDATA/BVNSETQA",0777);
 bvn::settings_qa::Checks c;const bool pass=c.run(path,"ms0:/PSP/SAVEDATA/BVNSETQA");
 g_settingsQa={pass?2U:3U,c.error,c.variants,c.saves,c.inputRows,c.rounds,c.classicActions,c.standardActions,unsigned(c.inputHash),unsigned(c.inputHash>>32),c.tapCases};
 pspDebugScreenPrintf("Settings status %u error %u variants %u saves %u inputs %u rounds %u classic %u normal %u hash %08x%08x\n",g_settingsQa.status,c.error,c.variants,c.saves,c.inputRows,c.rounds,c.classicActions,c.standardActions,unsigned(c.inputHash>>32),unsigned(c.inputHash));sceKernelDcacheWritebackAll();
 for(;;){SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);if(pad.Buttons&PSP_CTRL_CIRCLE)sceKernelExitGame();sceDisplayWaitVblankStart();}
}
