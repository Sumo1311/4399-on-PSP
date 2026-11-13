// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene_integration.hpp"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Scene QA",PSP_MODULE_USER,0,63);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(12288);
extern "C" {struct SceneOrderQA {
    unsigned status,error,rows,rangeFaults,renderActions,contactActions,bulletRows,matches,switches,clashes,births,skips,failedRow,failedColumn;
    int actual,expected;
    unsigned referenceLow,referenceHigh,integrationLow,integrationHigh;
}g_sceneOrderQa;}
int main(int argc,char** argv){std::string folder=argc?argv[0]:"";const auto slash=folder.find_last_of('/');folder=slash==std::string::npos?"":folder.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);g_sceneOrderQa.status=1;bvn::scene_order_qa::Checks c;bvn::scene_order_qa::Integration i;
    const bool reference=c.reference(folder.c_str()),pass=reference&&i.run(folder.c_str());
    g_sceneOrderQa={pass?2u:3u,c.error?c.error:i.error,c.rows,c.rangeFaults,c.renderActions,c.contactActions,i.bulletRows,i.matches,i.switches,i.clashes,i.births,i.skips,c.failedRow,c.failedColumn,c.actual,c.expected,unsigned(c.hash),unsigned(c.hash>>32),unsigned(i.hash),unsigned(i.hash>>32)};
    sceKernelDcacheWritebackAll();for(;;)sceDisplayWaitVblankStart();}
