// SPDX-License-Identifier: GPL-3.0-or-later
#include "fighter_motion.hpp"
#include <cstring>
#include <string>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>

PSP_MODULE_INFO("BVN CPU Bridge QA", PSP_MODULE_USER, 0, 36);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);

extern "C" {
struct CpuRuntimeBridgeQA {
    volatile unsigned status, queries, banks, lastCommand;
    volatile int error;
    char bank[16];
} g_cpuRuntimeBridgeQa;
}

int main(int argc, char** argv) {
    using namespace bvn;
    const std::string launch = argc ? argv[0] : "";
    const auto slash = launch.find_last_of('/');
    const auto folder = slash == std::string::npos ? std::string{} : launch.substr(0, slash + 1);
    scePowerSetClockFrequency(333,333,166);
    pspDebugScreenInit();
    pspDebugScreenPrintf("BVN original CPU hit queries\n");
    auto* input = std::fopen((folder + "TRAITS.IN").c_str(), "r");
    g_cpuRuntimeBridgeQa.status = 1;
    if (!input) g_cpuRuntimeBridgeQa.error = 1;
    else {
        FighterAsset asset;
        char name[16]{}, previous[16]{};
        unsigned command, breaks, down;
        while (std::fscanf(input, "%15s %u %u %u", name, &command, &breaks, &down) == 4) {
            std::memcpy(g_cpuRuntimeBridgeQa.bank, name, sizeof(name));
            g_cpuRuntimeBridgeQa.lastCommand = command;
            if (std::strcmp(previous, name)) {
                if (!asset.open((folder + name).c_str(), FighterAsset::TextureResidency::LowMemory)) {
                    g_cpuRuntimeBridgeQa.error = 2; break;
                }
                std::strcpy(previous, name);
                g_cpuRuntimeBridgeQa.banks = g_cpuRuntimeBridgeQa.banks + 1;
            }
            if (command >= unsigned(FighterCommand::Count) || breaks > 1 || down > 1) {
                g_cpuRuntimeBridgeQa.error = 3; break;
            }
            FighterMotion actor(asset);
            actor.reset({400,300},1);
            const auto actual = actor.aiHitTraits(FighterCommand(command));
            if (actual.breaksGuard != bool(breaks) || actual.knocksDown != bool(down)) {
                g_cpuRuntimeBridgeQa.error = 4; break;
            }
            g_cpuRuntimeBridgeQa.queries = g_cpuRuntimeBridgeQa.queries + 1;
        }
        if (!std::feof(input) && !g_cpuRuntimeBridgeQa.error) g_cpuRuntimeBridgeQa.error = 5;
        if (std::fclose(input) && !g_cpuRuntimeBridgeQa.error) g_cpuRuntimeBridgeQa.error = 6;
    }
    if (!g_cpuRuntimeBridgeQa.error && (g_cpuRuntimeBridgeQa.queries != 780 || g_cpuRuntimeBridgeQa.banks != 78)) {
        g_cpuRuntimeBridgeQa.error = 7;
    }
    g_cpuRuntimeBridgeQa.status = g_cpuRuntimeBridgeQa.error ? 3 : 2;
    pspDebugScreenPrintf("Status %u, queries %u, banks %u, error %d\n", g_cpuRuntimeBridgeQa.status,
        g_cpuRuntimeBridgeQa.queries, g_cpuRuntimeBridgeQa.banks, g_cpuRuntimeBridgeQa.error);
    pspDebugScreenPrintf("Last %s command %u\n", g_cpuRuntimeBridgeQa.bank, g_cpuRuntimeBridgeQa.lastCommand);
    sceKernelDcacheWritebackAll();
    for (;;) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(&pad,1);
        if (pad.Buttons & PSP_CTRL_CIRCLE) sceKernelExitGame();
        sceDisplayWaitVblankStart();
    }
}
