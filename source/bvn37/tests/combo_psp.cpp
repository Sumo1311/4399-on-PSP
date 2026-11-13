// SPDX-License-Identifier: GPL-3.0-or-later
#include "combo.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Combo QA",PSP_MODULE_USER,0,62);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(10240);
extern "C" {struct ComboQA{unsigned status,error,rows,actors,matches,musou,assets,failedRow,failedColumn;float actual,expected;unsigned hashLow,hashHigh;}g_comboQa;}
int main(int argc,char** argv){std::string path=argc?argv[0]:"";const auto slash=path.find_last_of('/');path=slash==std::string::npos?"":path.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_comboQa.status=1;bvn::combo_qa::Checks c;const bool pass=c.run(path);
    g_comboQa={pass?2u:3u,c.error,c.rows,c.actors,c.matches,c.musou,c.assets,c.failedRow,c.failedColumn,float(c.actual),float(c.expected),unsigned(c.hash),unsigned(c.hash>>32)};sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();}
