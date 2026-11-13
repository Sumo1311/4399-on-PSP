// SPDX-License-Identifier: GPL-3.0-or-later
#include "hit_controller.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Hit Controller QA",PSP_MODULE_USER,0,60);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(10240);
extern "C" {struct HitControllerQA{unsigned status,error,rows,actors,sessions,failedRow,failedColumn;float actual,expected;unsigned hashLow,hashHigh;}g_hitControllerQa;}
int main(int argc,char** argv){std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_hitControllerQa.status=1;bvn::hit_controller_qa::Checks c;const bool pass=c.run(path);
 g_hitControllerQa={pass?2u:3u,c.error,c.rows,c.actors,c.sessions,c.failedRow,c.failedColumn,float(c.actual),float(c.expected),unsigned(c.hash),unsigned(c.hash>>32)};sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();}
