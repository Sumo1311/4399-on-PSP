// SPDX-License-Identifier: GPL-3.0-or-later
#include "counter_controls.hpp"
#include "counter_assets.hpp"
#include <cstdio>
#include <string>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Counter Controls QA",PSP_MODULE_USER,0,38);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);
extern "C" {
struct CounterControlsQA { volatile unsigned status,cases,starts,frames,error; } g_counterControlsQa;
}
int main(int argc,char** argv) {
    using namespace bvn;
    std::string launch=argc?argv[0]:"";auto slash=launch.find_last_of('/');
    auto folder=slash==std::string::npos?std::string{}:launch.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);pspDebugScreenInit();
    pspDebugScreenPrintf("BVN source counter controls and FX loader QA\n");
    g_counterControlsQa.status=1;counter_qa::Checks checks;
    FighterAsset first,second;
    unsigned error=0,frames=0;
    if(!first.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)||
       !second.open((folder+"NARUTO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory))error=1;
    if(!error)for(unsigned faction:{0U,1U})for(unsigned hz:{30U,60U})for(int facing:{-1,1})for(bool cpu:{false,true})for(double energy:{50.,100.}) {
        if(!checks.start(faction?second:first,faction,hz,facing,cpu,energy)){error=checks.error;break;}
    }
    if(!error&&!checks.gates(first))error=checks.error;
    if(!error&&!counter_qa::pixels((folder+"REACTION.BVF").c_str(),(folder+"FX.IN").c_str(),frames,error)) {
        if(!error)error=207;
    }
    if(!error&&(checks.cases!=99||checks.starts!=32))error=2;
    g_counterControlsQa.cases=checks.cases;g_counterControlsQa.starts=checks.starts;
    g_counterControlsQa.frames=frames;g_counterControlsQa.error=error;g_counterControlsQa.status=error?3:2;
    pspDebugScreenPrintf("Status %u cases %u starts %u pixels %u error %u\n",g_counterControlsQa.status,checks.cases,checks.starts,frames,error);
    sceKernelDcacheWritebackAll();
    for(;;){SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);if(pad.Buttons&PSP_CTRL_CIRCLE)sceKernelExitGame();sceDisplayWaitVblankStart();}
}
