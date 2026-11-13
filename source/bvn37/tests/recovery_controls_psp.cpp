// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery_controls.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>

PSP_MODULE_INFO("BVN Recovery Controls QA", PSP_MODULE_USER, 0, 36);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);

extern "C" {
struct RecoveryControlsQA {
    volatile unsigned status, banks, cases, escapes, error;
    char bank[16];
} g_recoveryControlsQa;
}

int main(int argc, char** argv) {
    using namespace bvn;
    const std::string launch = argc ? argv[0] : "";
    const auto slash = launch.find_last_of('/');
    const auto folder = slash == std::string::npos ? std::string{} : launch.substr(0, slash + 1);
    scePowerSetClockFrequency(333, 333, 166);
    pspDebugScreenInit();
    pspDebugScreenPrintf("BVN original recovery controls QA\n");
    g_recoveryControlsQa.status = 1;
    recovery_qa::Checks checks;
    const auto load = [&](FighterAsset& asset, const char* name) {
        return asset.open((folder + name).c_str(), FighterAsset::TextureResidency::LowMemory);
    };
    auto* input = std::fopen((folder + "BANKS.IN").c_str(), "r");
    if (!input) g_recoveryControlsQa.error = 1;
    else {
        char name[16]{};
        while (std::fscanf(input, "%15s", name) == 1) {
            std::strcpy(g_recoveryControlsQa.bank, name);
            FighterAsset asset;
            if (!load(asset, name)) { g_recoveryControlsQa.error = 2; break; }
            std::array<FighterAsset, 3> byakuya;
            std::span<const FighterAsset> forms;
            if (!std::strcmp(name, "BYAKUYNS.BVF")) {
                if (!load(byakuya[0], "BYAKUYA.BVF") || !load(byakuya[1], "BYAKUYB.BVF") || !load(byakuya[2], name)) {
                    g_recoveryControlsQa.error = 2; break;
                }
                forms = byakuya;
            }
            for (unsigned hz : {30U, 60U}) for (int direction : {-1, 1}) for (bool cpu : {false, true}) {
                if (!checks.escape(asset, hz, direction, cpu, forms)) {
                    g_recoveryControlsQa.error = checks.error; break;
                }
            }
            if (g_recoveryControlsQa.error) break;
            g_recoveryControlsQa.banks = g_recoveryControlsQa.banks + 1;
            g_recoveryControlsQa.cases = checks.cases;
            g_recoveryControlsQa.escapes = checks.escapes;
        }
        if (!std::feof(input) && !g_recoveryControlsQa.error) g_recoveryControlsQa.error = 3;
        std::fclose(input);
    }
    if (!g_recoveryControlsQa.error) {
        FighterAsset asset;
        if (!load(asset, "ICHIGO.BVF")) g_recoveryControlsQa.error = 2;
        else if (!checks.limits(asset) || !checks.mutableHits(asset)) g_recoveryControlsQa.error = checks.error;
    }
    g_recoveryControlsQa.cases = checks.cases;
    g_recoveryControlsQa.escapes = checks.escapes;
    if (!g_recoveryControlsQa.error && (g_recoveryControlsQa.banks != 78 || checks.cases != 634 || checks.escapes != 624)) {
        g_recoveryControlsQa.error = 4;
    }
    g_recoveryControlsQa.status = g_recoveryControlsQa.error ? 3 : 2;
    pspDebugScreenPrintf("Status %u banks %u cases %u escapes %u error %u\n", g_recoveryControlsQa.status,
        g_recoveryControlsQa.banks, checks.cases, checks.escapes, g_recoveryControlsQa.error);
    pspDebugScreenPrintf("Last %s\n", g_recoveryControlsQa.bank);
    sceKernelDcacheWritebackAll();
    for (;;) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CIRCLE) sceKernelExitGame();
        sceDisplayWaitVblankStart();
    }
}
