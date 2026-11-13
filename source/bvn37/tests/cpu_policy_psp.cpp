// SPDX-License-Identifier: GPL-3.0-or-later
#include "cpu_policy_trace.hpp"
#include <string>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>

PSP_MODULE_INFO("BVN CPU Policy QA", PSP_MODULE_USER, 0, 36);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(96);
PSP_HEAP_SIZE_KB(8192);

extern "C" {
struct CpuPolicyQA {
    volatile unsigned status, rows, resets, elapsedMicros;
    volatile int error;
} g_cpuPolicyQa;
}

int main(int argc, char** argv) {
    const std::string launch = argc ? argv[0] : "";
    const auto slash = launch.find_last_of('/');
    const auto folder = slash == std::string::npos ? std::string{} : launch.substr(0, slash + 1);
    scePowerSetClockFrequency(333,333,166);
    pspDebugScreenInit();
    pspDebugScreenPrintf("BVN original CPU policy QA\n");
    auto* input = std::fopen((folder + "POLICY.IN").c_str(), "rb");
    auto* output = std::fopen((folder + "POLICY.OUT").c_str(), "wb");
    static char inputBuffer[64 * 1024], outputBuffer[64 * 1024];
    if (!input || !output) {
        g_cpuPolicyQa.error = -1;
        g_cpuPolicyQa.status = 3;
    } else {
        std::setvbuf(input, inputBuffer, _IOFBF, sizeof(inputBuffer));
        std::setvbuf(output, outputBuffer, _IOFBF, sizeof(outputBuffer));
        g_cpuPolicyQa.status = 1;
        const auto started = sceKernelGetSystemTimeWide();
        const auto result = traceCpuPolicy(input, output, &g_cpuPolicyQa.rows);
        g_cpuPolicyQa.resets = result.resets;
        g_cpuPolicyQa.error = result.error;
        g_cpuPolicyQa.elapsedMicros = unsigned(sceKernelGetSystemTimeWide() - started);
        const int inputClosed = std::fclose(input), outputClosed = std::fclose(output);
        if (inputClosed || outputClosed) g_cpuPolicyQa.error = 8;
        g_cpuPolicyQa.status = g_cpuPolicyQa.error || !g_cpuPolicyQa.rows ? 3 : 2;
        pspDebugScreenPrintf("Status %u, rows %u, sequences %u, error %d\n", g_cpuPolicyQa.status,
            g_cpuPolicyQa.rows, g_cpuPolicyQa.resets, g_cpuPolicyQa.error);
    }
    sceKernelDcacheWritebackAll();
    for (;;) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(&pad,1);
        if (pad.Buttons & PSP_CTRL_CIRCLE) sceKernelExitGame();
        sceDisplayWaitVblankStart();
    }
}
