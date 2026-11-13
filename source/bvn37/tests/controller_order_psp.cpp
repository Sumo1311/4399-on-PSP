// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller_order.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Controller Order QA",PSP_MODULE_USER,0,53);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct ControllerOrderQA{unsigned status,error,rows,cutins,ghosts,ghostFrames,hooks,dual,hashLow,hashHigh;}g_controllerOrderQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_controllerOrderQa.status=1;bvn::controller_order_qa::Checks c;const bool pass=c.run(path);
 g_controllerOrderQa={pass?2u:3u,c.error,c.rows,c.cutins,c.ghosts,c.ghostFrames,c.hooks,c.dual,unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
