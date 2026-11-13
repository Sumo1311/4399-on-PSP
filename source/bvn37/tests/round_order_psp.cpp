// SPDX-License-Identifier: GPL-3.0-or-later
#include "round_order.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Round Order QA",PSP_MODULE_USER,0,55);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct RoundOrderQA{unsigned status,error,rows,cutins,manual,expiry,hp,duals,banners,hooks,hashLow,hashHigh;}g_roundOrderQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_roundOrderQa.status=1;bvn::round_order_qa::Checks c;const bool pass=c.run(path);
 g_roundOrderQa={pass?2u:3u,c.error,c.rows,c.cutins,c.manual,c.expiry,c.hp,c.duals,c.banners,c.hooks,unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
