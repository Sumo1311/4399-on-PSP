#include "psp_stage_renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <memory>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <string>
#include <utility>
#include <vector>

PSP_MODULE_INFO("BVN 3.7 Stage QA", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

extern "C" {
struct StageQA {
    std::uint32_t ticks, drawCalls, map, airborne;
    float x, y;
} g_stageQa;
}

static std::vector<std::string> stagePaths(const std::string& folder) {
    const std::unique_ptr<FILE, decltype(&std::fclose)> list(
        std::fopen((folder + "STAGES.TXT").c_str(), "r"), std::fclose);
    if (!list) return {folder + "JIANCHANG.BVN", folder + "SENLIN.BVN"};
    std::vector<std::string> paths;
    char line[35];
    while (std::fgets(line, sizeof(line), list.get())) {
        std::string entry(line);
        if (entry.ends_with('\n')) entry.pop_back();
        if (entry.ends_with('\r')) entry.pop_back();
        if (entry.size() <= 4 || entry.size() > 32 || !entry.ends_with(".BVN") ||
            entry.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.") != std::string::npos ||
            paths.size() == 64) return {};
        paths.push_back(folder + entry);
    }
    if (std::ferror(list.get())) return {};
    return paths;
}

int main(int argc, char** argv) {
    using namespace bvn;
    const std::string launch = argc ? argv[0] : "";
    const auto slash = launch.find_last_of('/');
    const std::string folder = slash == std::string::npos ? "" : launch.substr(0, slash + 1);
    const auto stages = stagePaths(folder);
    StageAsset stage;
    if (stages.empty() || !stage.open(stages.front().c_str()) || !stage.spawn(0)) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("Missing or invalid stage files beside EBOOT.PBP\n");
        sceKernelSleepThread();
        return 1;
    }

    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    StageRenderer renderer;
    renderer.start();
    sceKernelDcacheWritebackAll();

    Motion probe;
    probe.position = *stage.spawn(0);
    unsigned previous = 0;
    for (;;) {
        SceCtrlData pads[16]{};
        const int count = sceCtrlReadBufferPositive(pads, std::size(pads));
        if (count <= 0) {
            sceDisplayWaitVblankStart();
            continue;
        }
        for (int i = 0; i < count; ++i) {
            const unsigned held = pads[i].Buttons;
            const unsigned pressed = held & ~previous;
            previous = held;
            if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) {
                const unsigned advance = pressed & PSP_CTRL_LTRIGGER ? stages.size() - 1 : 1;
                const unsigned next = (g_stageQa.map + advance) % stages.size();
                StageAsset replacement;
                if (replacement.open(stages[next].c_str()) && replacement.spawn(0)) {
                    stage = std::move(replacement);
                    probe = {};
                    probe.position = *stage.spawn(0);
                    g_stageQa.map = next;
                    sceKernelDcacheWritebackAll();
                }
            }
            if ((pressed & PSP_CTRL_CROSS) && !probe.airborne) probe.jump(19);
            probe.step(stage.geometry());
            const int direction = int(bool(held & PSP_CTRL_RIGHT)) - int(bool(held & PSP_CTRL_LEFT));
            probe.move({12.0 * direction, 0});
            ++g_stageQa.ticks;
        }
        const auto bounds = stage.geometry();
        const double maxCameraX = std::max(bounds.left, bounds.right - StageViewWidth);
        const double cameraX = std::clamp(probe.position.x - StageViewWidth / 2, bounds.left, maxCameraX);
        renderer.draw(stage, {{cameraX, 0}, 1});
        ++g_stageQa.drawCalls;
        g_stageQa.x = float(probe.position.x);
        g_stageQa.y = float(probe.position.y);
        g_stageQa.airborne = probe.airborne;
    }
}
