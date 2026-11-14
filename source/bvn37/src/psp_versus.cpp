#include "resource_diagnostics.hpp"
#ifdef BVN_RESOURCE_DIAGNOSTICS
#include "resource_manifest.hpp"
#include <zlib.h>
#endif
#include <cerrno>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "arcade_run.hpp"
#include "fighter_cpu.hpp"
#include "game_settings.hpp"
#include "psp_audio.hpp"
#include "psp_stage_renderer.hpp"
#include "psp_allocation_recovery.hpp"
#include "roster_asset.hpp"
#include "versus_round.hpp"
#include "wlan_link.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <iterator>
#include "source_scene_qa.hpp"
#include "source_paint_qa.hpp"
#include <memory>
#include <string>

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspiofilemgr.h>
#include <zlib.h>

PSP_MODULE_INFO("Bleach vs Naruto 3.7", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);
#ifdef BVN_LIVE_RENDER_QA
extern "C" {
struct LiveRenderQA {unsigned status,error,scene,step,pairs,pixels,differingPixels,directBlack,queuedBlack,maxError,guardFrames,complexFrames,pillarPixels;} g_liveRenderQa;
extern volatile struct GeSubmissionQA {unsigned lists,maxBytes,submitErrors,deferredLists;} g_geSubmissionQa;
}
#endif

extern "C" {
bvn::SourceSceneQa g_sceneGameQa;
bvn::SourcePaintQa g_paintGameQa;
bvn::AnimationClockView g_animationClockQa;
volatile struct SourceControlQA {
    unsigned draws,ticks,running,renderHit,animations,renders,actorAnimations[2];
    float gravity[2];
} g_sourceControlQa;
volatile struct RoundControlQA {
    unsigned draws,ticks,clockFrame,mainFrames,actionEnabled,allowHp[2];
    int displayedSeconds;
    unsigned bannerFrame;
} g_roundControlQa;
static_assert(sizeof(RoundControlQA)==36);
volatile struct StartEndControlQA {
    unsigned draws,ticks,startKind;
    int startStep,startHold;
    unsigned uiPlaying,endActive;
    int endStep,endHold;
    unsigned transitionFrame,deferredPause,transitionRendering;
} g_startEndControlQa;
static_assert(sizeof(StartEndControlQA)==48);
extern volatile struct TransitionRendererQA {unsigned draws,frame,decodeFailures;}g_transitionRendererQa;
volatile struct RootControlQA {
    unsigned draws,ticks,roots,visible,frame,rendering,quick,quickStage;
    float quickUp,quickDown;
    unsigned overlayDraws,decodeFailures,residentBytes;
}g_rootControlQa;
static_assert(sizeof(RootControlQA)==52);
struct VersusQA {
    unsigned screen, draws, ticks, phase, phaseTicks, round;
    int seconds, winner;
    unsigned wins[2], fighters[2], map, arena;
    float x[2], y[2], hp[2], qi[2], energy[2], zoom;
    unsigned timeline[2], finished;
    int lastWinner;
    unsigned lastWins[2], lastRound;
    unsigned audio;
    unsigned forms[2], transforming[2], cutin;
    unsigned cells[2], alternate[2];
    unsigned training, historyRows, historyActions;
    unsigned movesTarget[2], hidesTarget[2];
    unsigned projectiles[2], attackerFrame[2];
    float attackerX[2], attackerY[2];
    unsigned assistCells[2], assistPicks[2], assistCharge[2], assistFrame[2];
    float assistX[2], assistY[2];
    unsigned assistCutin;
    unsigned teamMode, members[2], teamPicks[2][3], teamOrder[2][3], orderCursor[2], teamCount[2], orderCount[2];
    unsigned mode, arcadeStage, arcadeScore, arcadeFaction, arcadeFoes, arcadeContinue;
} g_versusQa;

struct WlanQA {
    unsigned status, frame, side, seed, option, ready[2], held[2], pressed[2];
    int error;
} g_wlanQa;

struct CreditsQA {
    unsigned pages, page, ready, entries;
} g_creditsQa;

struct CpuRuntimeQA {
    unsigned level, gameHz, decisions[2], intents[2];
    int states[2];
} g_cpuRuntimeQa;

struct RecoveryRuntimeQA {
    unsigned escaping[2];
    int damage[2];
    unsigned starts[2], hittable[2];
} g_recoveryRuntimeQa;

struct CounterRuntimeQA {
    unsigned kind[2], paused[2], starts[2], hitFrames[2], steelFrames[2], replaceFrames[2], gameFrame[2];
} g_counterRuntimeQa;

struct GhostRuntimeQA {
    unsigned kind[2], active[2];
    int frames[2];
    unsigned starts[2], ends[2], blue[2];
    int jumps[2];
    unsigned shadows;
} g_ghostRuntimeQa;

struct FrameEffectsRuntimeQA {
    unsigned shadow[2];
    int tint[2][3];
    unsigned stamps, footPuffs;
    int puffFacing;
    unsigned puffFrame, stepSounds[3], ghostFootSounds, negativeStamps;
} g_frameEffectsRuntimeQa;
struct HudGameQA {
    unsigned draws,visible,sampled;
    struct Side {float green,red,energy,qi,assist,hp;unsigned energyFrame,visible,ready,max,readyFrame,maxFrame;} side[2];
} g_hudGameQa;
static_assert(sizeof(HudGameQA)==108);
struct ComboGameQA {
    unsigned draws,hits[2],number[2],frame[2],shown[2],visible[2],targets[2];
} g_comboGameQa;
struct SettingsRuntimeQA {
    unsigned screen,row,profile,capture,serial,loaded,saveFailed;
    unsigned values[8],keys[2][10];
    unsigned level,hz,classic,rules;
    float maxHp[2];
    unsigned bulk,editing[10];
} g_settingsRuntimeQa;
volatile struct CombatGameQA {
    unsigned emitted,live,freezeFrames,sounds,lastEffect;
    float x,y;
    int facing;
    unsigned rotation,frame,draws;
} g_combatGameQa;
volatile struct StatusGameQA {
    unsigned draws,live;
    int tint[2][3];
    unsigned special,finished,clip,frame,cursor;
    float x,y;
} g_statusGameQa;
volatile struct BuffGameQA {
    unsigned draws,live,auras,bursts;
    float values[2][3];
    unsigned remaining[2][3],active[2][3];
    unsigned target,clip,frame,cursor;
    float x,y;
} g_buffGameQa;
volatile struct FloorGameQA {
    unsigned draws,live,mask;
    struct View {unsigned clip,frame;float x,y;int facing;} views[4];
} g_floorGameQa;
volatile struct KoGameQA {
    unsigned draws,super,backgrounds,hideMap,fillBackdrop,clip,frame,cursor;
    int hold;
    unsigned freeze,regularSound,superSound,overHit;
    float shiftX,shiftY,cameraX,cameraY,zoom;
} g_koGameQa;
extern volatile struct BlurRendererQA {unsigned layers,tiles,missing,culled;} g_blurRendererQa;
volatile struct BlurGameQA {
    unsigned draws;
    float x,y,slowRate;
    int frames,slowFrames;
    unsigned enabled,layers,tiles,missing,culled,reserved;
} g_blurGameQa;
extern volatile struct OpticalRendererQA {unsigned layers,tiles,missing,cached,normal;} g_opticalRendererQa;
volatile struct OpticalGameQA {
    unsigned draws,mask,counter,cachedBits;
    unsigned cachedMasks[5];
    unsigned captures,children,layers,tiles,missing,cachedDraws,normalDraws;
    struct Area {float x,y,width,height;} actors[2];
} g_opticalGameQa;
}
static_assert(sizeof(VersusQA) == 388);
static_assert(sizeof(SourceControlQA)==40);
static_assert(sizeof(WlanQA) == 48);
static_assert(sizeof(FrameEffectsRuntimeQA) == 68);
static_assert(sizeof(SettingsRuntimeQA)==208);
static_assert(sizeof(BuffGameQA)==112);
static_assert(sizeof(FloorGameQA)==92);
static_assert(sizeof(KoGameQA)==72);
static_assert(sizeof(BlurGameQA)==48);
static_assert(sizeof(OpticalGameQA)==96);

namespace bvn {
namespace {

enum class Screen : unsigned {
    Title, FirstFighter, SecondFighter, Stage, Fight, Pause, LoadError, FirstAssist, SecondAssist, FirstOrder, SecondOrder,
    ArcadeWinner, ArcadeContinue, ArcadeOver, ArcadeClear, ArcadeMissing,
    WlanMenu = 17, WlanRoom, Credits, Settings, Keys
};

constexpr std::uint32_t Panel = 0xe51d1224, Accent = 0xffe0bb48, Rival = 0xffc14afe;
constexpr unsigned ConfirmButtons = PSP_CTRL_CROSS | PSP_CTRL_START;
constexpr std::array TitleModes{BattleMode::Versus, BattleMode::Training, BattleMode::Arcade,
                                BattleMode::TeamArcade, BattleMode::TeamVersus, BattleMode::Wlan,
                                BattleMode::Credits, BattleMode::Settings};

std::span<const BattleMode> titleModes() {
    return TitleModes;
}

unsigned titleIndex(std::span<const BattleMode> modes, BattleMode mode) {
    return unsigned(std::ranges::find(modes, mode) - modes.begin());
}

struct FighterPick { unsigned cell = 0, alternate = 0; };

struct MapChoice {
    std::string file, preview, title;
    bool random;
};

unsigned changeChoice(unsigned value, unsigned size, unsigned pressed) {
    if (pressed & PSP_CTRL_LEFT) return (value + size - 1) % size;
    if (pressed & PSP_CTRL_RIGHT) return (value + 1) % size;
    return value;
}

float gauge(double value, double maximum, float width) {
    return float(std::clamp(value / maximum, 0.0, 1.0) * width);
}

std::optional<std::uint32_t> catalogChecksum(const std::string& folder) {
    std::array<unsigned char, 1024> buffer;
    std::uint32_t checksum = 0;
    for (const char* name : {"ROSTER.BVR", "ASSISTS.BVR", "STAGES.TXT", "TRANS.BVT", "HUD.BVU", "COMBO.BVU"}) {
        const std::unique_ptr<FILE, decltype(&std::fclose)> file(
            std::fopen((folder + name).c_str(), "rb"), std::fclose);
        if (!file) return {};
        while (const auto count = std::fread(buffer.data(), 1, buffer.size(), file.get())) {
            checksum = crc32(checksum, buffer.data(), count);
        }
        if (std::ferror(file.get())) return {};
    }
    return checksum;
}

class Game {
public:
    explicit Game(std::string folder) : folder_(std::move(folder)) {}
    bool start();
    void step(unsigned held, unsigned pressed);
    void draw();
    void deferred(bool value) { renderer_.deferred(value); }
    bool deferred() const { return renderer_.deferred(); }
#ifdef BVN_LIVE_RENDER_QA
    bool liveRenderProbe(const std::string& reportFolder);
#endif

private:
    void stepGame(unsigned held, unsigned pressed, WlanPad second = {});
    void stepWlanMenu(unsigned pressed);
    void stepWlan(unsigned held, unsigned pressed);
    void stepWlanPicks(bool assists, const std::array<WlanPad, 2>& inputs);
    void stepWlanOrder(const std::array<WlanPad, 2>& inputs);
    void resetWlanPicks();
    void leaveWlan(Screen next);
    unsigned decisionSeed() const;
    void beginFight();
    void prepareArcadeFight();
    void finishArcade();
    void stepArcadeResult(bool confirm, bool back, unsigned pressed);
    void nextTeamMembers();
    bool loadForms(unsigned slot);
    void battleMusic(unsigned side, unsigned seed);
    void clearBattle();
    void showLoadError();
#ifdef BVN_RESOURCE_DIAGNOSTICS
    void recordResourceDiagnostics();
    unsigned loadStartSerial_ = 0, loggedResourceSerial_ = 0, diagnosticEntries_ = 0;
#endif
    void leaveFight();
    std::optional<unsigned> teamChoice(unsigned side, FighterPick pick, unsigned seed) const;
    std::optional<unsigned> stepPick(unsigned side, bool assists, unsigned pressed);
    void stepOrder(unsigned pressed);
    void stepOrderChoice(unsigned side, unsigned pressed);
    bool ordered(unsigned side, unsigned index) const;
    void drawTitle();
    void openCredits();
    void drawCredits();
    void openSettings();
    void stepSettings(unsigned held,unsigned pressed);
    void drawSettings();
    void settingsText(const char* key,float x,float y,bool left=false);
    void applySettings();
    FighterControls humanControls(unsigned side,unsigned held,unsigned pressed);
    GameSettings battleSettings()const;
    void drawWlanMenu();
    const char* wlanSide() const { return wlan_.host() ? "label-wlan-p1" : "label-wlan-p2"; }
    void drawFighters();
    void drawSelectedFighter(unsigned side, bool assists);
    void drawMap();
    void drawOrder();
    void drawArcadeResult();
    void drawScore(float x, float y);
    void drawFight();
    void drawHud(const VersusRound& round);
    void drawTrainingHistory(const VersusRound& round);
    void drawBanner(const VersusRound& round);
    void roundAudio(RoundPhase previous, std::optional<unsigned> previousBanner);
    void record();
    void image(const char* key, float x, float y, float w = 0, float h = 0, bool flip = false) {
        renderer_.image(ui_, key, x, y, w, h, flip);
    }
    void centered(const char* key, float x, float y, float width = 0, float height = 0);
    unsigned selected(unsigned side, bool assists = false) const {
        const auto pick = assists ? assistPicks_[side] : picks_[side];
        const auto& roster = assists ? assistRoster_ : roster_;
        return roster.cells()[pick.cell].choice(pick.alternate);
    }

    std::string folder_;
    UiAsset ui_;
    UiAsset hudArt_;
    UiAsset comboArt_;
    TransitionAsset transitionArt_;
    UiAsset arcadeUi_;
    UiAsset creditsUi_;
    UiAsset settingsUi_;
    GameSettings settings_;
    SettingsStore settingsStore_;
    std::string settingsFolder_;
    bool settingsLoaded_=false,settingsSaveFailed_=false,keyCapture_=false,keyCaptureArmed_=false,keyBulk_=false;
    unsigned settingsRow_=0,keyProfile_=0,keyRow_=0;
    KeyBindings editingKeys_;
    std::array<HumanInput,2> humanInputs_;
    FighterAsset reactionArt_;
    FighterAsset combatArt_;
    FighterAsset mapBlurArt_;
    FighterAsset mapOpticalArt_;
    unsigned creditsPage_ = 0, creditsPages_ = 0, creditsEntries_ = 0;
    RosterAsset roster_, assistRoster_;
    std::vector<MapChoice> maps_;
    ArcadeRun arcade_;
    const ArcadeStage* arcadeEncounter_ = nullptr;
    unsigned arcadeFighter_ = 0, arcadeAssist_ = 0, arcadeFaction_ = 2, arcadePortrait_ = 0;
    bool firstTeamMember_ = false, continueArcade_ = true;
    Screen screen_ = Screen::Title;
    BattleMode mode_ = BattleMode::Versus;
    FighterPick picks_[2], assistPicks_[2];
    unsigned active_[2]{}, activeAssists_[2]{};
    std::array<unsigned, TeamSize> teamPicks_[2]{}, teamOrder_[2]{};
    unsigned teamCount_[2]{}, orderCount_[2]{}, orderCursor_[2]{};
    unsigned map_ = 0, arena_ = 0;
    StageAsset stage_;
    std::shared_ptr<std::vector<FighterAsset>> assets_[2];
    FighterAsset assistAssets_[2];
    std::vector<unsigned> formIds_[2];
    std::optional<VersusRound> round_;
    FighterCpu cpu_, watchCpu_;
    unsigned cpuLevel_ = 1, aiGameHz_ = 30;
    PspAudio audio_;
    StageRenderer renderer_;
    std::vector<FighterView> sprites_;
    VersusQA stats_{};
    WlanSession wlan_;
    std::uint32_t catalog_ = 0;
    unsigned wlanOption_ = 0, defaultMap_ = 0, wlanPressed_ = 0;
    std::array<bool, 2> wlanReady_{};
    std::array<WlanPad, 2> wlanPads_{};
};

bool Game::start() {
    if (!installGameAllocationRecovery(renderer_)) return false;
#ifdef BVN_RESOURCE_DIAGNOSTICS
    if (auto* log = std::fopen((folder_ + "BVN_DIAG.LOG").c_str(), "w")) {
        std::fprintf(log, "BVN37 1.0.5 HWD3 HD QUEUED-GE\nreader=%s\nfirmware=0x%08x\n",
                     resourceDiagnostics.synchronousOnly ? "stdio-only" : "original-readahead", sceKernelDevkitVersion());
        std::fclose(log);
    }
#endif
    const char* drive=folder_.starts_with("ef0:")?"ef0:":"ms0:";
    settingsFolder_=std::string(drive)+"/PSP/SAVEDATA/BVN37C001";
    settingsLoaded_=settingsStore_.load(settingsFolder_.c_str(),settings_);
    if(!settingsUi_.open((folder_+"SETTINGS.BVU").c_str()))return false;
    if(!hudArt_.open((folder_+"HUD.BVU").c_str()))return false;
    if(!comboArt_.open((folder_+"COMBO.BVU").c_str()))return false;
    if(!transitionArt_.open((folder_+"TRANS.BVT").c_str()))return false;
    if (!ui_.open((folder_ + "UI.BVU").c_str()) || !roster_.open((folder_ + "ROSTER.BVR").c_str()) ||
        !assistRoster_.open((folder_ + "ASSISTS.BVR").c_str(), true)) return false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto cell = roster_.firstCell(i);
        if (!cell || !roster_.resolve(i, 0)) return false;
        picks_[i].cell = *cell;
        const auto assist = assistRoster_.firstCell(i);
        if (!assist || !assistRoster_.resolve(i, 0)) return false;
        assistPicks_[i].cell = *assist;
    }
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(
        std::fopen((folder_ + "STAGES.TXT").c_str(), "r"), std::fclose);
    if (!file) return false;
    char line[32]{};
    while (std::fgets(line, sizeof(line), file.get())) {
        std::string name(line);
        if (!name.ends_with('\n')) return false;
        name.pop_back();
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](char c) { return c >= 'A' && c <= 'Z'; })) return false;
        std::string id = name;
        std::transform(id.begin(), id.end(), id.begin(), [](char c) { return char(std::tolower(c)); });
        maps_.push_back({name + ".BVN", "map-" + id, "name-" + id, id == "random"});
        if (id == "jianchang") map_ = maps_.size() - 1;
        if (maps_.size() > 64) return false;
    }
    if (std::none_of(maps_.begin(), maps_.end(), [](const MapChoice& map) { return !map.random; })) return false;
    const auto catalog = catalogChecksum(folder_);
    if (!catalog) return false;
    catalog_ = *catalog;
    defaultMap_ = map_;
    if (!arcade_.open((folder_ + "ARCADE.BVM").c_str(), roster_, assistRoster_, maps_.size())) return false;
    if (!reactionArt_.open((folder_ + "REACTION.BVF").c_str(), FighterAsset::TextureResidency::LowMemory)) return false;
    constexpr unsigned reactionFrames[]{34,17,15,14,16};
    for (unsigned cue=0;cue<std::size(reactionFrames);++cue)
        if (!reactionArt_.has(MoveClip(9+cue)) || reactionArt_.clip(MoveClip(9+cue)).count!=reactionFrames[cue]) return false;
    if(!combatArt_.open((folder_+"COMBAT.BVF").c_str(),FighterAsset::TextureResidency::LowMemory))return false;
    for(const auto& item:CombatArtData)
        if(!combatArt_.has(MoveClip(item.clip))||combatArt_.clip(MoveClip(item.clip)).count!=item.frames)return false;
    sprites_.reserve(FightMatch::ObjectLimit + 2);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    stats_.audio = audio_.open((folder_ + "AUDIO.BVA").c_str());
    applySettings();
    audio_.music("op");
    renderer_.start(true);
    sceKernelDcacheWritebackAll();
    return true;
}

unsigned Game::decisionSeed() const {
    if (!wlan_.active()) return sceKernelGetSystemTimeLow();
    // A different repeatable choice is available at each shared selection step.
    unsigned seed = wlan_.seed() ^ (wlan_.frame() * 0x9e3779b9U);
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

void Game::beginFight() {
#ifdef BVN_RESOURCE_DIAGNOSTICS
    loadStartSerial_ = resourceDiagnostics.failures;
#endif
    const auto config=battleSettings();
    const auto* encounter = isArcadeMode(mode_) ? arcadeEncounter_ : nullptr;
    arena_ = encounter ? encounter->map : map_;
    const unsigned seed = decisionSeed();
    if (maps_[arena_].random) {
        std::array<unsigned, 64> choices{};
        unsigned count = 0;
        for (unsigned i = 0; i < maps_.size(); ++i) if (!maps_[i].random) choices[count++] = i;
        arena_ = choices[seed % count];
    }
    TeamFactions factions{};
    RoundRules rules;
    rules.maxHealth=FighterMaxHealth*config.healthScale();
    rules.classicScore=config.classic(wlan_.active());
    rules.sourceQuality=true;rules.gameHz=config.gameHz(wlan_.active());rules.shineHz=config.shineHz();
    arcade_.setClassicScore(rules.classicScore);
    const bool team = isTeamMode(mode_);
    if (encounter) {
        rules.members[1] = team ? encounter->count : 1;
        rules.maxHealth *= encounter->healthPercent / 100.0;
        rules.enemyAttackRate = encounter->enemyAttackPercent / 100.0;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const auto chosen = team || (encounter && i) ? std::optional(teamOrder_[i][0])
            : encounter ? std::optional(arcadeFighter_) : roster_.resolve(selected(i), i ? seed >> 8 : seed);
        const auto assist = encounter ? std::optional(i ? unsigned(encounter->assister) : arcadeAssist_)
            : assistRoster_.resolve(selected(i, true), i ? seed >> 4 : seed >> 12);
        if (!chosen || !assist) {
            showLoadError();
            return;
        }
        active_[i] = *chosen;
        activeAssists_[i] = *assist;
        formIds_[i] = roster_.forms(*chosen);
        if (team) {
            for (unsigned slot = 0; slot < rules.members[i]; ++slot) {
                factions[i][slot] = roster_.fighter(teamOrder_[i][slot]).faction;
            }
        } else {
            factions[i][0] = roster_.fighter(*chosen).faction;
        }
    }
    const auto& first = roster_.fighter(active_[0]);
    const auto& second = roster_.fighter(active_[1]);
    const bool sharedAssist = activeAssists_[0] == activeAssists_[1];
    std::array<AssisterChoice, 2> assisters;
    bool assistsReady = true;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& pick = assistRoster_.fighter(activeAssists_[i]);
        auto& asset = assistAssets_[sharedAssist ? 0 : i];
        if (!i || !sharedAssist) {
            assistsReady &= asset.open((folder_ + pick.file).c_str(),FighterAsset::TextureResidency::LowMemory) && asset.assister() && asset.hasObjectAreas();
        }
        assisters[i] = {&asset, pick.faction};
    }
    if (!assistsReady || !stage_.open((folder_ + maps_[arena_].file).c_str(),true) ||
        !mapBlurArt_.open((folder_+maps_[arena_].file.substr(0,maps_[arena_].file.size()-4)+".BVB").c_str(),FighterAsset::TextureResidency::LowMemory)) {
        showLoadError();
        return;
    }
    mapOpticalArt_={};
    if(!stage_.optical().children().empty()){
        const auto file=folder_+maps_[arena_].file.substr(0,maps_[arena_].file.size()-4)+".BVO";
        if(!mapOpticalArt_.open(file.c_str(),FighterAsset::TextureResidency::LowMemory)||
           !mapOpticalArt_.has(MoveClip(9))||mapOpticalArt_.clip(MoveClip(9)).count!=stage_.optical().paletteFrames()){
            showLoadError();return;
        }
    }
    stage_.useOpticalPalette();
    if(!loadForms(0)||!loadForms(1)){showLoadError();return;}
    for(auto& input:humanInputs_)input.clear();
    cpuLevel_=wlan_.active()?1:settings_.difficulty;aiGameHz_=config.gameHz(wlan_.active());
    round_.emplace(stage_, *assets_[0], *assets_[1], config.seconds(),
                   std::array<unsigned, 2>{first.form, second.form}, mode_, assisters, factions, rules);
    round_->seedObjects(seed);
    round_->setGameHz(aiGameHz_);
    cpu_ = FighterCpu(seed, cpuLevel_, aiGameHz_);
    watchCpu_ = FighterCpu(seed ^ 0x85ebca6bU, cpuLevel_, aiGameHz_);
    // Connected picks use Versus/TeamVersus modes; connection state owns P2.
    if (!round_->training() && !wlan_.active()) round_->bindCpu(1, &cpu_);
    if (isWatchMode(mode_)) round_->bindCpu(0, &watchCpu_);
    battleMusic(1, seed);
    sceKernelDcacheWritebackAll();
    screen_ = Screen::Fight;
}

void Game::prepareArcadeFight() {
    const bool team = isTeamMode(mode_);
    arcadeEncounter_ = &arcade_.stage(arcadeFaction_, team);
    const auto& stage = *arcadeEncounter_;
    const auto foes = std::span(stage.opponents).first(stage.count);
    if (!assistRoster_.fighter(stage.assister).playable() || maps_[stage.map].random ||
        !std::ranges::all_of(foes, [&](unsigned id) { return roster_.fighter(id).playable(); })) {
        audio_.music("select");
        screen_ = Screen::ArcadeMissing;
        return;
    }
    teamOrder_[1] = stage.lineup(sceKernelGetSystemTimeLow(), team);
    if (!team) {
        beginFight();
        return;
    }
    std::copy(stage.opponents.begin(), stage.opponents.end(), teamPicks_[1].begin());
    teamCount_[1] = orderCount_[1] = stage.count;
    orderCount_[0] = orderCursor_[0] = 0;
    audio_.music("loading");
    screen_ = Screen::FirstOrder;
}

void Game::finishArcade() {
    const auto& player = round_->match().actor(0);
    arcadePortrait_ = formIds_[0][player.form()];
    firstTeamMember_ = round_->team() && round_->member(0) == 0;
    const bool won = round_->winner() == 0;
    clearBattle();
    if (!arcadeUi_.open((folder_ + "ARCADE.BVU").c_str())) {
        showLoadError();
        return;
    }
    sceKernelDcacheWritebackAll();
    continueArcade_ = true;
    screen_ = !won ? Screen::ArcadeContinue : arcade_.lastStage() ? Screen::ArcadeClear : Screen::ArcadeWinner;
    audio_.music(!won ? "continue" : arcade_.lastStage() ? "win" : "winloop");
}

void Game::stepArcadeResult(bool confirm, bool back, unsigned pressed) {
    if (screen_ == Screen::ArcadeContinue) {
        if (pressed & PSP_CTRL_LEFT) continueArcade_ = true;
        if (pressed & PSP_CTRL_RIGHT) continueArcade_ = false;
        if (!(confirm || back)) return;
        audio_.play(Sound::Confirm);
        if (confirm && continueArcade_) {
            arcade_.retry();
            arcadeUi_ = {};
            leaveFight();
        } else {
            audio_.music("");
            screen_ = Screen::ArcadeOver;
        }
        return;
    }
    if (!(confirm || back)) return;
    arcadeUi_ = {};
    if (screen_ == Screen::ArcadeWinner && confirm) {
        arcade_.advance(firstTeamMember_);
        prepareArcadeFight();
    } else {
        teamCount_[0] = teamCount_[1] = 0;
        orderCount_[0] = orderCount_[1] = 0;
        screen_ = Screen::Title;
        audio_.music("op");
    }
}

bool Game::loadForms(unsigned slot) {
    const auto& forms = formIds_[slot];
    const unsigned other = 1 - slot;
    if (assets_[other] && forms == formIds_[other]) {
        assets_[slot] = assets_[other];
        return true;
    }
    auto assets = std::make_shared<std::vector<FighterAsset>>(forms.size());
    for (std::size_t i = 0; i < forms.size(); ++i) {
        // Selected forms need the same bounded texture residency as inactive
        // forms. Keeping a large opening fighter bank resident can exhaust the
        // 32 MB heap while the rest of its family is still being loaded. The
        // existing stream and decoded-pose cache retain every source texture.
        if (forms[i] != MissingFighter &&
            (!(*assets)[i].open((folder_ + roster_.fighter(forms[i]).file).c_str(),FighterAsset::TextureResidency::LowMemory) || (*assets)[i].assister() || !(*assets)[i].hasObjectAreas())) return false;
    }
    assets_[slot] = std::move(assets);
    return true;
}

void Game::battleMusic(unsigned side, unsigned seed) {
    const auto& fighter = roster_.fighter(active_[side]);
    const bool theme = !round_->training() && (seed / 64) % 100 < fighter.musicChance;
    audio_.music(theme ? fighter.music : maps_[arena_].preview.c_str());
}

void Game::nextTeamMembers() {
    renderer_.clearFighterTextures();
    std::array<TeamEntrant, 2> entries;
    unsigned introSide = 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (const auto next = round_->entering(i)) {
            active_[i] = teamOrder_[i][*next];
            formIds_[i] = roster_.forms(active_[i]);
            assets_[i].reset();
            introSide = i;
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!round_->entering(i)) continue;
        if (!loadForms(i)) {
            showLoadError();
            return;
        }
        entries[i] = {*assets_[i], roster_.fighter(active_[i]).form};
    }
    if (!round_->enterTeamMembers(entries)) {
        showLoadError();
        return;
    }
    round_->setGameHz(aiGameHz_);
    const auto seed = decisionSeed();
    if (!entries[1].forms.empty()) cpu_ = FighterCpu(seed, cpuLevel_, aiGameHz_);
    if (!entries[0].forms.empty() && isWatchMode(mode_)) watchCpu_ = FighterCpu(seed ^ 0x85ebca6bU, cpuLevel_, aiGameHz_);
    battleMusic(introSide, seed);
    sceKernelDcacheWritebackAll();
}

void Game::clearBattle() {
    for (unsigned side = 0; side < 2; ++side) audio_.dashSound(side, false);
    round_.reset();
    renderer_.clearFighterTextures();
    stage_ = {};
    mapBlurArt_ = {};
    mapOpticalArt_ = {};
    for (auto& asset : assets_) asset = {};
    for (auto& assist : assistAssets_) assist = {};
}


#ifdef BVN_RESOURCE_DIAGNOSTICS
void Game::recordResourceDiagnostics() {
    const auto& failure = resourceDiagnostics;
    if (!failure.failures || failure.failures == loggedResourceSerial_ || diagnosticEntries_ >= 16) return;
    loggedResourceSerial_ = failure.failures;
    ++diagnosticEntries_;
    if (auto* log = std::fopen((folder_ + "BVN_DIAG.LOG").c_str(), "a")) {
        SceIoStat stat{};
        const int statResult = sceIoGetstat(failure.failed, &stat);
        std::fprintf(log, "\nERROR %u tick=%llu operation=%s errno=%d\nfile=%s\nstat=0x%08x file_bytes=%lld texture=%u request_bytes=%u\nheap_free_at_failure=%u partition_free_at_failure=%u largest_partition_at_failure=%u\n",
                     failure.failures, sceKernelGetSystemTimeWide(), failure.operation, failure.error,
                     failure.failed, unsigned(statResult), static_cast<long long>(stat.st_size),
                     failure.texture, failure.bytes, failure.heapFree, failure.partitionFree, failure.largestPartition);
        const auto& context = failure.failedLoad;
        std::fprintf(log, "load_step=%s item=%u opened_bytes=%ld last_read_expected=%u last_read_actual=%u last_read_position=%ld stream_error=%d eof=%d metadata=%016llx\n",
                     context.step, context.index, context.fileLength, context.expected, context.actual,
                     context.position, context.streamError, context.eof, context.metadata);
        std::fprintf(log, "integrity=%d expected_bytes=%u verified_bytes=%u expected_crc=%08x actual_crc=%08x verify_errno=%d\n",
                     failure.integrity, failure.expectedFileBytes, failure.verifiedBytes,
                     failure.expectedCrc, failure.actualCrc, failure.verifyErrno);
        for (unsigned side = 0; side < 2; ++side) {
            std::fprintf(log, "side%u fighter=%s assist=%s\n", side,
                         roster_.fighter(active_[side]).file, assistRoster_.fighter(activeAssists_[side]).file);
        }
        if (arena_ < maps_.size()) std::fprintf(log, "map=%s\n", maps_[arena_].file.c_str());
        std::fclose(log);
    }
}
#endif

#ifdef BVN_RESOURCE_DIAGNOSTICS
// Run only after battle assets/read-ahead descriptors have been released, so
// the integrity probe cannot overlap their queued reads or change failure data.
void verifyFailedResource() {
    auto& failure = resourceDiagnostics;
    const char* name = diagnosticFileName();
    const auto found = std::ranges::find_if(ResourceFileIdentities, [&](const auto& item) {
        return std::strcmp(name, item.name) == 0;
    });
    if (found == ResourceFileIdentities.end()) return;
    failure.expectedFileBytes = found->bytes; failure.expectedCrc = found->crc;
    errno = 0;
    auto* file = std::fopen(failure.failed, "rb");
    if (!file) { failure.integrity = -1; failure.verifyErrno = errno; return; }
    std::array<unsigned char, 4096> buffer{};
    uLong crc = crc32(0, Z_NULL, 0);
    while (const auto count = std::fread(buffer.data(), 1, buffer.size(), file)) {
        crc = crc32(crc, buffer.data(), count);
        failure.verifiedBytes += count;
    }
    const bool readError = std::ferror(file);
    failure.verifyErrno = errno; failure.actualCrc = crc;
    failure.integrity = !readError && failure.verifiedBytes == found->bytes && crc == found->crc ? 1 : 2;
    std::fclose(file);
}
#endif

void Game::showLoadError() {
#ifdef BVN_RESOURCE_DIAGNOSTICS
    if (resourceDiagnostics.failures == loadStartSerial_)
        diagnosticFailure(resourceDiagnostics.attempted, "ASSET BIND", errno);
#endif
    if (wlan_.active()) {
        wlan_.close();
        leaveWlan(Screen::LoadError);
        return;
    }
    clearBattle();
#ifdef BVN_RESOURCE_DIAGNOSTICS
    verifyFailedResource();
    recordResourceDiagnostics();
#endif
    audio_.music("select");
    screen_ = Screen::LoadError;
}

void Game::leaveFight() {
    clearBattle();
    teamCount_[0] = teamCount_[1] = 0;
    orderCount_[0] = orderCount_[1] = 0;
    wlanReady_.fill(false);
    audio_.music("select");
    screen_ = Screen::FirstFighter;
}

std::optional<unsigned> Game::teamChoice(unsigned side, FighterPick pick, unsigned seed) const {
    const auto chosen = std::span(teamPicks_[side]).first(teamCount_[side]);
    const auto& cell = roster_.cells()[pick.cell];
    if (!roster_.fighter(cell.fighter).random) {
        for (unsigned i = 0; i <= cell.moreCount; ++i) {
            if (std::ranges::find(chosen, cell.choice(i)) != chosen.end()) return {};
        }
    }
    return roster_.resolve(cell.choice(pick.alternate), seed, chosen);
}

bool Game::ordered(unsigned side, unsigned index) const {
    const auto chosen = std::span(teamOrder_[side]).first(orderCount_[side]);
    return std::ranges::find(chosen, teamPicks_[side][index]) != chosen.end();
}

std::optional<unsigned> Game::stepPick(unsigned side, bool assists, unsigned pressed) {
    const bool team = isTeamMode(mode_) && !assists;
    auto& pick = assists ? assistPicks_[side] : picks_[side];
    const auto& roster = assists ? assistRoster_ : roster_;
    const int horizontal = int(bool(pressed & PSP_CTRL_RIGHT)) - int(bool(pressed & PSP_CTRL_LEFT));
    const int vertical = int(bool(pressed & PSP_CTRL_DOWN)) - int(bool(pressed & PSP_CTRL_UP));
    const auto moved = roster.move(pick.cell, horizontal, vertical);
    if (moved != pick.cell) {
        pick = {moved, roster.initialAlternate(moved)};
        audio_.play(Sound::Select);
    }
    if ((pressed & PSP_CTRL_TRIANGLE) && roster.cells()[pick.cell].moreCount) {
        pick.alternate = (pick.alternate + 1) % (roster.cells()[pick.cell].moreCount + 1);
        audio_.play(Sound::Select);
    }
    if (!(pressed & ConfirmButtons)) return {};
    auto seed = isArcadeMode(mode_) || team ? decisionSeed() : 0U;
    if (team && side && wlan_.active()) seed ^= 0x85ebca6bU;
    const auto chosen = team ? teamChoice(side, pick, seed) : roster.resolve(selected(side, assists), seed);
    if (!chosen) return {};
    audio_.play(Sound::Confirm);
    if (team) {
        auto& count = teamCount_[side];
        teamPicks_[side][count++] = *chosen;
        if (count < TeamSize) {
            if (!roster_.fighter(selected(side)).random) {
                const auto next = roster_.move(pick.cell, side ? -1 : 1, 0);
                pick = {next, roster_.initialAlternate(next)};
            }
            return {};
        }
    }
    return chosen;
}

void Game::stepOrder(unsigned pressed) {
    const unsigned side = screen_ == Screen::SecondOrder;
    if ((pressed & PSP_CTRL_CIRCLE) && !orderCount_[side]) {
        audio_.play(Sound::Confirm);
        if (side) {
            --orderCount_[0];
            screen_ = Screen::FirstOrder;
        } else {
            screen_ = isArcadeMode(mode_) ? Screen::FirstAssist : Screen::Stage;
            audio_.music("select");
        }
        return;
    }
    stepOrderChoice(side, pressed);
    if (orderCount_[side] != TeamSize) return;
    if (side || isArcadeMode(mode_)) beginFight();
    else screen_ = Screen::SecondOrder;
}

void Game::stepOrderChoice(unsigned side, unsigned pressed) {
    auto& cursor = orderCursor_[side];
    auto& count = orderCount_[side];
    if (pressed & PSP_CTRL_CIRCLE) {
        if (count) {
            audio_.play(Sound::Confirm);
            const auto previous = std::ranges::find(teamPicks_[side], teamOrder_[side][--count]);
            cursor = unsigned(previous - teamPicks_[side].begin());
        }
        return;
    }
    if (count == TeamSize) return;
    const int vertical = int(bool(pressed & PSP_CTRL_DOWN)) - int(bool(pressed & PSP_CTRL_UP));
    if (vertical) {
        for (int i = 1; i < int(TeamSize); ++i) {
            const unsigned next = (int(cursor) + i * vertical + int(TeamSize)) % TeamSize;
            if (!ordered(side, next)) {
                cursor = next;
                audio_.play(Sound::Select);
                break;
            }
        }
    }
    if (!(pressed & ConfirmButtons) || ordered(side, cursor)) return;
    teamOrder_[side][count++] = teamPicks_[side][cursor];
    audio_.play(Sound::Confirm);
    if (count < TeamSize) {
        for (unsigned i = 1; i < TeamSize; ++i) {
            const unsigned next = (cursor + i) % TeamSize;
            if (!ordered(side, next)) {
                cursor = next;
                break;
            }
        }
    }
}

void Game::resetWlanPicks() {
    leaveFight();
    map_ = defaultMap_;
    wlanPressed_ = 0;
    wlanPads_ = {};
    for (unsigned side = 0; side < 2; ++side) {
        picks_[side] = {*roster_.firstCell(side), 0};
        assistPicks_[side] = {*assistRoster_.firstCell(side), 0};
    }
}

void Game::leaveWlan(Screen next) {
    leaveFight();
    if(!wlan_.active())applySettings();
    wlanPressed_ = 0;
    wlanPads_ = {};
    mode_ = BattleMode::Wlan;
    screen_ = next;
}

void Game::stepWlanMenu(unsigned pressed) {
    const bool confirm = pressed & ConfirmButtons;
    const bool back = pressed & PSP_CTRL_CIRCLE;
    if (screen_ == Screen::WlanRoom) {
        if (back || (confirm && !wlan_.busy())) {
            wlan_.close();
            audio_.play(Sound::Confirm);
            screen_ = Screen::WlanMenu;
            return;
        }
        wlan_.pump(sceKernelGetSystemTimeWide());
        if (wlan_.active()) {
            mode_ = wlan_.team() ? BattleMode::TeamVersus : BattleMode::Versus;
            resetWlanPicks();
        }
        return;
    }
    if (back) {
        audio_.play(Sound::Confirm);
        audio_.music("op");
        screen_ = Screen::Title;
        return;
    }
    if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) {
        wlanOption_ = (wlanOption_ + (pressed & PSP_CTRL_DOWN ? 1 : 2)) % 3;
        audio_.play(Sound::Select);
    }
    if (confirm) {
        audio_.play(Sound::Confirm);
        const auto now = sceKernelGetSystemTimeWide();
        wlan_.start(wlanOption_ != 2, wlanOption_ == 1, static_cast<std::uint32_t>(now), catalog_, now,settings_.wireRules());
        screen_ = Screen::WlanRoom;
    }
}

void Game::stepWlanPicks(bool assists, const std::array<WlanPad, 2>& inputs) {
    const bool team = isTeamMode(mode_);
    for (unsigned side = 0; side < 2; ++side) {
        const auto pressed = inputs[side].menuPressed;
        if (pressed & PSP_CTRL_CIRCLE) {
            audio_.play(Sound::Confirm);
            if (wlanReady_[side]) {
                wlanReady_[side] = false;
                if (team && !assists) --teamCount_[side];
            } else if (assists) {
                if (team) for (auto& count : teamCount_) --count;
                wlanReady_.fill(false);
                screen_ = Screen::FirstFighter;
                return;
            } else if (team && teamCount_[side]) {
                --teamCount_[side];
            }
        } else if (!wlanReady_[side]) {
            wlanReady_[side] = stepPick(side, assists, pressed).has_value();
        }
    }
    if (wlanReady_[0] && wlanReady_[1]) {
        wlanReady_.fill(false);
        screen_ = assists ? Screen::Stage : Screen::FirstAssist;
    }
}

void Game::stepWlanOrder(const std::array<WlanPad, 2>& inputs) {
    if ((inputs[0].menuPressed & PSP_CTRL_CIRCLE) && !orderCount_[0]) {
        audio_.play(Sound::Confirm);
        audio_.music("select");
        screen_ = Screen::Stage;
        return;
    }
    for (unsigned side = 0; side < 2; ++side) stepOrderChoice(side, inputs[side].menuPressed);
    if (orderCount_[0] == TeamSize && orderCount_[1] == TeamSize) beginFight();
}

void Game::stepWlan(unsigned held, unsigned pressed) {
    if (pressed & PSP_CTRL_SELECT) {
        wlan_.close();
        leaveWlan(Screen::WlanMenu);
        return;
    }
    const auto now = sceKernelGetSystemTimeWide();
    wlan_.pump(now);
    if (wlan_.active()) {
        constexpr unsigned horizontal = PSP_CTRL_LEFT | PSP_CTRL_RIGHT;
        constexpr unsigned vertical = PSP_CTRL_UP | PSP_CTRL_DOWN;
        // Short direction taps and action presses must survive a full send slot.
        for (const unsigned directions : {horizontal, vertical}) {
            if (pressed & directions) wlanPressed_ &= ~directions;
            if (!(held & directions)) held |= (wlanPressed_ | pressed) & directions;
        }
        wlanPressed_ |= pressed;
        const auto& keys=settings_.keys[wlan_.host()?0:1];
        const auto canonical=[&](unsigned raw){
            const auto logical=keys.logical(raw);unsigned value=raw&PSP_CTRL_START;
            for(unsigned i=0;i<HumanKeyCount;++i)if(logical&(1U<<i))value|=DefaultButtons[i];
            return value;
        };
        if (wlan_.submit({canonical(held),canonical(wlanPressed_),held,wlanPressed_})) wlanPressed_ = 0;
        wlan_.pump(now);
    }
    if (!wlan_.active()) {
        leaveWlan(Screen::WlanRoom);
        return;
    }
    if (!wlan_.takeFrame(wlanPads_[0], wlanPads_[1])) return;
    if (screen_ == Screen::FirstFighter || screen_ == Screen::FirstAssist) {
        stepWlanPicks(screen_ == Screen::FirstAssist, wlanPads_);
    } else if (screen_ == Screen::FirstOrder) {
        stepWlanOrder(wlanPads_);
    } else {
        stepGame(wlanPads_[0].menuHeld, wlanPads_[0].menuPressed, wlanPads_[1]);
    }
}

void Game::step(unsigned held, unsigned pressed) {
    if (screen_ == Screen::WlanMenu || screen_ == Screen::WlanRoom) stepWlanMenu(pressed);
    else if (wlan_.active()) stepWlan(held, pressed);
    else stepGame(held, pressed);
}

void Game::stepGame(unsigned held, unsigned pressed, WlanPad second) {
    const bool confirm = pressed & ConfirmButtons;
    const bool back = pressed & PSP_CTRL_CIRCLE;
    if (screen_ == Screen::Title || screen_ == Screen::Stage) {
        if (confirm || (back && screen_ != Screen::Title)) audio_.play(Sound::Confirm);
        else if (screen_ != Screen::Title && (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) audio_.play(Sound::Select);
    }
    switch (screen_) {
    case Screen::Title: {
#ifdef BVN_RESOURCE_DIAGNOSTICS
        if (pressed & PSP_CTRL_RTRIGGER) {
            resourceDiagnostics.synchronousOnly = !resourceDiagnostics.synchronousOnly;
            if (auto* log = std::fopen((folder_ + "BVN_DIAG.LOG").c_str(), "a")) {
                std::fprintf(log, "reader-change=%s\n", resourceDiagnostics.synchronousOnly ? "stdio-only" : "original-readahead");
                std::fclose(log);
            }
        }
#endif
        const auto modes = titleModes();
        const auto count = modes.size();
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) {
            const unsigned step = pressed & PSP_CTRL_DOWN ? 1 : count - 1;
            mode_ = modes[(titleIndex(modes, mode_) + step) % count];
            audio_.play(Sound::Select);
        }
        if (confirm) {
            if(mode_==BattleMode::Settings){openSettings();break;}
            if (mode_ == BattleMode::Wlan) {
                wlanOption_ = 0;
                screen_ = Screen::WlanMenu;
                audio_.music("select");
                break;
            }
            if (mode_ == BattleMode::Credits) {
                openCredits();
                break;
            }
            arcade_.reset();
            arcadeEncounter_ = nullptr;
            arcadeFaction_ = 2;
            screen_ = Screen::FirstFighter;
            audio_.music("select");
        }
        break;
    }
    case Screen::Credits:
        if (back || confirm) {
            creditsUi_ = UiAsset{};
            creditsPage_ = creditsPages_ = 0;
            screen_ = Screen::Title;
            audio_.play(Sound::Confirm);
            audio_.music("op");
        } else if (creditsPages_ && (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
            creditsPage_ = changeChoice(creditsPage_, creditsPages_, pressed);
            audio_.play(Sound::Select);
        }
        break;
    case Screen::FirstFighter:
    case Screen::SecondFighter:
    case Screen::FirstAssist:
    case Screen::SecondAssist: {
        const bool assists = screen_ == Screen::FirstAssist || screen_ == Screen::SecondAssist;
        const bool cpu = screen_ == Screen::SecondFighter || screen_ == Screen::SecondAssist;
        const bool team = isTeamMode(mode_);
        const bool arcade = isArcadeMode(mode_);
        const auto chosen = stepPick(cpu, assists, pressed);
        if (confirm) {
            if (!chosen) break;
            if (arcade) {
                if (assists) {
                    if (team) arcadeFighter_ = teamPicks_[0][0];
                    arcadeFaction_ = roster_.fighter(arcadeFighter_).faction;
                    arcadeAssist_ = *chosen;
                    arcadeEncounter_ = nullptr;
                    if (arcadeFaction_ < 2) prepareArcadeFight();
                    else screen_ = Screen::ArcadeMissing;
                } else {
                    arcadeFighter_ = *chosen;
                    screen_ = Screen::FirstAssist;
                }
            } else if (assists) screen_ = cpu ? Screen::Stage : Screen::SecondAssist;
            else screen_ = cpu ? Screen::FirstAssist : Screen::SecondFighter;
        }
        else if (back) {
            audio_.play(Sound::Confirm);
            if (team && !assists && teamCount_[cpu]) {
                --teamCount_[cpu];
                break;
            }
            if (arcade && assists) {
                if (team) --teamCount_[0];
                screen_ = Screen::FirstFighter;
                break;
            }
            if (team && !cpu && assists) --teamCount_[1];
            if (team && cpu && !assists) --teamCount_[0];
            if (assists) screen_ = cpu ? Screen::FirstAssist : Screen::SecondFighter;
            else screen_ = cpu ? Screen::FirstFighter : Screen::Title;
            if (screen_ == Screen::Title) audio_.music("op");
        }
        break;
    }
    case Screen::Stage:
        map_ = changeChoice(map_, maps_.size(), pressed);
        if (confirm && (mode_ == BattleMode::TeamVersus || mode_ == BattleMode::TeamWatch)) {
            orderCount_[0] = orderCount_[1] = 0;
            orderCursor_[0] = orderCursor_[1] = 0;
            audio_.music("loading");
            screen_ = Screen::FirstOrder;
        } else if (confirm) beginFight();
        else if (back) screen_ = wlan_.active() ? Screen::FirstAssist : Screen::SecondAssist;
        break;
    case Screen::FirstOrder:
    case Screen::SecondOrder:
        stepOrder(pressed);
        break;
    case Screen::Fight: {
        round_->updateUiClock(sceKernelGetSystemTimeWide());
        const auto phase = round_->phase();
        if (phase == RoundPhase::TeamChange) {
            nextTeamMembers();
            break;
        }
        const bool start = (pressed | second.menuPressed) & PSP_CTRL_START;
        if (start && phase == RoundPhase::Playing) {
            round_->stopInput();round_->manualPause(true);round_->pausedDisplayFrame();
            screen_ = Screen::Pause;
            break;
        }
        const auto player = isWatchMode(mode_) ? FighterControls{} : humanControls(0,wlan_.active()?wlanPads_[0].held:held,wlan_.active()?wlanPads_[0].pressed:pressed);
        const auto rival = wlan_.active() ? humanControls(1,second.held,second.pressed)
            : FighterControls{};
        const auto banner = round_->bannerFrame();
        const bool stepped=round_->step({player, rival}, start);
        if (stepped && isArcadeMode(mode_) && phase == RoundPhase::Playing) {
            const auto& match = round_->match();
            arcade_.addScore(match.pointsThisStep());
            if (round_->phase() == RoundPhase::KnockoutFlash && round_->winner() == 0) {
                const auto& player = match.actor(0);
                arcade_.addScore(FightMatch::modeScore((player.lastBisha() ? 2000 : 0) +
                                                          (round_->perfect() ? 20000 : 3000),settings_.classic()));
            }
        }
        if(stepped)roundAudio(phase, banner);
        if(round_->takeDeferredPause()){round_->stopInput();screen_=Screen::Pause;}
        if (round_->phase() == RoundPhase::Finished) {
            ++stats_.finished;
            stats_.lastWinner = round_->winner();
            stats_.lastRound = round_->round();
            for (unsigned i = 0; i < 2; ++i) stats_.lastWins[i] = round_->wins(i);
            if (isArcadeMode(mode_)) finishArcade();
            else leaveFight();
        }
        break;
    }
    case Screen::Pause:
        round_->updateUiClock(sceKernelGetSystemTimeWide());
        round_->pausedDisplayFrame();
        if ((pressed | second.menuPressed) & PSP_CTRL_CIRCLE) leaveFight();
        else if ((pressed | second.menuPressed) & PSP_CTRL_START) {
            for(auto& input:humanInputs_)input.clear();
            round_->stopInput();
            round_->manualResume();
            screen_ = Screen::Fight;
        }
        break;
    case Screen::Settings:
    case Screen::Keys:stepSettings(held,pressed);break;
    case Screen::LoadError:
        if (back || confirm) {
            if (mode_ == BattleMode::Wlan) screen_ = Screen::WlanMenu;
            else if (isArcadeMode(mode_)) leaveFight();
            else screen_ = Screen::Stage;
        }
        break;
    case Screen::ArcadeMissing:
        if (back || confirm) leaveFight();
        break;
    case Screen::ArcadeWinner:
    case Screen::ArcadeContinue:
    case Screen::ArcadeOver:
    case Screen::ArcadeClear:
        stepArcadeResult(confirm, back, pressed);
        break;
    case Screen::WlanMenu:
    case Screen::WlanRoom:
        break;
    }
}

void Game::roundAudio(RoundPhase previous, std::optional<unsigned> previousBanner) {
    for(const auto name:round_->takeCombatSounds())audio_.sourceEffect(name);
    for (unsigned side = 0; side < 2; ++side) {
        if (const auto cue = round_->takeDashSound(side)) audio_.dashSound(side, *cue);
    }
    for (unsigned count = round_->takeRushSounds(); count; --count) audio_.play(Sound::Rush);
    for (unsigned count = round_->takeGhostStepSounds(); count; --count) audio_.play(Sound::GhostStep);
    audio_.footsteps(round_->takeWalkSounds());
    audio_.ghostFootsteps(round_->takeGhostFootSounds());
    const auto counter = round_->takeCounterSounds();
    if (counter & 1) audio_.play(Sound::CounterReplace);
    if (counter & 2) audio_.play(Sound::CounterCharge);
    if (counter & 4) audio_.play(Sound::CounterBurst);
    const unsigned assists = round_->match().assistSounds();
    if (assists & 1) audio_.play(Sound::AssistBleach);
    if (assists & 2) audio_.play(Sound::AssistNaruto);
    const auto phase = round_->phase();
    if (phase != previous && phase == RoundPhase::KnockoutFlash) audio_.play(Sound::OverHit);
    if (phase != previous && phase == RoundPhase::Knockout) audio_.play(round_->match().superKnockout()?Sound::KOBisha:Sound::KO);
    const auto frame = round_->bannerFrame();
    if (!frame || frame == previousBanner) return;
    switch (*frame) {
    case 11: audio_.play(Sound::Round); break;
    case 32: {
        const Sound numbers[]{Sound::One, Sound::Two, Sound::Three, Sound::Four};
        audio_.play(numbers[round_->round() - 1]);
        break;
    }
    case 57: audio_.play(Sound::Fight); break;
    case 96: audio_.play(Sound::FinalRound); break;
    case 221: audio_.play(Sound::TimeOver); break;
    case 267: audio_.play(Sound::Draw); break;
    default: break;
    }
}

void Game::centered(const char* key, float x, float y, float width, float height) {
    const auto* sprite = ui_.find(key);
    if (!sprite) return;
    if (width <= 0) width = sprite->width / StageTextureScale;
    if (height <= 0) height = sprite->height / StageTextureScale;
    image(key, x - width / 2, y - height / 2, width, height);
}

void Game::drawTitle() {
    image("title", 0, 0, 800, 600);
    const auto modes = titleModes();
    const unsigned selected = titleIndex(modes, mode_);
    constexpr unsigned visible = 7;
    const unsigned first = selected >= visible ? selected - visible + 1 : 0;
    renderer_.box(226, 307, 348, 281, Panel);
    renderer_.box(226, 307, 348, 2, Accent);
    const float selectedY = 333 + 34 * (selected - first);
    renderer_.box(243, selectedY - 16, 314, 32, 0x80495d5d);
    renderer_.box(243, selectedY - 16, 3, 32, Accent);
    for (unsigned index = first; index < std::min(unsigned(modes.size()), first + visible); ++index) {
        const float y = 333 + 34 * (index - first);
        switch (modes[index]) {
        case BattleMode::Versus:
        case BattleMode::Arcade:
            centered("single-play", 367, y, 174, 30);
            centered(modes[index] == BattleMode::Arcade ? "label-arcade" : "label-team-cpu", 493, y + 1);
            break;
        case BattleMode::TeamVersus:
        case BattleMode::TeamArcade:
            centered("team-play", 367, y, 147, 30);
            centered(modes[index] == BattleMode::TeamArcade ? "label-arcade" : "label-team-cpu", 493, y + 1);
            break;
        case BattleMode::Training: centered("training", 400, y, 149, 30); break;
        case BattleMode::Wlan: centered("label-wlan", 400, y); break;
        default: break;
        case BattleMode::Watch: centered("label-watch-single", 400, y); break;
        case BattleMode::TeamWatch: centered("label-watch-team", 400, y); break;
        case BattleMode::Credits: centered("label-credits", 400, y); break;
        case BattleMode::Settings: settingsText("title",400,y); break;
        }
    }
    centered("label-title-choose", 400, 574);
    image("studio", 14, 14, 82, 51);
}

GameSettings Game::battleSettings()const {
    auto config=settings_;
    if(wlan_.active())config.applyWireRules(wlan_.rules());
    return config;
}

void Game::applySettings() {
    cpuLevel_=settings_.difficulty;aiGameHz_=settings_.gameHz();
    audio_.volumes(settings_.soundPercent(),settings_.musicPercent());
    renderer_.quality(settings_.quality);
}

FighterControls Game::humanControls(unsigned side,unsigned held,unsigned pressed) {
    const KeyBindings defaults;
    const auto& keys=wlan_.active()?defaults:settings_.keys[side];
    return humanInputs_[side].controls(keys.logical(held),keys.logical(pressed),settings_.classic(wlan_.active()));
}

void Game::settingsText(const char* key,float x,float y,bool left) {
    if(const auto* sprite=settingsUi_.find(key)) {
        const auto width=sprite->width/StageTextureScale,height=sprite->height/StageTextureScale;
        renderer_.image(settingsUi_,key,left?x:x-width/2,y-height/2,width,height);
    }
}

void Game::openSettings() {
    settingsRow_=0;settingsSaveFailed_=false;screen_=Screen::Settings;audio_.music("back");
}

void Game::stepSettings(unsigned held,unsigned pressed) {
    const auto leave=[&]{screen_=Screen::Title;audio_.music("op");};
    const auto save=[&]{
        const auto parent=settingsFolder_.substr(0,settingsFolder_.find_last_of('/'));
        sceIoMkdir(parent.c_str(),0777);sceIoMkdir(settingsFolder_.c_str(),0777);
        if(settingsStore_.save(settingsFolder_.c_str(),settings_)){settingsSaveFailed_=false;leave();}
        else settingsSaveFailed_=true;
    };
    if(screen_==Screen::Keys) {
        if(keyCapture_) {
            if(pressed&PSP_CTRL_START){keyCapture_=false;keyBulk_=false;return;}
            unsigned gameMask=0;for(const auto b:DefaultButtons)gameMask|=b;
            if(!(held&gameMask))keyCaptureArmed_=true;
            if(keyCaptureArmed_)for(const auto b:DefaultButtons)if(pressed&b){
                editingKeys_.buttons[keyRow_]=b;audio_.play(Sound::Confirm);
                if(keyBulk_&&keyRow_+1<HumanKeyCount){++keyRow_;keyCaptureArmed_=false;}
                else {keyCapture_=false;if(keyBulk_)keyRow_=13;keyBulk_=false;}
                break;
            }
            return;
        }
        if(pressed&PSP_CTRL_CIRCLE){screen_=Screen::Settings;return;}
        if(pressed&PSP_CTRL_UP)keyRow_=(keyRow_+13)%14;
        if(pressed&PSP_CTRL_DOWN)keyRow_=(keyRow_+1)%14;
        if(keyRow_>=10&&(pressed&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT)))
            keyRow_=10+(keyRow_-10+((pressed&PSP_CTRL_RIGHT)?1:3))%4;
        if(keyRow_<HumanKeyCount&&(pressed&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT))) {
            auto i=std::ranges::find(DefaultButtons,editingKeys_.buttons[keyRow_])-DefaultButtons.begin();
            i=(i+DefaultButtons.size()+((pressed&PSP_CTRL_RIGHT)?1:-1))%DefaultButtons.size();
            editingKeys_.buttons[keyRow_]=DefaultButtons[i];audio_.play(Sound::Select);
        }
        if(pressed&ConfirmButtons) {
            if(keyRow_<HumanKeyCount){keyBulk_=false;keyCapture_=true;keyCaptureArmed_=false;}
            else if(keyRow_==10)editingKeys_={};
            else if(keyRow_==13){keyRow_=0;keyBulk_=true;keyCapture_=true;keyCaptureArmed_=false;}
            else {if(keyRow_==11)settings_.keys[keyProfile_]=editingKeys_;screen_=Screen::Settings;}
            audio_.play(Sound::Confirm);
        }
        return;
    }
    if(settingsSaveFailed_){if(pressed&PSP_CTRL_CIRCLE)leave();else if(pressed&ConfirmButtons)save();return;}
    if(pressed&PSP_CTRL_CIRCLE){save();return;}
    if(pressed&PSP_CTRL_UP)settingsRow_=(settingsRow_+10)%11;
    if(pressed&PSP_CTRL_DOWN)settingsRow_=(settingsRow_+1)%11;
    if(settingsRow_<8&&(pressed&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT|PSP_CTRL_CROSS))) {
        settings_.change(settingsRow_,(pressed&PSP_CTRL_LEFT)?-1:1);applySettings();audio_.play(Sound::Select);
    } else if(pressed&ConfirmButtons) {
        if(settingsRow_==8||settingsRow_==9){keyProfile_=settingsRow_-8;keyRow_=0;keyCapture_=keyBulk_=false;editingKeys_=settings_.keys[keyProfile_];screen_=Screen::Keys;}
        else if(settingsRow_==10){settings_={};applySettings();}
        audio_.play(Sound::Confirm);
    }
}

void Game::drawSettings() {
    image("order-bg",0,0,800,600);renderer_.box(62,36,676,552,Panel);
    settingsText(screen_==Screen::Keys?(keyProfile_?"profile-1":"profile-0"):"title",400,76);
    if(screen_==Screen::Keys) {
        for(unsigned i=0;i<14;++i){
            const float y=i<10?128+34*i:500;
            const float x=i<10?400:190+140*(i-10);
            if(i==keyRow_)renderer_.box(i<10?95:x-66,y-16,i<10?610:132,32,0x80495d5d);
            char key[32];
            if(i<10){std::snprintf(key,sizeof(key),"key-%u",i);settingsText(key,125,y,true);
                const auto b=std::ranges::find(DefaultButtons,editingKeys_.buttons[i])-DefaultButtons.begin();
                std::snprintf(key,sizeof(key),"button-%u",unsigned(b));settingsText(key,610,y);}
            else settingsText(i==10?"default":i==11?"apply":i==12?"cancel":"set-all",x,y);
        }
        settingsText(keyCapture_?"capture-hint":"keys-hint",400,558);
    } else {
        const unsigned values[]{settings_.difficulty-1,settings_.mode,settings_.life,settings_.time,settings_.sound,settings_.music,settings_.quality,settings_.showHp};
        for(unsigned i=0;i<11;++i){
            const float y=140+34*i;
            if(i==settingsRow_)renderer_.box(95,y-16,610,32,0x80495d5d);
            char key[32];
            if(i<8){std::snprintf(key,sizeof(key),"row-%u",i);settingsText(key,125,y,true);
                std::snprintf(key,sizeof(key),"value-%u-%u",i,values[i]);settingsText(key,585,y);}
            else settingsText(i==8?"profile-0":i==9?"profile-1":"default",125,y,true);
        }
        settingsText(settingsSaveFailed_?"save-error":"main-hint",400,558);
    }
}

void Game::openCredits() {
    creditsPage_ = creditsPages_ = 0;
    ++creditsEntries_;
    const bool loaded = creditsUi_.open((folder_ + "CREDITS.BVU").c_str());
    if (loaded && creditsUi_.find("background") && creditsUi_.find("title") && creditsUi_.find("controls")) {
        for (unsigned page = 0; page < 32; ++page) {
            char imageKey[32], numberKey[32];
            std::snprintf(imageKey, sizeof(imageKey), "page-%u", page);
            std::snprintf(numberKey, sizeof(numberKey), "number-%u", page);
            if (!creditsUi_.find(imageKey) || !creditsUi_.find(numberKey)) break;
            ++creditsPages_;
        }
    }
    if (!creditsPages_) creditsUi_ = UiAsset{};
    sceKernelDcacheWritebackAll();
    screen_ = Screen::Credits;
    audio_.music("back");
}

void Game::drawCredits() {
    if (!creditsPages_) {
        drawTitle();
        renderer_.box(193, 203, 414, 188, Panel);
        centered("label-load-error", 400, 269);
        centered("label-confirm", 400, 332);
        return;
    }
    renderer_.image(creditsUi_, "background", 0, 0, 800, 600);
    renderer_.box(20, 72, 760, 447, 0xb0000000);
    const auto label = [&](const char* key, float y) {
        if (const auto* sprite = creditsUi_.find(key)) {
            const float width = sprite->width / StageTextureScale, height = sprite->height / StageTextureScale;
            renderer_.image(creditsUi_, key, 400 - width / 2, y - height / 2, width, height);
        }
    };
    label("title", 39);
    char key[32];
    std::snprintf(key, sizeof(key), "page-%u", creditsPage_);
    renderer_.image(creditsUi_, key, 30, 90, 740, 416);
    std::snprintf(key, sizeof(key), "number-%u", creditsPage_);
    label(key, 541);
    label("controls", 576);
}

void Game::drawWlanMenu() {
    image("title", 0, 0, 800, 600);
    renderer_.box(174, 312, 452, 252, Panel);
    renderer_.box(174, 312, 452, 2, Accent);
    centered("label-wlan", 400, 345);
    if (screen_ == Screen::WlanMenu) {
        constexpr const char* choices[]{"label-wlan-single", "label-wlan-team", "label-wlan-join"};
        for (unsigned i = 0; i < std::size(choices); ++i) {
            const float y = 398 + 48 * i;
            if (i == wlanOption_) {
                renderer_.box(196, y - 20, 408, 40, 0x80495d5d);
                renderer_.box(196, y - 20, 3, 40, Accent);
            }
            centered(choices[i], 400, y);
        }
        centered("label-order-choose", 400, 540);
        return;
    }
    const char* status = "label-wlan-failed";
    switch (wlan_.status()) {
    case WlanStatus::Searching: status = "label-wlan-search"; break;
    case WlanStatus::Waiting: status = "label-wlan-wait"; break;
    case WlanStatus::Connecting: status = "label-wlan-connect"; break;
    case WlanStatus::WlanOff: status = "label-wlan-off"; break;
    case WlanStatus::Timeout: status = "label-wlan-timeout"; break;
    case WlanStatus::PeerLeft: status = "label-wlan-disconnect"; break;
    case WlanStatus::Incompatible: status = "label-wlan-version"; break;
    default: break;
    }
    centered(status, 400, 431);
    centered(wlan_.busy() ? "label-wlan-cancel" : "label-wlan-back", 400, 518);
}

void Game::drawFighters() {
    const bool assists = screen_ == Screen::FirstAssist || screen_ == Screen::SecondAssist;
    const bool linked = wlan_.active();
    const unsigned side = linked ? !wlan_.host() : screen_ == Screen::SecondFighter || screen_ == Screen::SecondAssist;
    const auto& roster = assists ? assistRoster_ : roster_;
    const auto& picks = assists ? assistPicks_ : picks_;
    const char* prefix = assists ? "assist" : "fighter";
    const bool choosingTeam = !assists && isTeamMode(mode_);
    image("select", 0, 0, 800, 600);
    renderer_.box(0, 0, 800, 86, Panel);
    const char* heading = linked ? (choosingTeam ? "label-wlan-teams" : assists ? "label-wlan-assists" : "label-wlan-fighters")
        : choosingTeam ? (side ? "label-cpu-team" : "label-player-team") : assists
        ? (!side ? "label-player-assist" : mode_ == BattleMode::Training ? "label-dummy-assist" : "label-cpu-assist")
        : (!side ? "label-player-pick" : mode_ == BattleMode::Training ? "label-dummy-pick" : "label-cpu-pick");
    if (isWatchMode(mode_)) {
        heading = choosingTeam ? "label-cpu-team" : assists ? "label-cpu-assist" : "label-cpu-pick";
    }
    centered(heading, 400, 23);
    if (isWatchMode(mode_)) centered(side ? "label-watch-p2" : "label-watch-p1", 205, 23);
    if (linked) centered(wlanSide(), 400, 53, 100, 25);
    renderer_.box(0, 554, 800, 46, Panel);
    centered(linked ? (assists ? "label-wlan-assist-keys" : "label-wlan-pick-keys")
                    : assists ? "label-assist-choose" : "label-grid-choose", 400, 576);
    const auto cells = roster.cells();
    for (unsigned i = 0; i < cells.size(); ++i) {
        const auto& cell = cells[i];
        if (cell.fighter == MissingFighter) continue;
        const bool current = picks[side].cell == i;
        const unsigned id = current ? selected(side, assists) : cell.choice(roster.initialAlternate(i));
        const auto& fighter = roster.fighter(id);
        char key[32];
        std::snprintf(key, sizeof(key), "%s-icon-%u", prefix, id);
        if (linked) {
            if (picks[0].cell == i) renderer_.box(cell.x - 4, cell.y - 4, 58, 58, Accent);
            if (picks[1].cell == i) renderer_.box(cell.x - 2, cell.y - 2, 54, 54, Rival);
        } else if (current) renderer_.box(cell.x - 3, cell.y - 3, 56, 56, side ? Rival : Accent);
        image("select-item", cell.x, cell.y, 50, 50);
        image(key, cell.x + 1, cell.y + 1, 48, 48);
        const bool enabled = choosingTeam ? teamChoice(side, {i, current ? picks[side].alternate : roster.initialAlternate(i)}, 0).has_value()
            : fighter.playable() || fighter.random;
        if (!enabled) renderer_.box(cell.x + 1, cell.y + 1, 48, 48, 0x90201624);
    }
    drawSelectedFighter(0, assists);
    if (side || linked) drawSelectedFighter(1, assists);
    if (linked) {
        for (unsigned player = 0; player < 2; ++player) {
            if (wlanReady_[player]) centered("label-wlan-ready", player ? 733 : 67, 77, 86, 24);
        }
    }
    if (choosingTeam) {
        for (unsigned team = 0; team <= (linked ? 1U : side); ++team) {
            for (unsigned slot = 0; slot < TeamSize; ++slot) {
                const float x = team ? 572 + 32 * slot : 122 + 32 * slot;
                renderer_.box(x, 58, 26, 26, 0x803a2b44);
                if (slot < teamCount_[team]) {
                    char key[32];
                    std::snprintf(key, sizeof(key), "fighter-icon-%u", teamPicks_[team][slot]);
                    image(key, x, 58, 26, 26);
                }
            }
        }
        if ((!linked || !wlanReady_[side]) && !teamChoice(side, picks[side], 0)) {
            centered(roster.resolve(selected(side), 0) ? "label-already-picked" : "label-unavailable",
                     400, linked ? 76 : 47, linked ? 90 : 0, linked ? 22 : 0);
        }
    } else if (!roster.resolve(selected(side, assists), 0)) {
        centered("label-unavailable", 400, linked ? 76 : 47, linked ? 90 : 0, linked ? 22 : 0);
    }
}

void Game::drawSelectedFighter(unsigned side, bool assists) {
    const auto id = selected(side, assists);
    const auto& entry = (assists ? assistRoster_ : roster_).fighter(id);
    const char* prefix = assists ? "assist" : "fighter";
    char key[32];
    if (entry.playable()) {
        std::snprintf(key, sizeof(key), "%s-card-%u", prefix, id);
        image(key, side ? 685 : 13, 3, 102, 68, side);
    } else {
        std::snprintf(key, sizeof(key), "%s-icon-%u", prefix, id);
        image(key, side ? 730 : 20, 9, 54, 54);
    }
    std::snprintf(key, sizeof(key), "%s-name-%u", prefix, id);
    centered(key, side ? 608 : 192, 46);
}

void Game::drawMap() {
    image("select", 0, 0, 800, 600);
    const bool linked = wlan_.active();
    centered(linked ? "label-wlan-map" : "label-map-pick", 400, 85);
    image("select-map", 157, 150, 486, 286);
    image(maps_[map_].preview.c_str(), 174.5f, 174, 450, 240);
    image("select-frame", 171, 169, 458, 248);
    image("select-arrow", 105, 268, 37, 50, true);
    image("select-arrow", 658, 268, 37, 50);
    centered(maps_[map_].title.c_str(), 400, 491);
    centered(linked ? (wlan_.host() ? "label-wlan-map-keys" : "label-wlan-leave") : "label-choose", 400, 565);
}

void Game::drawOrder() {
    image("order-bg", 0, 0, 800, 600);
    renderer_.box(0, 0, 800, 98, Panel);
    const unsigned active = screen_ == Screen::SecondOrder;
    const bool linked = wlan_.active();
    const char* heading = linked ? "label-wlan-order" : active ? "label-order-cpu" : "label-order-player";
    if (isWatchMode(mode_)) heading = "label-order-cpu";
    centered(heading, 400, 47);
    if (isWatchMode(mode_)) centered(active ? "label-watch-p2" : "label-watch-p1", 205, 47);
    if (linked) centered(wlanSide(), 400, 77);
    centered("order-vs", 400, 290, 82, 65);
    for (unsigned side = 0; side < 2; ++side) {
        const float x = side ? 466 : 54;
        for (unsigned slot = 0; slot < teamCount_[side]; ++slot) {
            const float y = 141 + 100 * slot;
            const auto id = teamPicks_[side][slot];
            if ((linked ? orderCount_[side] < TeamSize : side == active) && slot == orderCursor_[side]) {
                renderer_.box(x - 3, y - 3, 286, 78, side ? Rival : Accent);
            }
            renderer_.box(x, y, 280, 72, Panel);
            char key[32];
            std::snprintf(key, sizeof(key), "fighter-card-%u", id);
            image(key, x + 1, y + 2, 102, 68);
            std::snprintf(key, sizeof(key), "fighter-name-%u", id);
            if (const auto* name = ui_.find(key)) {
                const float width = std::min(120.f, float(name->width / StageTextureScale));
                centered(key, x + 167, y + 37, width, width * name->height / name->width);
            }
            const auto order = std::span(teamOrder_[side]).first(orderCount_[side]);
            const auto entry = std::ranges::find(order, id);
            if (entry != order.end()) {
                std::snprintf(key, sizeof(key), "order-number-%u", unsigned(entry - order.begin()) + 1);
                image(key, x + 231, y + 17, 38, 38);
            }
        }
        char key[32];
        const unsigned assist = isArcadeMode(mode_) ? (side ? arcadeEncounter_->assister : arcadeAssist_) : selected(side, true);
        std::snprintf(key, sizeof(key), "assist-icon-%u", assist);
        image(key, x + 5, 462, 44, 44);
        std::snprintf(key, sizeof(key), "assist-name-%u", assist);
        centered(key, x + 120, 485);
    }
    renderer_.box(0, 548, 800, 52, Panel);
    centered(linked ? "label-wlan-order-keys" : "label-order-choose", 400, 573);
}

void Game::drawScore(float x, float y) {
    constexpr float labelHeight = 24, gap = 9, digitWidth = 15, advance = 17;
    const auto* label = ui_.find("label-score");
    const float labelWidth = label ? labelHeight * label->width / label->height : 0;
    char digits[12]{};
    const auto length = std::snprintf(digits, sizeof(digits), "%u", arcade_.score());
    x -= (labelWidth + gap + digitWidth + (length - 1) * advance) / 2;
    if (label) centered("label-score", x + labelWidth / 2, y, labelWidth, labelHeight);
    x += labelWidth + gap + digitWidth / 2;
    for (const char* digit = digits; *digit; ++digit) {
        char key[]{"time-0"};
        key[5] = *digit;
        centered(key, x, y, digitWidth, 23);
        x += advance;
    }
}

void Game::drawArcadeResult() {
    if (screen_ == Screen::ArcadeMissing) {
        image("select", 0, 0, 800, 600);
        renderer_.box(157, 195, 486, 206, Panel);
        centered(arcadeEncounter_ ? "label-arcade-missing" : "label-arcade-no-route", 400, 245);
        if (arcadeEncounter_) centered(maps_[arcadeEncounter_->map].title.c_str(), 400, 292);
        centered("label-confirm", 400, 352);
        return;
    }
    const auto art = [&](const char* key, float x, float y, float width, float height) {
        renderer_.image(arcadeUi_, key, x, y, width, height);
    };
    if (screen_ == Screen::ArcadeOver) {
        art("over-bg", 0, 0, 800, 600);
        art("over", 104, 125, 592, 231);
    } else if (screen_ == Screen::ArcadeClear) {
        art("clear-bg", 0, 0, 800, 600);
    } else {
        const bool retry = screen_ == Screen::ArcadeContinue;
        if (retry) art("continue-bg", 0, 0, 800, 600);
        else image("order-bg", 0, 0, 800, 600);
        art(retry ? "continue" : "winner", 214, 111, 372, retry ? 66 : 85);
        char key[32];
        std::snprintf(key, sizeof(key), "fighter-card-%u", arcadePortrait_);
        image(key, 298, 241, 204, 136);
        std::snprintf(key, sizeof(key), "fighter-name-%u", arcadePortrait_);
        centered(key, 400, 403);
        if (retry) {
            renderer_.box(continueArcade_ ? 215 : 430, 459, 155, 49, 0x80495d5d);
            centered("label-arcade-yes", 292, 484);
            centered("label-arcade-no", 507, 484);
        }
    }
    renderer_.box(0, 527, 800, 73, Panel);
    drawScore(400, 547);
    centered(screen_ == Screen::ArcadeContinue ? "label-choose" :
             screen_ == Screen::ArcadeWinner ? "label-arcade-next" : "label-arcade-title", 400, 579);
}

void Game::drawHud(const VersusRound& round) {
    if (!round.hud().visible()) return;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& actor = round.match().actor(i);
        const auto id = formIds_[i][actor.form()];
        const bool bleach = roster_.fighter(id).faction == 0;
        const float barX = i ? 427 : 102;
        image("bar_back", barX, 39, 271, 19, i);
        const auto& hpView=round.hud().hp(i);
        const auto& energyView=round.hud().energy(i);
        const auto& qiView=round.hud().qi(i);
        const float red=gauge(hpView.red(),1,260);
        if(red>0)renderer_.image(hudArt_,"hp-red",i?barX+6:barX+266-red,42,red,15,i);
        const float hp = gauge(hpView.green(),1,260);
        if (hp > 0) image("bar", i ? barX + 6 : barX + 266 - hp, 42, hp, 15, i);
        image("bar_front", barX, 37, 271, 23, i);
        if(settings_.showHp&&round.hud().sampled()) {
            char number[32];std::snprintf(number,sizeof(number),"%.12g",round.hud().sample(i).hp);
            const auto count=std::strlen(number);float width=0;
            for(const char* p=number;*p;++p){char glyph[32];std::snprintf(glyph,sizeof(glyph),"hp-%u",unsigned((unsigned char)*p));if(const auto* a=settingsUi_.find(glyph))width+=a->width/StageTextureScale+1;}
            float x=(i?580.f:220.f)-width/2;
            for(std::size_t n=0;n<count;++n){char glyph[32];std::snprintf(glyph,sizeof(glyph),"hp-%u",unsigned((unsigned char)number[n]));if(const auto* a=settingsUi_.find(glyph)){settingsText(glyph,x,86,true);x+=a->width/StageTextureScale+1;}}
        }
        char key[32];
        std::snprintf(key, sizeof(key), "fighter-hud-%u", id);
        image(key, i ? 698 : 0, 11, 102, 64, i);
        std::snprintf(key, sizeof(key), "fighter-name-%u", id);
        if (const auto* name = ui_.find(key)) {
            const float halfWidth = float(name->width / StageTextureScale / 2);
            const float x = i ? std::min(749.f, 798.f - halfWidth) : std::max(51.f, 2.f + halfWidth);
            centered(key, x, 64);
        }
        const float energy = gauge(energyView.value(), 1, 123);
        const float energyX = i ? 424 : 236;
        image("sbar_back", energyX, 66, 127, 7, i);
        if (energy > 0){if(energyView.frame()==2)renderer_.image(hudArt_,"energy-low",i?energyX+2:energyX+125-energy,67,energy,5,i);
            else image("sbar",i?energyX+2:energyX+125-energy,67,energy,5,i);}
        image("sbar_front", energyX - 6, 64, 140, 10, i);
        if(energyView.frame()==2)renderer_.image(hudArt_,bleach?"energy-bleach-low":"energy-naruto-low",i?562:166,62,0,0);
        else image(bleach ? "sbar_txt1" : "sbar_txt2", i ? 562 : 166, 62);
        const char* badge = bleach ? "win_bleach" : "win_naruto";
        for (unsigned win = 0; win < round.wins(i); ++win) {
            image(badge, i ? 448 + 28 * win : 325 - 28 * win, 88, 22, 23);
        }
        if (round.team()) {
            for (unsigned slot = round.member(i) + 1; slot < round.memberCount(i); ++slot) {
                std::snprintf(key, sizeof(key), "fighter-icon-%u", teamOrder_[i][slot]);
                const unsigned offset = 32 * (slot - round.member(i) - 1);
                image(key, i ? 748 - offset : 23 + offset, 85, 27, 27);
            }
        }
        if (!qiView.visible()) continue;
        const float qiX = i ? 552 : 64;
        const float assistX = i ? 740 : 6;
        image("qbar_outback", assistX + 1, 537, 52, 38, i);
        renderer_.imageFillBottom(ui_, "qbar_bar", assistX + 1, 537, 52, 38, i,
                                  float(qiView.assist()));
        image("qbar_outborder", assistX, 536, 54, 40, i);
        std::snprintf(key, sizeof(key), "assist-icon-%u", activeAssists_[i]);
        image(key, assistX + 13, 542, 28, 28, i);
        image("qbar_back2", qiX + (i ? 23 : 11), 549, 158, 19, i);
        const double qi=std::clamp(qiView.value(),0.,3.);
        const int stock=int(qi);
        for(unsigned layer=0;layer<3;++layer){
            const float width=gauge(qi-layer,1,140);if(width<=0)continue;
            const char* key=layer==0?"qbar_bar1":layer==1?"qbar_bar2":"qbar_bar3";
            image(key,i?qiX+163-width:qiX+12,553,width,12,i);
        }
        if(qi>=3&&qiView.maxVisible())renderer_.image(hudArt_,"qi-max",i?qiX+23:qiX+12,553,140,12,i);
        image("qbar_border2", qiX, 547, 191, 23, i);
        char number[]{"qi-0"};
        number[3] += stock;
        image(number, qiX + (i ? 13 : 166), 551, 11, 14);
        if(qiView.readyVisible())image("qbar_ready",i?714.4f:49.6f,i?532.2f:531.2f,33,12);
    }
    centered("bar_center", 400, 48, 75, 54);
    if (round.displayedSeconds() < 0) {
        centered("timetxt_wx", 400, 47, 43, 27);
    } else {
        char digit[]{"time-0"};
        digit[5] += round.displayedSeconds() / 10 % 10;
        centered(digit, 387.5f, 46);
        digit[5] = '0' + round.displayedSeconds() % 10;
        centered(digit, 412.5f, 46);
    }
    if (isArcadeMode(mode_)) drawScore(400, 117);
    for(unsigned side=0;side<2;++side)sourceComboDraw(round.match().comboUi(side),false,side,
        [&](const char* key,double x,double y,double w,double h){renderer_.image(comboArt_,key,x,y,w,h);});
}

void Game::drawTrainingHistory(const VersusRound& round) {
    const auto history = round.trainingHistory(0);
    for (std::size_t row = 0; row < history.size(); ++row) {
        const float y = 490 - float(history.size() - 1 - row) * 34;
        float x = 20;
        const auto glyph = [&](const char* key) {
            if (const auto* sprite = ui_.find(key)) {
                image(key, x, y);
                x += float(sprite->width / StageTextureScale) + 2;
            }
        };
        const auto mappedGlyph=[&](HumanKey key,bool highlight) {
            const auto physical=settings_.keys[0].buttons[unsigned(key)];
            const auto index=std::ranges::find(DefaultButtons,physical)-DefaultButtons.begin();
            char name[32];std::snprintf(name,sizeof(name),highlight?"button-hi-%u":"button-%u",unsigned(index));
            if(const auto* sprite=settingsUi_.find(name)){settingsText(name,x,y+sprite->height/StageTextureScale/2,true);x+=sprite->width/StageTextureScale+2;}
        };
        const auto& line = history[row];
        for (unsigned i = 0; i < line.count; ++i) {
            if (i) glyph("log-sep");
            const auto input = line.commands[i];
            if(settings_.keys[0]!=KeyBindings{}) {
                std::array<HumanKey,3> combo{};unsigned count=0;
                const auto add=[&](HumanKey key){combo[count++]=key;};
                if(input.ghost){add(input.ghost==2?HumanKey::Up:HumanKey::Down);add(HumanKey::Dash);}
                else if(input.assist)add(HumanKey::Assist);
                else if(input.command==FighterCommand::Count)add(input.horizontal<0?HumanKey::Left:HumanKey::Right);
                else if(input.drop){add(HumanKey::Down);add(HumanKey::Jump);}
                else {
                    switch(input.command) {
                    case FighterCommand::SkillDown:case FighterCommand::SpecialDown:case FighterCommand::SuperDown:case FighterCommand::TransformDown:add(HumanKey::Down);break;
                    case FighterCommand::SkillUp:case FighterCommand::SpecialUp:case FighterCommand::SuperUp:case FighterCommand::TransformUp:add(HumanKey::Up);break;
                    case FighterCommand::CatchAttack:case FighterCommand::CatchSpecial:add(input.horizontal<0?HumanKey::Left:HumanKey::Right);break;
                    default:break;
                    }
                    switch(input.command) {
                    case FighterCommand::Attack:case FighterCommand::SkillDown:case FighterCommand::SkillUp:case FighterCommand::AirAttack:case FighterCommand::CatchAttack:add(HumanKey::Attack);break;
                    case FighterCommand::Special:case FighterCommand::SpecialDown:case FighterCommand::SpecialUp:case FighterCommand::AirSpecial:case FighterCommand::CatchSpecial:add(HumanKey::Skill);break;
                    case FighterCommand::Super:case FighterCommand::SuperUp:case FighterCommand::SuperDown:case FighterCommand::SuperAir:add(HumanKey::Super);break;
                    case FighterCommand::Transform:case FighterCommand::TransformUp:case FighterCommand::TransformDown:add(HumanKey::Attack);add(HumanKey::Jump);break;
                    case FighterCommand::Jump:add(HumanKey::Jump);break;
                    case FighterCommand::Dash:add(HumanKey::Dash);break;
                    case FighterCommand::Guard:add(HumanKey::Down);break;
                    default:break;
                    }
                }
                if(count){for(unsigned k=0;k<count;++k){if(k){if(const auto* plus=settingsUi_.find("input-plus")){settingsText("input-plus",x,y+plus->height/StageTextureScale/2,true);x+=plus->width/StageTextureScale+2;}}mappedGlyph(combo[k],input.highlight);}continue;}
            }
            if (input.ghost) glyph(input.ghost == 2 ? "log-ghost-jump" : "log-ghost-step");
            else if (input.assist) glyph(input.highlight ? "log-highlight-assist" : "log-assist");
            else if (input.command == FighterCommand::Count) glyph(input.horizontal < 0 ? "log-left" : "log-right");
            else if (input.drop) glyph("log-drop");
            else if (input.command == FighterCommand::CatchAttack || input.command == FighterCommand::CatchSpecial) {
                glyph(input.horizontal < 0 ? "log-left" : "log-right");
                glyph(input.command == FighterCommand::CatchAttack ? "log-0" : "log-7");
            } else {
                char key[32];
                std::snprintf(key, sizeof(key), input.highlight ? "log-highlight-%u" : "log-%u", unsigned(input.command));
                glyph(key);
            }
        }
    }
}

void Game::drawBanner(const VersusRound& round) {
    const auto displayedFrame=round.bannerFrame();
    if (round.phase() == RoundPhase::Victory || (displayedFrame&&*displayedFrame>=199&&*displayedFrame<=209)) {
        const bool second = round.winner() == 1;
        image("txt_winner", second ? 546 : 30, 120, 224, 95);
        if (round.perfect()) {
            image("txt_perfect", second ? 549 : 33, 207, 218, 95);
        }
        return;
    }
    const auto frame = round.bannerFrame();
    if (!frame) return;
    const auto f = *frame;
    switch (round.phase()) {
    case RoundPhase::Ready:
        if (f >= 54 && f < 87) {
            const float scale = f < 56 ? (f - 53) / 3.0f : f >= 83 ? (87 - f) / 4.0f : 1;
            centered("txt_fight", 400, 250, 288 * (f < 56 ? scale : 1), 124 * scale);
        } else if (f < 54) {
            const float h = std::min(1.0f, (f - 8) / 7.0f) * 114;
            image("txt_round", 179.85f, 246 - h / 2, 249, h);
            if (f >= 26) {
                char digit[]{"r1"};
                digit[1] += round.round() - 1;
                image(digit, 428, 189, 82, 115);
            }
        } else if (f < 139) {
            centered("label-final", 400, 194);
            centered("txt_round", 400, 285, 249, 114);
        }
        break;
    case RoundPhase::Knockout:
        if (f >= 184) break;
        image("txt_k", 270, 181, 112, 154);
        if (f >= 148) image("txt_kod", 346, 270, 62, 64);
        if (f >= 153) image("txt_o", 389, 177, 110, 158);
        if (f >= 157) image("txt_kod", 459, 270, 62, 64);
        break;
    case RoundPhase::TimeOver:
        if (f >= 222) centered("txt_timeover", 400, 286, 279, 96);
        break;
    case RoundPhase::Draw:
        if(f>=308)break;
        centered("txt_drawgame", 400, 286, 316, 95);
        break;
    default: break;
    }
}

void Game::drawFight() {
    const auto& match = round_->match();
    sprites_.clear();
    const auto appendActor = [&](unsigned side) {
        const bool hidden=match.hidesTarget(1-side);
        if(hidden&&!match.sourceControl())return;
        const auto& actor = match.actor(side);
        sprites_.push_back({actor.asset(), actor.pose(), actor.motion().position, actor.facing(), actor.screenPose(), actor.opacity(),actor.visualTint(),SourcePaint::key(SourcePaint::Kind::Sprite,side),!hidden});
    };
    const auto appendAttacker = [&](const FightAttacker& attacker) {
        sprites_.push_back({attacker.asset(), attacker.pose(), attacker.position(), attacker.facing()});
    };
    if(match.sourceControl()){
        for(auto key:match.sourceScene().paint().players())if(SourcePaint::kind(key)==SourcePaint::Kind::Sprite){
            const auto id=SourcePaint::id(key);
            if(!(id&SourceScene::Object)){if(id<2)appendActor(id);continue;}
            const auto index=match.sourceScene().storage(id);if(index>=match.objects().size())continue;
            std::visit([&](const auto& object){sprites_.push_back({object.asset(),object.pose(),object.position(),object.facing(),nullptr,255,{},key});},match.objects()[index]);
        }
    }else{
    std::array<const FightAttacker*, 2> swapped{};
    for (auto object = match.objects().rbegin(); object != match.objects().rend(); ++object) {
        const auto* attacker = std::get_if<FightAttacker>(&*object);
        if (!attacker || !attacker->active() || !attacker->behindFighters()) continue;
        const auto target = 1 - attacker->owner();
        if (attacker->swapsTargetLayer() && !swapped[target]) {
            swapped[target] = attacker;
            appendActor(target);
        } else {
            appendAttacker(*attacker);
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (swapped[i]) appendAttacker(*swapped[i]);
        else appendActor(i);
    }
    for (const auto& object : match.objects()) {
        if (const auto* attacker = std::get_if<FightAttacker>(&object);
            attacker && (!attacker->active() || attacker->behindFighters())) continue;
        std::visit([&](const auto& member) {
            sprites_.push_back({member.asset(), member.pose(), member.position(), member.facing()});
        }, object);
    }
    }
    const auto scene=match.scene();
    auto arenaEffects=scene;arenaEffects.shines={};arenaEffects.shineAlpha=0;
    renderer_.arena(stage_, match.camera(), sprites_, arenaEffects, &ui_, &reactionArt_,&combatArt_,&mapBlurArt_,&mapOpticalArt_);
    renderer_.beginUi();
    drawHud(*round_);
    if (round_->training()) drawTrainingHistory(*round_);
    drawBanner(*round_);
    if (wlan_.active() && screen_ != Screen::Pause) centered(wlanSide(), 400, 577);
    if (screen_ == Screen::Pause) {
        renderer_.box(0, 0, 800, 600, 0xa0000000);
        renderer_.box(224, 199, 352, 202, Panel);
        centered("label-pause", 400, 237);
        const bool linked = wlan_.active();
        centered("label-resume", 400, linked ? 285 : 307);
        centered("label-quit", 400, linked ? 328 : 357);
        if (linked) centered("label-wlan-leave", 400, 371);
    }
    // ShineEffectView is appended to GameStage after its FightUI child.
    renderer_.shine(scene);
    if(round_->sourceTransitionVisible())renderer_.transition(transitionArt_,round_->sourceTransitionFrame());
    const auto& quick=round_->quickTransition();
    if(quick.active()){
        renderer_.rootOffset({});renderer_.box(0,quick.up(),800,300,0xff000000);renderer_.box(0,quick.down(),800,300,0xff000000);
    }
}

void Game::record() {
    recordSourceSceneQa(g_sceneGameQa,stats_.draws,round_?&round_->match().sourceScene():nullptr);
    recordSourcePaintQa(g_paintGameQa,stats_.draws,round_?&round_->match().sourceScene():nullptr);
    ComboGameQA combo{};combo.draws=stats_.draws;
    if(round_)for(unsigned side=0;side<2;++side){const auto& match=round_->match();const auto& ui=match.comboUi(side);
        combo.hits[side]=match.comboHits().count(side);combo.number[side]=ui.number();combo.frame[side]=ui.frame();
        combo.shown[side]=ui.shown();combo.visible[side]=ui.visible();combo.targets[side]=combo.hits[side]?1:0;}
    std::memcpy(reinterpret_cast<char*>(&g_comboGameQa)+sizeof(unsigned),reinterpret_cast<const char*>(&combo)+sizeof(unsigned),sizeof(combo)-sizeof(unsigned));
    asm volatile("" ::: "memory");g_comboGameQa.draws=combo.draws;
    HudGameQA hud{};hud.draws=stats_.draws;
    if(round_){const auto& model=round_->hud();hud.visible=model.visible();hud.sampled=model.sampled();
        for(unsigned i=0;i<2;++i){const auto& q=model.qi(i);hud.side[i]={float(model.hp(i).green()),float(model.hp(i).red()),float(model.energy(i).value()),
            float(q.value()),float(q.assist()),float(model.sample(i).hp),model.energy(i).frame(),q.visible(),q.readyVisible(),q.maxVisible(),q.readyFrame(),q.maxFrame()};}}
    std::memcpy(reinterpret_cast<char*>(&g_hudGameQa)+sizeof(unsigned),reinterpret_cast<const char*>(&hud)+sizeof(unsigned),sizeof(hud)-sizeof(unsigned));
    asm volatile("" ::: "memory");g_hudGameQa.draws=hud.draws;
    g_animationClockQa={};
    if(round_)if(const auto* clocks=round_->match().gameClocks())g_animationClockQa=clocks->view();
    SourceControlQA control{};control.draws=stats_.draws;
    if(round_)if(const auto* source=round_->match().sourceControl()){
        const auto& match=round_->match();control.ticks=round_->ticks();control.running=source->running();
        control.renderHit=source->renderHit();control.animations=source->animations().size();control.renders=source->renders().size();
        for(unsigned i=0;i<2;++i){control.actorAnimations[i]=match.aiAnimation(i);control.gravity[i]=match.actor(i).motion().gravity();}
    }
    std::memcpy(reinterpret_cast<char*>(const_cast<SourceControlQA*>(&g_sourceControlQa))+sizeof(unsigned),
        reinterpret_cast<const char*>(&control)+sizeof(unsigned),sizeof(control)-sizeof(unsigned));
    asm volatile("" ::: "memory");g_sourceControlQa.draws=control.draws;
    RoundControlQA roundControl{};roundControl.draws=stats_.draws;roundControl.displayedSeconds=-1;roundControl.bannerFrame=0xffffffff;
    if(round_){
        roundControl.ticks=round_->ticks();roundControl.clockFrame=round_->roundTimeFrame();roundControl.mainFrames=round_->sourceMainFrames();
        roundControl.actionEnabled=round_->match().sourceActionsEnabled();roundControl.displayedSeconds=round_->displayedSeconds();
        for(unsigned i=0;i<2;++i)roundControl.allowHp[i]=round_->match().actor(i).allowLoseHp();
        if(const auto banner=round_->bannerFrame())roundControl.bannerFrame=*banner;
    }
    std::memcpy(reinterpret_cast<char*>(const_cast<RoundControlQA*>(&g_roundControlQa))+sizeof(unsigned),
        reinterpret_cast<const char*>(&roundControl)+sizeof(unsigned),sizeof(roundControl)-sizeof(unsigned));
    asm volatile("" ::: "memory");g_roundControlQa.draws=roundControl.draws;
    StartEndControlQA lifecycle{};lifecycle.draws=stats_.draws;
    if(round_){const auto& start=round_->sourceStart();const auto& end=round_->sourceEnd();
        lifecycle.ticks=round_->ticks();lifecycle.startKind=unsigned(start.kind());lifecycle.startStep=start.step();lifecycle.startHold=start.hold();lifecycle.uiPlaying=start.uiPlaying();
        lifecycle.endActive=end.active();lifecycle.endStep=end.step();lifecycle.endHold=end.hold();
        lifecycle.transitionFrame=round_->sourceTransitionFrame();lifecycle.deferredPause=round_->sourceDeferredPause();lifecycle.transitionRendering=round_->sourceTransitionRendering();
    }
    std::memcpy(reinterpret_cast<char*>(const_cast<StartEndControlQA*>(&g_startEndControlQa))+sizeof(unsigned),
        reinterpret_cast<const char*>(&lifecycle)+sizeof(unsigned),sizeof(lifecycle)-sizeof(unsigned));
    asm volatile("" ::: "memory");g_startEndControlQa.draws=lifecycle.draws;
    RootControlQA root{};root.draws=stats_.draws;
    if(round_){root.ticks=round_->ticks();root.roots=round_->sourceRootFrames();root.visible=round_->sourceTransitionVisible();root.frame=round_->sourceTransitionFrame();root.rendering=round_->sourceTransitionRendering();
        const auto& quick=round_->quickTransition();root.quick=quick.active();root.quickStage=quick.stage();root.quickUp=quick.up();root.quickDown=quick.down();}
    root.overlayDraws=g_transitionRendererQa.draws;root.decodeFailures=g_transitionRendererQa.decodeFailures;root.residentBytes=transitionArt_.residentBytes();
    std::memcpy(reinterpret_cast<char*>(const_cast<RootControlQA*>(&g_rootControlQa))+sizeof(unsigned),reinterpret_cast<const char*>(&root)+sizeof(unsigned),sizeof(root)-sizeof(unsigned));
    asm volatile("" ::: "memory");g_rootControlQa.draws=root.draws;
    stats_.screen = static_cast<unsigned>(screen_);
    stats_.assistCutin = 0;
    stats_.teamMode = isTeamMode(mode_);
    stats_.mode = static_cast<unsigned>(mode_);
    stats_.arcadeStage = arcade_.stageIndex();
    stats_.arcadeScore = arcade_.score();
    stats_.arcadeFaction = arcadeFaction_;
    stats_.arcadeFoes = arcadeEncounter_ ? (isTeamMode(mode_) ? arcadeEncounter_->count : 1) : 0;
    stats_.arcadeContinue = continueArcade_;
    for (unsigned i = 0; i < 2; ++i) {
        stats_.fighters[i] = round_ ? active_[i] : selected(i);
        stats_.cells[i] = picks_[i].cell;
        stats_.alternate[i] = picks_[i].alternate;
        stats_.movesTarget[i] = stats_.hidesTarget[i] = 0;
        stats_.projectiles[i] = stats_.attackerFrame[i] = 0;
        stats_.attackerX[i] = stats_.attackerY[i] = 0;
        stats_.assistCells[i] = assistPicks_[i].cell;
        stats_.assistPicks[i] = round_ ? activeAssists_[i] : selected(i, true);
        stats_.assistCharge[i] = stats_.assistFrame[i] = 0;
        stats_.assistX[i] = stats_.assistY[i] = 0;
        stats_.members[i] = round_ ? round_->member(i) : 0;
        stats_.teamCount[i] = teamCount_[i];
        stats_.orderCount[i] = orderCount_[i];
        stats_.orderCursor[i] = orderCursor_[i];
        std::copy(teamPicks_[i].begin(), teamPicks_[i].end(), stats_.teamPicks[i]);
        std::copy(teamOrder_[i].begin(), teamOrder_[i].end(), stats_.teamOrder[i]);
    }
    stats_.map = map_;
    stats_.arena = arena_;
    stats_.training = mode_ == BattleMode::Training;
    stats_.historyRows = stats_.historyActions = 0;
    if (round_) {
        const auto history = round_->trainingHistory(0);
        stats_.historyRows = history.size();
        if (!history.empty()) stats_.historyActions = history.back().count;
        const auto& match = round_->match();
        stats_.ticks = round_->ticks();
        stats_.phase = static_cast<unsigned>(round_->phase());
        stats_.phaseTicks = round_->phaseTicks();
        stats_.round = round_->round();
        stats_.seconds = round_->seconds();
        stats_.winner = round_->winner();
        stats_.zoom = float(match.camera().zoom);
        stats_.cutin = 0;
        for (unsigned i = 0; i < 2; ++i) {
            const auto& actor = match.actor(i);
            stats_.wins[i] = round_->wins(i);
            stats_.x[i] = float(actor.motion().position.x);
            stats_.y[i] = float(actor.motion().position.y);
            stats_.hp[i] = float(actor.health());
            stats_.qi[i] = float(actor.qi());
            stats_.energy[i] = float(actor.energy());
            stats_.assistCharge[i] = actor.assistCharge();
            stats_.timeline[i] = actor.timelineFrame();
            stats_.forms[i] = actor.form();
            stats_.transforming[i] = actor.transforming();
            stats_.movesTarget[i] = actor.movingTarget();
            stats_.hidesTarget[i] = match.hidesTarget(i);
            if (!stats_.cutin) stats_.cutin = static_cast<unsigned>(actor.cutin().kind);
        }
        for (const auto& object : match.objects()) {
            if (const auto* shot = std::get_if<FightProjectile>(&object)) {
                ++stats_.projectiles[shot->owner()];
            } else {
                const auto& attacker = std::get<FightAttacker>(object);
                if (!attacker.active()) continue;
                const auto owner = attacker.owner();
                if (attacker.assister()) {
                    stats_.assistFrame[owner] = attacker.phase() + 1;
                    stats_.assistX[owner] = float(attacker.position().x);
                    stats_.assistY[owner] = float(attacker.position().y);
                    stats_.assistCutin |= attacker.cutin() != 0;
                    continue;
                }
                if (stats_.attackerFrame[owner]) continue;
                stats_.attackerFrame[owner] = attacker.phase() + 1;
                stats_.attackerX[owner] = float(attacker.position().x);
                stats_.attackerY[owner] = float(attacker.position().y);
            }
        }
    }
    g_versusQa = stats_;
    g_wlanQa = {unsigned(wlan_.status()), wlan_.frame(), unsigned(!wlan_.host()), wlan_.seed(), wlanOption_,
                {wlanReady_[0], wlanReady_[1]}, {wlanPads_[0].held, wlanPads_[1].held},
                {wlanPads_[0].pressed, wlanPads_[1].pressed}, wlan_.error()};
    g_creditsQa = {creditsPages_, creditsPage_, unsigned(creditsPages_ != 0), creditsEntries_};
    g_recoveryRuntimeQa = {};
    g_counterRuntimeQa = {};
    g_ghostRuntimeQa = {};
    g_settingsRuntimeQa={};
    g_settingsRuntimeQa.screen=unsigned(screen_);g_settingsRuntimeQa.row=screen_==Screen::Keys?keyRow_:settingsRow_;
    g_settingsRuntimeQa.profile=keyProfile_;g_settingsRuntimeQa.capture=keyCapture_;g_settingsRuntimeQa.serial=settingsStore_.serial();
    g_settingsRuntimeQa.loaded=settingsLoaded_;g_settingsRuntimeQa.saveFailed=settingsSaveFailed_;
    const unsigned settingValues[]{settings_.difficulty,settings_.mode,settings_.life,settings_.time,settings_.sound,settings_.music,settings_.quality,settings_.showHp};
    std::copy(std::begin(settingValues),std::end(settingValues),g_settingsRuntimeQa.values);
    for(unsigned p=0;p<2;++p)for(unsigned i=0;i<HumanKeyCount;++i)g_settingsRuntimeQa.keys[p][i]=settings_.keys[p].buttons[i];
    g_settingsRuntimeQa.level=round_?cpu_.level():settings_.difficulty;g_settingsRuntimeQa.hz=round_?round_->match().actor(0).gameHz():settings_.gameHz(wlan_.active());g_settingsRuntimeQa.classic=settings_.classic(wlan_.active());
    g_settingsRuntimeQa.rules=battleSettings().wireRules();
    if(round_)for(unsigned i=0;i<2;++i)g_settingsRuntimeQa.maxHp[i]=round_->match().actor(i).maxHealth();
    g_settingsRuntimeQa.bulk=keyBulk_;
    for(unsigned i=0;i<HumanKeyCount;++i)g_settingsRuntimeQa.editing[i]=editingKeys_.buttons[i];
    g_frameEffectsRuntimeQa = {};
    for (unsigned i = 0; i < 3; ++i)
        g_frameEffectsRuntimeQa.stepSounds[i] = audio_.requests(Sound(unsigned(Sound::Step1) + i));
    g_frameEffectsRuntimeQa.ghostFootSounds = audio_.requests(Sound::GhostFoot);
    if (round_) for (unsigned side = 0; side < 2; ++side) {
        const auto& actor = round_->match().actor(side);
        g_recoveryRuntimeQa.escaping[side] = actor.hurtEscaping();
        g_recoveryRuntimeQa.damage[side] = actor.currentHurtDamage();
        g_recoveryRuntimeQa.starts[side] = actor.hurtEscapes();
        g_recoveryRuntimeQa.hittable[side] = actor.aiHittable();
        g_counterRuntimeQa.kind[side] = unsigned(actor.counterKind());
        g_counterRuntimeQa.paused[side] = actor.counterPaused();
        g_counterRuntimeQa.starts[side] = actor.counterStarts();
        g_counterRuntimeQa.hitFrames[side] = actor.counterHitFrames();
        g_counterRuntimeQa.steelFrames[side] = actor.counterSteelFrames();
        g_counterRuntimeQa.replaceFrames[side] = actor.counterReplaceFrames();
        g_counterRuntimeQa.gameFrame[side] = actor.counterGameFrame();
        g_ghostRuntimeQa.kind[side] = unsigned(actor.ghostKind());
        g_ghostRuntimeQa.active[side] = actor.ghostActive();
        g_ghostRuntimeQa.frames[side] = actor.ghostFrames();
        g_ghostRuntimeQa.starts[side] = actor.ghostStarts();
        g_ghostRuntimeQa.ends[side] = actor.ghostEnds();
        g_ghostRuntimeQa.blue[side] = actor.blueShadow();
        g_ghostRuntimeQa.jumps[side] = actor.remainingJumps();
        const auto tint = actor.shadowTint();
        g_frameEffectsRuntimeQa.shadow[side] = actor.shadowing();
        g_frameEffectsRuntimeQa.tint[side][0] = tint.red;
        g_frameEffectsRuntimeQa.tint[side][1] = tint.green;
        g_frameEffectsRuntimeQa.tint[side][2] = tint.blue;
    }
    if (round_) g_ghostRuntimeQa.shadows = round_->match().scene().shadows.size();
    if (round_) {
        const auto scene = round_->match().scene();
        g_frameEffectsRuntimeQa.stamps = scene.shadows.size();
        for (const auto& shadow : scene.shadows)
            g_frameEffectsRuntimeQa.negativeStamps += shadow.tint.red < 0 || shadow.tint.green < 0 || shadow.tint.blue < 0;
        for (const auto& cue : scene.reactions) if (cue.cue == ReactionCue::GhostFoot) {
            ++g_frameEffectsRuntimeQa.footPuffs;
            g_frameEffectsRuntimeQa.puffFacing = cue.facing;
            g_frameEffectsRuntimeQa.puffFrame = cue.frame();
        }
    }
    g_cpuRuntimeQa = {cpuLevel_, aiGameHz_, {watchCpu_.decisions(), cpu_.decisions()},
                      {watchCpu_.intents(), cpu_.intents()},
                      {round_ ? int(round_->match().actor(0).aiState()) : -1,
                       round_ ? int(round_->match().actor(1).aiState()) : -1}};
    if(round_) {
        const auto& match=round_->match();const auto effects=match.scene();
        const auto b=effects.blur;
        const BlurGameQA blur{stats_.draws,float(b.x),float(b.y),float(b.slowRate),b.frames,b.slowFrames,unsigned(b.enabled),
            g_blurRendererQa.layers,g_blurRendererQa.tiles,g_blurRendererQa.missing,g_blurRendererQa.culled,0};
        std::memcpy(reinterpret_cast<char*>(const_cast<BlurGameQA*>(&g_blurGameQa))+sizeof(unsigned),
                    reinterpret_cast<const char*>(&blur)+sizeof(unsigned),sizeof(blur)-sizeof(unsigned));
        asm volatile("" ::: "memory");g_blurGameQa.draws=stats_.draws;
        const auto o=effects.optical;OpticalGameQA optical{};optical.draws=stats_.draws;
        optical.mask=o.mask;optical.counter=o.counter;optical.captures=o.captures;optical.children=stage_.optical().children().size();
        for(unsigned i=0;i<5;++i){optical.cachedMasks[i]=o.cached[i];if(o.cached[i]!=OpticalView::Missing)optical.cachedBits|=1u<<i;}
        optical.layers=g_opticalRendererQa.layers;optical.tiles=g_opticalRendererQa.tiles;optical.missing=g_opticalRendererQa.missing;
        optical.cachedDraws=g_opticalRendererQa.cached;optical.normalDraws=g_opticalRendererQa.normal;
        const auto opticalSprites=match.opticalSprites();
        for(unsigned i=0;i<2&&i<opticalSprites.size();++i){const auto a=opticalSprites[i].area;
            optical.actors[i]={float(a.x),float(a.y),float(a.width),float(a.height)};}
        std::memcpy(reinterpret_cast<char*>(const_cast<OpticalGameQA*>(&g_opticalGameQa))+sizeof(unsigned),
                    reinterpret_cast<const char*>(&optical)+sizeof(unsigned),sizeof(optical)-sizeof(unsigned));
        asm volatile("" ::: "memory");g_opticalGameQa.draws=stats_.draws;
        KoGameQA ko{};ko.draws=stats_.draws;ko.super=match.superKnockout();ko.backgrounds=effects.backgrounds.size();
        ko.hideMap=effects.hideMap;ko.fillBackdrop=effects.fillBackdrop;ko.freeze=match.hitFreezeFrames();
        ko.regularSound=audio_.requests(Sound::KO);ko.superSound=audio_.requests(Sound::KOBisha);ko.overHit=audio_.requests(Sound::OverHit);
        ko.shiftX=effects.shiftX;ko.shiftY=effects.shiftY;const auto camera=match.camera();
        ko.cameraX=camera.origin.x;ko.cameraY=camera.origin.y;ko.zoom=camera.zoom;
        if(!effects.backgrounds.empty()) {
            const auto& v=effects.backgrounds.back();ko.clip=v.art->clip;ko.frame=v.frame();ko.cursor=v.cursor;ko.hold=v.holdFrame;
        }
        std::memcpy(reinterpret_cast<char*>(const_cast<KoGameQA*>(&g_koGameQa))+sizeof(unsigned),
                    reinterpret_cast<const char*>(&ko)+sizeof(unsigned),sizeof(ko)-sizeof(unsigned));
        asm volatile("" ::: "memory");g_koGameQa.draws=stats_.draws;
        FloorGameQA floor{};floor.draws=stats_.draws;
        constexpr std::string_view floorIds[]{"hit_floor","hit_floor_low","hit_floor_heavy","hit_floor_yan"};
        for(const auto& view:effects.combat)for(unsigned i=0;i<4;++i)if(view.effect->id==floorIds[i]) {
            ++floor.live;floor.mask|=1U<<i;
            floor.views[i]={view.art->clip,view.frame(),float(view.origin.x),float(view.origin.y),view.facing};
        }
        std::memcpy(reinterpret_cast<char*>(const_cast<FloorGameQA*>(&g_floorGameQa))+sizeof(unsigned),
                    reinterpret_cast<const char*>(&floor)+sizeof(unsigned),sizeof(floor)-sizeof(unsigned));
        asm volatile("" ::: "memory");g_floorGameQa.draws=stats_.draws;
        BuffGameQA buff{};buff.draws=stats_.draws;buff.live=effects.combat.size();buff.target=0xffffffff;
        for(unsigned side=0;side<2;++side)for(unsigned kind=0;kind<3;++kind) {
            const auto& state=match.actor(side).buffState(FighterBuff(kind));
            if(state) {
                buff.values[side][kind]=float(state->value);
                buff.remaining[side][kind]=state->frames;
                buff.active[side][kind]=!state->finished;
            }
        }
        for(const auto& view:effects.combat) {
            buff.bursts+=view.effect->id.starts_with("buff_effect_");
            if(!view.effect->buff)continue;
            if(!buff.auras++) {
                buff.clip=view.art->clip;buff.frame=view.frame();buff.cursor=view.cursor;
                buff.x=float(view.origin.x);buff.y=float(view.origin.y);
                for(unsigned side=0;side<2;++side)if(view.target==match.actor(side).effectTarget())buff.target=side;
            }
        }
        // Publish the draw stamp last so a debugger read can reject a torn copy.
        std::memcpy(reinterpret_cast<char*>(const_cast<BuffGameQA*>(&g_buffGameQa))+sizeof(unsigned),
                    reinterpret_cast<const char*>(&buff)+sizeof(unsigned),sizeof(buff)-sizeof(unsigned));
        asm volatile("" ::: "memory");g_buffGameQa.draws=stats_.draws;
        g_combatGameQa.emitted=match.emittedEffects();g_combatGameQa.live=effects.combat.size();
        g_combatGameQa.freezeFrames=match.hitFreezeFrames();
        unsigned sounds=0;
        for(unsigned i=unsigned(Sound::SourceFirst);i<unsigned(Sound::Count);++i)sounds+=audio_.requests(Sound(i));
        g_combatGameQa.sounds=sounds;
        if(!effects.combat.empty()) {
            const auto& effect=effects.combat.back();
            g_combatGameQa.lastEffect=unsigned(effect.effect-combatEffects().data())+1;
            g_combatGameQa.x=effect.origin.x;g_combatGameQa.y=effect.origin.y;g_combatGameQa.facing=effect.facing;
            g_combatGameQa.rotation=effect.rotation;g_combatGameQa.frame=effect.frame();
        }
        asm volatile("" ::: "memory");
        g_combatGameQa.draws=stats_.draws;
        g_statusGameQa.live=effects.combat.size();
        for(unsigned side=0;side<2;++side){
            const auto tint=match.actor(side).visualTint();
            g_statusGameQa.tint[side][0]=tint.red;g_statusGameQa.tint[side][1]=tint.green;g_statusGameQa.tint[side][2]=tint.blue;
        }
        unsigned special=0,finished=0;
        for(const auto& effect:effects.combat)if(effect.effect->special){
            ++special;finished+=effect.finished;
            g_statusGameQa.clip=effect.art->clip;g_statusGameQa.frame=effect.frame();g_statusGameQa.cursor=effect.cursor;
            g_statusGameQa.x=effect.origin.x;g_statusGameQa.y=effect.origin.y;
        }
        g_statusGameQa.special=special;g_statusGameQa.finished=finished;
        asm volatile("" ::: "memory");g_statusGameQa.draws=stats_.draws;
    }
}

void Game::draw() {
    renderer_.begin();
    if (round_) {
        drawFight();
    } else {
        renderer_.beginUi();
        switch (screen_) {
        case Screen::Title:
            drawTitle();
#ifdef BVN_RESOURCE_DIAGNOSTICS
            renderer_.diagnosticText(resourceDiagnostics.synchronousOnly ? "HD 1.0.5: STDIO ONLY" : "HD 1.0.5: ORIGINAL IO", 2, 2);
            renderer_.diagnosticText(renderer_.deferred() ? "GE: QUEUED FRAME" : "GE: ORIGINAL DIRECT", 2, 12);
#endif
            break;
        case Screen::Credits: drawCredits(); break;
        case Screen::Settings:
        case Screen::Keys:drawSettings();break;
        case Screen::WlanMenu:
        case Screen::WlanRoom: drawWlanMenu(); break;
        case Screen::FirstFighter:
        case Screen::SecondFighter:
        case Screen::FirstAssist:
        case Screen::SecondAssist: drawFighters(); break;
        case Screen::Stage: drawMap(); break;
        case Screen::FirstOrder:
        case Screen::SecondOrder: drawOrder(); break;
        case Screen::ArcadeWinner:
        case Screen::ArcadeContinue:
        case Screen::ArcadeOver:
        case Screen::ArcadeClear:
        case Screen::ArcadeMissing: drawArcadeResult(); break;
        case Screen::LoadError:
            drawMap();
#ifdef BVN_RESOURCE_DIAGNOSTICS
            renderer_.box(193, 203, 414, 216, Panel);
#else
            renderer_.box(193, 203, 414, 188, Panel);
#endif
            centered("label-load-error", 400, 269);
#ifdef BVN_RESOURCE_DIAGNOSTICS
            {
                const char* name = diagnosticFileName();
                renderer_.diagnosticText(name, (800 * StageTextureScale - std::strlen(name) * 8) / 2, 140);
                char code[40];
                std::snprintf(code, sizeof(code), "ERR=%d  %s", resourceDiagnostics.error, resourceDiagnostics.operation);
                renderer_.diagnosticText(code, (800 * StageTextureScale - std::strlen(code) * 8) / 2, 151);
            }
            {
                const char* status = resourceDiagnostics.integrity == 1 ? "FILE CRC OK" :
                    resourceDiagnostics.integrity == 2 ? "FILE CRC DIFFERENT" : "FILE CHECK FAILED";
                renderer_.diagnosticText(status, (800 * StageTextureScale - std::strlen(status) * 8) / 2, 162);
            }
            centered("label-confirm", 400, 399);
#else
            centered("label-confirm", 400, 332);
#endif
            break;
        default: break;
        }
    }
    renderer_.end();
#ifdef BVN_RESOURCE_DIAGNOSTICS
    recordResourceDiagnostics();
#endif
    ++stats_.draws;
    record();
}

#ifdef BVN_LIVE_RENDER_QA
#include "../tests/live_render_probe.inc"
#endif

int exitGame(int, int, void*) {
    sceKernelExitGame();
    return 0;
}

int callbacks(SceSize, void*) {
    const int callback = sceKernelCreateCallback("BVN exit", exitGame, nullptr);
    if (callback >= 0) sceKernelRegisterExitCallback(callback);
    sceKernelSleepThreadCB();
    return 0;
}

} // namespace
} // namespace bvn

int main(int argc, char** argv) {
    const std::string launch = argc ? argv[0] : "";
    const auto slash = launch.find_last_of('/');
    const int thread = sceKernelCreateThread("BVN callbacks", bvn::callbacks, 17, 0x1000, 0, nullptr);
    if (thread >= 0) sceKernelStartThread(thread, 0, nullptr);
    scePowerSetClockFrequency(333, 333, 166);
#ifdef BVN_RESOURCE_DIAGNOSTICS
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    SceCtrlData startupPad{};
    sceCtrlReadBufferPositive(&startupPad, 1);
    const auto diagnosticFolder = slash == std::string::npos ? std::string{} : launch.substr(0, slash + 1);
    SceIoStat syncStat{};
    bvn::resourceDiagnostics.synchronousOnly = (startupPad.Buttons & PSP_CTRL_RTRIGGER) ||
        sceIoGetstat((diagnosticFolder + "BVN_SYNC.IO").c_str(), &syncStat) == 0;
#endif
    const auto launchFolder = slash == std::string::npos ? std::string{} : launch.substr(0, slash + 1);
#ifdef BVN_LIVE_RENDER_QA
    bvn::resourceDiagnostics.synchronousOnly = true;
    static bvn::Game game(launchFolder + "../BVN37/");
#else
    static bvn::Game game(launchFolder);
#ifdef BVN_RESOURCE_DIAGNOSTICS
    game.deferred(!(startupPad.Buttons & PSP_CTRL_LTRIGGER));
#endif
#endif
    {
        const auto path=launchFolder+"BVN_HWD3.LOG";
        const int boot=sceIoOpen(path.c_str(),PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
        if(boot>=0){
            char line[384];const int n=std::snprintf(line,sizeof(line),"BVN37 1.0.5 HWD3 HD\nGE=%s\nfirmware=0x%08x\nlaunch=%s\n",game.deferred()?"queued-frame":"original-direct",sceKernelDevkitVersion(),launch.c_str());
            if(n>0)sceIoWrite(boot,line,std::min(n,int(sizeof(line)-1)));
            sceIoClose(boot);sceIoSync(launchFolder.starts_with("ef0:")?"ef0:":"ms0:",0);
        }
    }
    if (!game.start()) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("Missing or invalid game data. Copy the full game folder.\n");
        sceKernelSleepThread();
        return 1;
    }
#ifdef BVN_LIVE_RENDER_QA
    g_liveRenderQa.status = 1;
    bool complete=false;
    try { complete=game.liveRenderProbe(launchFolder); } catch (...) {g_liveRenderQa.error=99;}
    g_liveRenderQa.status=complete?2:3;
    pspDebugScreenInit();
    pspDebugScreenPrintf("BVN37 LIVE RENDER CHECK\n\nREPORT SAVED\n\nPress X to exit and reconnect card.\n");
    for(;;){SceCtrlData pad{};sceCtrlReadBufferPositive(&pad,1);if(pad.Buttons&PSP_CTRL_CROSS)break;sceDisplayWaitVblankStart();}
    sceKernelExitGame();return 0;
#endif
    unsigned previous = 0;
    unsigned previousVblank = sceDisplayGetVcount();
    for (;;) {
        game.draw();
        const unsigned vblank = sceDisplayGetVcount();
        SceCtrlData pads[4]{};
        const unsigned elapsed = std::clamp(vblank - previousVblank, 1U, unsigned(std::size(pads)));
        previousVblank = vblank;
        // The renderer already waits for refresh. Read the collected samples without waiting again.
        const int count = sceCtrlPeekBufferPositive(pads, elapsed);
        for (int i = 0; i < count; ++i) {
            const unsigned held = pads[i].Buttons;
            game.step(held, held & ~previous);
            previous = held;
        }
    }
}
