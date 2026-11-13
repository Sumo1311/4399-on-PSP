// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud_session.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN HUD Session QA",PSP_MODULE_USER,0,58);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(10240);
extern "C" {struct HudSessionQA{unsigned status,error,hudRows,musouRows,versusCases,musouCases,pauses,assets,hashLow,hashHigh;}g_hudSessionQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_hudSessionQa.status=1;bvn::hud_session_qa::Checks c;const bool pass=c.run(path);
 g_hudSessionQa={pass?2u:3u,c.error,c.hudRows,c.musouRows,c.versusCases,c.musouCases,c.pauses,c.assets,unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
