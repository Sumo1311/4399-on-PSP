// SPDX-License-Identifier: GPL-3.0-or-later
#include "ghost_controls.hpp"
#include <cstdio>
#include <string>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("BVN Ghost Controls QA",PSP_MODULE_USER,0,39);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);
extern "C" {
struct GhostControlsQA{volatile unsigned status,banks,cases,starts,ends,shadowFrames,error;}g_ghostControlsQa;
}
int main(int argc,char** argv){
    using namespace bvn;std::string launch=argc?argv[0]:"";auto slash=launch.find_last_of('/');
    const auto folder=slash==std::string::npos?std::string{}:launch.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);pspDebugScreenInit();
    pspDebugScreenPrintf("BVN original ghost controls and blue shadow sampling QA\n");
    g_ghostControlsQa.status=1;ghost_qa::Checks checks;unsigned banks=0,error=0;
    FILE* list=std::fopen((folder+"BANKS.IN").c_str(),"r");char name[32];
    if(!list)error=1;
    while(!error&&std::fscanf(list,"%31s",name)==1){
        FighterAsset asset;
        if(!asset.open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory)){error=2;break;}
        for(unsigned scenario=0;scenario<4&&!error;++scenario)for(unsigned hz:{30U,60U})for(int direction:{-1,1})for(bool cpu:{false,true}){
            if(!checks.exercise(asset,scenario,hz,direction,cpu))error=checks.error;
        }
        ++banks;g_ghostControlsQa.banks=banks;sceKernelDcacheWritebackAll();
    }
    if(list)std::fclose(list);
    std::array<FighterAsset,2> forms;
    if(!error&&(!forms[0].open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)||
        !forms[1].open((folder+"ICHIGOB.BVF").c_str(),FighterAsset::TextureResidency::LowMemory)))error=3;
    if(!error&&!checks.gates(forms))error=checks.error;
    if(!error&&banks!=78)error=4;
    g_ghostControlsQa.cases=checks.cases;g_ghostControlsQa.starts=checks.starts;g_ghostControlsQa.ends=checks.ends;
    g_ghostControlsQa.shadowFrames=checks.shadowFrames;g_ghostControlsQa.error=error;g_ghostControlsQa.status=error?3:2;
    pspDebugScreenPrintf("Status %u banks %u cases %u starts %u ends %u shadow ticks %u error %u\n",g_ghostControlsQa.status,banks,checks.cases,checks.starts,checks.ends,checks.shadowFrames,error);
    sceKernelDcacheWritebackAll();for(;;){SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);if(pad.Buttons&PSP_CTRL_CIRCLE)sceKernelExitGame();sceDisplayWaitVblankStart();}
}
