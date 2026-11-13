// SPDX-License-Identifier: GPL-3.0-or-later
#include "source_delivery.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Source Delivery QA",PSP_MODULE_USER,0,52);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct SourceDeliveryQA {unsigned status,error,motion,bullets,calls,inputs,rounds,freezes,musou,hashLow,hashHigh,badField,wantLow,wantHigh,actualLow,actualHigh;}g_sourceDeliveryQa;}
int main(int argc,char** argv){
 std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_sourceDeliveryQa.status=1;bvn::source_delivery_qa::Checks c;const bool pass=c.run(path);
 const auto want=std::bit_cast<std::uint64_t>(c.expected),actual=std::bit_cast<std::uint64_t>(c.actual);
 g_sourceDeliveryQa={pass?2u:3u,c.error,c.motions,c.bullets,c.calls,c.inputs,c.rounds,c.freezes,c.musou,unsigned(c.hash),unsigned(c.hash>>32),c.badField,unsigned(want),unsigned(want>>32),unsigned(actual),unsigned(actual>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
