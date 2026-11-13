// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_blur.hpp"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Map Blur QA",PSP_MODULE_USER,0,49);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);
extern "C" {struct MapBlurQA {unsigned status,error,states,banks,clips,tiles,pixels,integrated,hashLow,hashHigh;} g_mapBlurQa;}
int main(int argc,char** argv){
 std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
 scePowerSetClockFrequency(333,333,166);g_mapBlurQa.status=1;bvn::blur_qa::Checks c;const bool pass=c.run(folder);
 g_mapBlurQa={pass?2U:3U,c.error,c.states,c.banks,c.clips,c.tiles,c.pixels,c.integrated,unsigned(c.hash),unsigned(c.hash>>32)};
 sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();
}
