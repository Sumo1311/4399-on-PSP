// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_asset.hpp"
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>

PSP_MODULE_INFO("BVN Recovery Assets QA",PSP_MODULE_USER,0,36);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);

extern "C" {
struct RecoveryAssetsQA {
    volatile unsigned status,banks,frames,phase,error;
    char bank[16];
} g_recoveryAssetsQa;
}

namespace {
struct PoseShape { float x,y; std::uint16_t width,height,stride,rows; };
static_assert(sizeof(PoseShape)==16);
uLong poseCrc(uLong crc,const bvn::FighterPose& pose) {
    const PoseShape shape{pose.x,pose.y,pose.width,pose.height,pose.stride,pose.rows};
    return crc32(crc,reinterpret_cast<const Bytef*>(&shape),sizeof(shape));
}
}

int main(int argc,char** argv) {
    using namespace bvn;
    const std::string launch=argc?argv[0]:"";
    const auto slash=launch.find_last_of('/');
    const auto folder=slash==std::string::npos?std::string{}:launch.substr(0,slash+1);
    scePowerSetClockFrequency(333,333,166);
    pspDebugScreenInit();
    pspDebugScreenPrintf("BVN original recovery asset QA\n");
    auto* input=std::fopen((folder+"RECOVER.IN").c_str(),"r");
    g_recoveryAssetsQa.status=1;
    if(!input)g_recoveryAssetsQa.error=1;
    else {
        FighterAsset asset;
        std::vector<std::byte> pixels;
        char name[16]{};
        unsigned addedClips;
        while(std::fscanf(input,"%15s %u",name,&addedClips)==2) {
            std::strcpy(g_recoveryAssetsQa.bank,name);
            if(!asset.open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory)) {
                g_recoveryAssetsQa.error=2;break;
            }
            g_recoveryAssetsQa.banks=g_recoveryAssetsQa.banks+1;
            for(unsigned c=0;c<addedClips;++c) {
            unsigned index,timeline,count,repeat;
            if(std::fscanf(input,"%u %u %u %u",&index,&timeline,&count,&repeat)!=4) {g_recoveryAssetsQa.error=3;break;}
            const auto recovery=MoveClip(index);
            if(index!=unsigned(asset.hurtEscape())+c||!asset.has(recovery)||asset.clip(recovery).count!=count||
               asset.clip(recovery).timelineStart!=timeline||asset.clip(recovery).repeat!=repeat) {
                g_recoveryAssetsQa.error=3;break;
            }
            for(unsigned phase=0;phase<count;++phase) {
                g_recoveryAssetsQa.phase=phase;
                unsigned expected;
                if(std::fscanf(input,"%u",&expected)!=1) {g_recoveryAssetsQa.error=4;break;}
                const auto& frame=asset.frame(recovery,phase);
                uLong crc=crc32(0,reinterpret_cast<const Bytef*>(&frame.body),sizeof(frame.body));
                crc=poseCrc(crc,frame.pose);
                for(const auto& tile:asset.poseTiles(frame.pose)) {
                    crc=poseCrc(crc,tile);
                    if(!tile.stride||!tile.width||!tile.height)continue;
                    pixels.resize(FighterAsset::textureSize(tile));
                    if(!asset.decodePixels(tile,pixels)) {g_recoveryAssetsQa.error=5;break;}
                    crc=crc32(crc,reinterpret_cast<const Bytef*>(pixels.data()),pixels.size());
                }
                if(g_recoveryAssetsQa.error)break;
                if(crc!=expected) {g_recoveryAssetsQa.error=6;break;}
                g_recoveryAssetsQa.frames=g_recoveryAssetsQa.frames+1;
            }
            if(g_recoveryAssetsQa.error)break;
            }
            if(g_recoveryAssetsQa.error)break;
        }
        if(!std::feof(input)&&!g_recoveryAssetsQa.error)g_recoveryAssetsQa.error=7;
        if(std::fclose(input)&&!g_recoveryAssetsQa.error)g_recoveryAssetsQa.error=8;
    }
    if(!g_recoveryAssetsQa.error&&(g_recoveryAssetsQa.banks!=78||g_recoveryAssetsQa.frames!=866))g_recoveryAssetsQa.error=9;
    g_recoveryAssetsQa.status=g_recoveryAssetsQa.error?3:2;
    pspDebugScreenPrintf("Status %u, banks %u, frames %u, error %u\n",g_recoveryAssetsQa.status,g_recoveryAssetsQa.banks,g_recoveryAssetsQa.frames,g_recoveryAssetsQa.error);
    pspDebugScreenPrintf("Last %s phase %u\n",g_recoveryAssetsQa.bank,g_recoveryAssetsQa.phase);
    sceKernelDcacheWritebackAll();
    for(;;) {
        SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);
        if(pad.Buttons&PSP_CTRL_CIRCLE)sceKernelExitGame();
        sceDisplayWaitVblankStart();
    }
}
