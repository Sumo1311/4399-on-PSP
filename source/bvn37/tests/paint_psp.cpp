// SPDX-License-Identifier: GPL-3.0-or-later
#include "paint.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Paint QA",PSP_MODULE_USER,0,66);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(16384);
extern "C" {struct PaintQA {unsigned status,error,rows,faults,changes,matches,switches,layers,failedRow,failedColumn,actualLow,actualHigh,expectedLow,expectedHigh,hashLow,hashHigh;}g_paintQa;}
int main(int argc,char** argv){
 std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_paintQa.status=1;bvn::paint_qa::Checks c;const bool pass=c.run(folder);
 g_paintQa={pass?2u:3u,c.error,c.rows,c.faults,c.changes,c.matches,c.switches,c.layers,c.failedRow,c.failedColumn,unsigned(c.actual),unsigned(c.actual>>32),unsigned(c.expected),unsigned(c.expected>>32),unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
