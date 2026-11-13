// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewport.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Camera QA",PSP_MODULE_USER,0,65);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(12288);
extern "C" {struct ViewportQA {
 unsigned status,error,rows,faults,forced,scenes,failedRow,failedColumn,hashLow,hashHigh;
 double actual,expected;
}g_viewportQa;}
static_assert(sizeof(ViewportQA)==56);
int main(int argc,char** argv){
 std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_viewportQa.status=1;bvn::viewport_qa::Checks c;const bool pass=c.run(folder);
 g_viewportQa={pass?2u:3u,c.error,c.rows,c.faults,c.forced,c.scenes,c.failedRow,c.failedColumn,unsigned(c.hash),unsigned(c.hash>>32),c.actual,c.expected};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
