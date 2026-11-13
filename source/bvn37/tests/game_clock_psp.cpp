// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_clock.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
PSP_MODULE_INFO("BVN Game Clock QA",PSP_MODULE_USER,0,51);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(64);
PSP_HEAP_SIZE_KB(1024);
extern "C" {struct GameClockQA {unsigned status,error,rows,actor,effect,split,hashLow,hashHigh;}g_gameClockQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 g_gameClockQa.status=1;bvn::game_clock_qa::Checks c;const bool pass=c.run(path+"GAMECLOCK.IN");
 g_gameClockQa={pass?2u:3u,c.error,c.rows,c.actorFrames,c.effectFrames,c.splitFrames,unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
