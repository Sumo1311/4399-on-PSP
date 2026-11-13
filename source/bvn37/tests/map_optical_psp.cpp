// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_optical_checks.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Map Optical QA",PSP_MODULE_USER,0,50);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct MapOpticalQA {unsigned status,error,states,half,maps,variants,palette,pixels,lookups,captures,objectBanks,objectAreas,hashLow,hashHigh,integrated;} g_mapOpticalQa;}
int main(int argc,char** argv){
 std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_mapOpticalQa.status=1;bvn::optical_qa::Checks c;const bool pass=c.run(folder);
 g_mapOpticalQa={pass?2U:3U,c.error,c.states,c.half,c.maps,c.variants,c.palette,c.pixels,c.lookups,c.captures,c.objectBanks,c.objectAreas,unsigned(c.hash),unsigned(c.hash>>32),c.integrated};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
