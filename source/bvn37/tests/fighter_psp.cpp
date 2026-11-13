#include "fight_match.hpp"
#include "psp_stage_renderer.hpp"

#include <algorithm>
#include <iterator>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <string>
#include <utility>
#include <vector>

PSP_MODULE_INFO("BVN 3.7 Action QA", PSP_MODULE_USER, 0, 7);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

constexpr unsigned TraceCapacity = 1024;
constexpr unsigned ReplayTwoControllers = 1u << 31;

extern "C" {
struct FighterQA {
    std::uint32_t ticks, draws, animations, map, active;
    struct Actor {
        float x, y, vx, vy;
        std::int32_t facing;
        std::uint32_t frame, phase, airborne;
        float health, qi, energy;
        std::uint32_t flags;
    } actors[2];
    float shakeY;
    std::uint32_t shineColor, shineAlpha;
    float cameraX, cameraY, zoom;
    std::uint32_t objectCount;
    struct Object {
        float x, y, vx, vy;
        std::uint32_t owner, phase, flags;
    } objects[4];
} g_fighterQa;
FighterQA g_fighterTrace[TraceCapacity];
struct FighterReplay {
    std::uint32_t request, count, firstTick, lastTick;
    std::uint32_t buttons[TraceCapacity];
};
volatile FighterReplay g_fighterReplay{};
}
static_assert(sizeof(FighterQA) == 256);

static bvn::FighterControls controlsFromPad(unsigned held, unsigned pressed) {
    return {int(bool(held & PSP_CTRL_RIGHT)) - int(bool(held & PSP_CTRL_LEFT)),
            int(bool(held & PSP_CTRL_UP)) - int(bool(held & PSP_CTRL_DOWN)),
            bool((pressed & PSP_CTRL_CROSS) && !(held & PSP_CTRL_SQUARE)),
            bool(pressed & PSP_CTRL_SQUARE), bool(pressed & PSP_CTRL_LTRIGGER), bool(held & PSP_CTRL_SQUARE),
            bool(pressed & PSP_CTRL_CIRCLE)};
}

int main(int argc, char** argv) {
    using namespace bvn;
    const std::string launch = argc ? argv[0] : "";
    const auto slash = launch.find_last_of('/');
    const std::string folder = slash == std::string::npos ? "" : launch.substr(0, slash + 1);
    const std::string stages[]{folder + "JIANCHANG.BVN", folder + "SENLIN.BVN"};
    StageAsset stage;
    FighterAsset assets[2];
    if (!stage.open(stages[0].c_str()) || !stage.spawn(0) || !stage.spawn(1) ||
        !assets[0].open((folder + "ICHIGO.BVF").c_str()) || !assets[1].open((folder + "NARUTO.BVF").c_str())) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("Missing or invalid stage/fighter files beside EBOOT.PBP\n");
        sceKernelSleepThread();
        return 1;
    }

    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    StageRenderer renderer;
    renderer.start();
    sceKernelDcacheWritebackAll();

    FightMatch match(assets[0], assets[1]);
    std::vector<FighterView> sprites;
    sprites.reserve(2 + FightMatch::ObjectLimit);
    const auto placeActors = [&]() {
        match.reset(stage, *stage.spawn(0), *stage.spawn(1));
    };
    placeActors();
    FighterQA stats{};
    unsigned previous = 0;
    unsigned replayRequest = 0, replayCount = 0, replayIndex = 0;
    bool twoControllers = false;
    unsigned replayPrevious[2]{};
    for (;;) {
        SceCtrlData pads[16]{};
        const int count = sceCtrlReadBufferPositive(pads, std::size(pads));
        if (count <= 0) {
            sceDisplayWaitVblankStart();
            continue;
        }
        for (int i = 0; i < count; ++i) {
            const unsigned request = g_fighterReplay.request;
            if (request != replayRequest) {
                replayRequest = request;
                const unsigned modeCount = g_fighterReplay.count;
                const unsigned requested = modeCount & ~ReplayTwoControllers;
                replayCount = requested <= TraceCapacity ? requested : 0;
                twoControllers = modeCount & ReplayTwoControllers;
                replayIndex = 0;
                replayPrevious[0] = replayPrevious[1] = 0;
                g_fighterReplay.firstTick = stats.ticks + 1;
                g_fighterReplay.lastTick = stats.ticks + replayCount;
            }
            const bool replaying = replayIndex < replayCount;
            const unsigned held = replaying ? g_fighterReplay.buttons[replayIndex++] : pads[i].Buttons;
            std::array<FighterControls, 2> input{};
            if (replaying && twoControllers) {
                const unsigned controllers[]{held & 0xffff, held >> 16};
                for (unsigned player = 0; player < 2; ++player) {
                    input[player] = controlsFromPad(controllers[player], controllers[player] & ~replayPrevious[player]);
                    replayPrevious[player] = controllers[player];
                }
            } else {
                const unsigned pressed = held & ~previous;
                previous = held;
                if (pressed & PSP_CTRL_TRIANGLE) stats.active = 1 - stats.active;
                if (pressed & PSP_CTRL_RTRIGGER) {
                    const unsigned next = 1 - stats.map;
                    StageAsset replacement;
                    if (replacement.open(stages[next].c_str()) && replacement.spawn(0) && replacement.spawn(1)) {
                        stage = std::move(replacement);
                        stats.map = next;
                        placeActors();
                        sceKernelDcacheWritebackAll();
                    }
                }
                input[stats.active] = controlsFromPad(held, pressed);
            }
            const bool animate = ++stats.ticks % (LogicHz / AnimationHz) == 0;
            stats.animations += animate;
            match.step(stage.geometry(), input, animate);
            for (unsigned i = 0; i < 2; ++i) {
                const auto& actor = match.actor(i);
                const auto& motion = actor.motion();
                stats.actors[i] = {float(motion.position.x), float(motion.position.y),
                                   float(motion.velocity.x), float(motion.velocity.y), actor.facing(),
                                   actor.timelineFrame(), actor.phase(), motion.airborne,
                                   float(actor.health()), float(actor.qi()), float(actor.energy()),
                                   unsigned(actor.steel()) | (unsigned(actor.guarding()) << 1) | (unsigned(actor.dodging()) << 2)};
            }
            const SceneEffects scene = match.scene();
            stats.shakeY = scene.shiftY;
            stats.shineColor = scene.shineColor;
            stats.shineAlpha = scene.shineAlpha;
            const CameraView view = match.camera();
            stats.cameraX = float(view.origin.x);
            stats.cameraY = float(view.origin.y);
            stats.zoom = float(view.zoom);
            const auto objects = match.objects();
            stats.objectCount = std::min(objects.size(), std::size(stats.objects));
            for (unsigned k = 0; k < stats.objectCount; ++k) {
                const auto* projectile = std::get_if<FightProjectile>(&objects[k]);
                const unsigned flags = projectile ? unsigned(projectile->impacting()) : 2;
                std::visit([&](const auto& object) {
                    const auto p = object.position();
                    const auto v = object.velocity();
                    stats.objects[k] = {float(p.x), float(p.y), float(v.x), float(v.y), object.owner(), object.phase(), flags};
                }, objects[k]);
            }
            g_fighterTrace[(stats.ticks - 1) % TraceCapacity] = stats;
        }

        sprites.clear();
        for (unsigned i = 0; i < 2; ++i) {
            if (match.actor(1 - i).hidesTarget()) continue;
            const auto& actor = match.actor(i);
            sprites.push_back({actor.asset(), actor.pose(), actor.motion().position, actor.facing()});
        }
        for (const auto& object : match.objects()) {
            std::visit([&](const auto& member) {
                sprites.push_back({member.asset(), member.pose(), member.position(), member.facing()});
            }, object);
        }
        renderer.draw(stage, match.camera(), sprites, match.scene());
        ++stats.draws;
        g_fighterQa = stats;
    }
}
