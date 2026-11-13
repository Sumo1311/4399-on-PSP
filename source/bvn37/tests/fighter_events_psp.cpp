// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_events.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Fighter Events QA",PSP_MODULE_USER,0,61);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(10240);
extern "C" {struct FighterEventsQA{unsigned status,error,rows,actors,bosses,minions,selfs,hashLow,hashHigh;}g_fighterEventsQa;}
int main(int argc,char** argv){std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_fighterEventsQa.status=1;bvn::fighter_events_qa::Checks c;const bool pass=c.run(path);
    g_fighterEventsQa={pass?2u:3u,c.error,c.rows,c.actors,c.bosses,c.minions,c.selfs,unsigned(c.hash),unsigned(c.hash>>32)};sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();}
