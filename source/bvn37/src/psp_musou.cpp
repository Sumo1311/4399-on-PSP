// Adapted from 5DPLAY Game Studio's Musou WorldMapStage and GameUI.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "psp_musou.hpp"
#include "source_scene_qa.hpp"
#include "source_paint_qa.hpp"
#include "game_settings.hpp"
#include "musou_cue_data.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>

extern "C" {
bvn::SourceSceneQa g_musouSceneQa;
bvn::SourcePaintQa g_musouPaintQa;
bvn::AnimationClockView g_musouAnimationClockQa;
struct MusouComboQA {unsigned draws,hits,number,frame,shown,visible,targets;}g_musouComboQa;
struct MusouLifecycleQA {
    unsigned draws,revision,result,complete,finished,passRequested,returnSerial,endListeners;
    unsigned clearRecorded,clearSaved,saveFailed,activeFades;
    float firstFadeAlpha;
}g_musouLifecycleQa;
volatile struct MusouPresentationQA {
    unsigned draws,ticks,roots,frame,kind,starting,ending,requested,leaveReady,koListeners,completions,timeoutRoots,closingFrame,closingVisible,closingRendering,allowPause,stripFrame;
} g_musouPresentationQa;
volatile struct MusouControlQA {
    unsigned draws,ticks,running,renderHit,animations,renders,replaceFrame,replaceHold,explodeFrame,retained;
    unsigned bossLevel,bossDecisions,bossIntents;
    float gravity;
} g_musouControlQa;
volatile struct MusouQA {
    unsigned screen, draws, ticks, wave, kills, spawned, time, mission, area, leader, money, enemyCount;
    float x, y, hp, qi, maxHp;
    unsigned facing, action, result, level, stars;
    struct Enemy { float x,y,hp; unsigned id, action, group; } foes[64];
    unsigned saveSerial, levels[3], experience[3];
    unsigned bossCue, bossCueTicks, bossId, bossPose;
    float bossHp, bossMaxHp, zoom;
    unsigned team[3], owned, teamSlot, recruit, recruitLevel, recruitPrice, notice;
} g_musouQa;
volatile struct MusouCombatQA {
    unsigned draws, live, freeze, paused, enabled, emitted;
    int playerTint[3];
    unsigned playerState, count;
    struct Tint { unsigned id; int red, green, blue; } foes[64];
    struct View { unsigned effect, clip, frame, cursor, finished, target; float x, y; int facing; } views[64];
} g_musouCombatQa;
}
static_assert(offsetof(MusouQA, foes) == 88);
static_assert(offsetof(MusouQA, team) == 1680);
static_assert(sizeof(MusouControlQA)==56);

namespace bvn {
namespace {

constexpr unsigned ColorPanel = 0xe5231826;
constexpr unsigned ShopColumns = 5;
constexpr unsigned ShopPageSize = 25;

std::string saveSlot(const std::string& folder, unsigned slot) {
    return folder + (slot ? "/MUSOU1.DAT" : "/MUSOU0.DAT");
}

void art(StageRenderer& renderer, const UiAsset& ui, const char* key, float x, float y, float w, float h) {
    renderer.image(ui,key,x,y,w,h);
}

void portrait(StageRenderer& renderer, const UiAsset& common, unsigned roster, const char* kind,
              float x, float y, float w, float h) {
    char key[32];
    std::snprintf(key, sizeof(key), "fighter-%s-%u", kind, roster);
    art(renderer, common, key, x, y, w, h);
}

void fighterName(StageRenderer& renderer, const UiAsset& common, unsigned roster, float x, float y, float limit) {
    char key[32];
    std::snprintf(key, sizeof(key), "fighter-name-%u", roster);
    const auto* image = common.find(key);
    if (!image) return;
    const float width = std::min(limit, image->width * 1.3f / float(StageTextureScale));
    const float height = width * image->height / image->width;
    art(renderer, common, key, x - width / 2, y - height / 2, width, height);
}

void number(StageRenderer& renderer, const UiAsset& ui, unsigned value, float x, float y, float height = 25) {
    char text[12];
    std::snprintf(text,sizeof(text),"%u",value);
    for (char digit : std::string_view(text)) {
        char name[] = "time-0";
        name[5] = digit;
        art(renderer,ui,name,x,y,height*.68f,height);
        x += height*.7f;
    }
}

void gauge(StageRenderer& renderer, double fraction, float x, float y, float w, float h, unsigned color) {
    renderer.box(x,y,w,h,0xe01b1720);
    renderer.box(x+1,y+1,float(std::clamp(fraction,0.0,1.0))*(w-2),h-2,color);
}

} // namespace

bool PspMusou::open(std::string folder, const RosterAsset& roster) {
    folder_ = std::move(folder);
    if (!campaign_.open((folder_+"MUSOU.BVM").c_str()) || !ui_.open((folder_+"MUSOUUI.BVU").c_str()) ||
        !party_.open((folder_+"MUSOUSHOP.BVM").c_str(), roster)) return false;
    const std::unique_ptr<FILE,decltype(&std::fclose)> points(std::fopen((folder_+"MUSOU.PTS").c_str(),"rb"),std::fclose);
    char magic[8];
    if (!points || std::fread(magic,1,8,points.get())!=8 || std::memcmp(magic,"BVNMAP01",8)) return false;
    for (const auto& area : campaign_.areas()) {
        (void)area;
        float xy[2];
        if (std::fread(xy,sizeof(float),2,points.get())!=2 ||
            !(xy[0]>=0 && xy[0]<=1024 && xy[1]>=0 && xy[1]<=1024)) return false;
        markers_.push_back({xy[0],xy[1]});
    }
    if (std::fgetc(points.get()) != EOF || std::ferror(points.get())) return false;
    const std::unique_ptr<FILE,decltype(&std::fclose)> catalog(std::fopen((folder_+"MUSOUACT.TXT").c_str(),"r"),std::fclose);
    if (!catalog) return false;
    char name[32], file[16];
    unsigned hp;
    while (std::fscanf(catalog.get(),"%31s %15s %u",name,file,&hp)==3) {
        const std::string_view path(file);
        if (!hp || path.size()<5 || !path.ends_with(".BVF") || enemyFiles_.size()>=256 ||
            !std::ranges::all_of(path.substr(0,path.size()-4),[](char c){return (c>='A'&&c<='Z')||(c>='0'&&c<='9');}) ||
            std::ranges::find(enemyFiles_,std::string_view(name),&EnemyFile::name)!=enemyFiles_.end()) return false;
        enemyFiles_.push_back({name,file,hp});
    }
    if (!std::feof(catalog.get()) || std::ferror(catalog.get()) || enemyFiles_.empty()) return false;
    roster_ = &roster;
    progress_.emplace(campaign_);
    const char* drive = folder_.starts_with("ef0:") ? "ef0:" : "ms0:";
    saveFolder_ = std::string(drive) + "/PSP/SAVEDATA/BVN37M001";
    loadProgress();
    sprites_.reserve(96);
    return true;
}

void PspMusou::loadProgress() {
    for (unsigned slot = 0; slot < 2; ++slot)
        party_.loadSave(saveSlot(saveFolder_, slot).c_str(), campaign_, *progress_, slot, saveSerial_, area_);
}

bool PspMusou::saveProgress(const MusouParty& party, unsigned money) {
    if (saveSerial_ == std::numeric_limits<std::uint32_t>::max()) return false;
    const auto parent = saveFolder_.substr(0, saveFolder_.find_last_of('/'));
    sceIoMkdir(parent.substr(0, parent.find_last_of('/')).c_str(), 0777);
    sceIoMkdir(parent.c_str(), 0777);
    sceIoMkdir(saveFolder_.c_str(), 0777);
    const auto serial = saveSerial_ + 1;
    if (!party.writeSave(saveSlot(saveFolder_, serial & 1).c_str(), campaign_, *progress_, money, serial, area_))
        return false;
    saveSerial_ = serial;
    return true;
}

bool PspMusou::loadTeam() {
    members_ = party_.battleMembers();
    for (unsigned slot = 0; slot < party_.team().size(); ++slot) {
        const auto& recruit = party_.recruits()[party_.team()[slot]];
        const auto* file = roster_->fighter(recruit.roster).file;
        if (!loadBank(recruit.id, file, FighterMaxHealth, teamAssets_[slot], teamBanks_[slot])) return false;
    }
    return true;
}

bool PspMusou::commitParty(MusouParty next, unsigned cost) {
    if (cost > progress_->money()) {
        notice_ = Notice::Coins;
        return false;
    }
    if (!saveProgress(next, progress_->money() - cost)) {
        notice_ = Notice::Save;
        return false;
    }
    progress_->spend(cost);
    party_ = std::move(next);
    return true;
}

void PspMusou::openShop() {
    shop_.clear();
    for (unsigned i = 0; i < party_.recruits().size(); ++i) {
        if (std::ranges::find(party_.team(), i) == party_.team().end()) shop_.push_back(i);
    }
    std::stable_partition(shop_.begin(), shop_.end(), [&](unsigned i) { return party_.member(i) != nullptr; });
    shopIndex_ = 0;
    screen_ = Screen::Shop;
}

void PspMusou::stepShop(unsigned pressed, bool confirm, bool back) {
    if (back) {
        screen_ = Screen::Team;
        return;
    }
    if (shop_.empty()) return;
    int change = 0;
    if (pressed & PSP_CTRL_LEFT) change = -1;
    else if (pressed & PSP_CTRL_RIGHT) change = 1;
    else if (pressed & PSP_CTRL_UP) change = -int(ShopColumns);
    else if (pressed & PSP_CTRL_DOWN) change = ShopColumns;
    else if (pressed & PSP_CTRL_LTRIGGER) change = -int(ShopPageSize);
    else if (pressed & PSP_CTRL_RTRIGGER) change = ShopPageSize;
    shopIndex_ = std::clamp(int(shopIndex_) + change, 0, int(shop_.size()) - 1);
    if (!confirm) return;
    const unsigned recruit = shop_[shopIndex_];
    if (!party_.available(recruit)) {
        notice_ = Notice::Asset;
    } else if (party_.member(recruit)) {
        auto next = party_;
        if (next.replace(teamSlot_, recruit) && commitParty(std::move(next))) screen_ = Screen::Team;
    } else if (progress_->money() < party_.recruits()[recruit].price) {
        notice_ = Notice::Coins;
    } else {
        screen_ = Screen::Buy;
    }
}

bool PspMusou::loadBank(std::string_view name, const char* path, unsigned hp,
                        std::vector<FighterAsset>& assets, MusouFighterBank& bank) {
    constexpr auto residency = FighterAsset::TextureResidency::LowMemory;
    unsigned chosen = 0;
    while (chosen < roster_->size() && roster_->fighter(chosen).file != std::string_view(path)) ++chosen;
    if (chosen == roster_->size()) {
        assets.resize(1);
        if (!assets.front().open((folder_ + path).c_str(), residency)||!assets.front().hasObjectAreas()) return false;
        bank = {name, assets, 0, hp};
        return true;
    }
    const auto& fighter = roster_->fighter(chosen);
    const auto forms = roster_->forms(chosen);
    assets.resize(forms.size());
    for (unsigned i = 0; i < forms.size(); ++i) {
        if (forms[i] != MissingFighter && (!assets[i].open((folder_ + roster_->fighter(forms[i]).file).c_str(), residency)||!assets[i].hasObjectAreas()))
            return false;
    }
    bank = {name, assets, fighter.form, hp, fighter.faction, chosen};
    return true;
}

#ifdef BVN_MUSOU_QA
void PspMusou::loadReplay() {
    replay_.clear();
    replayCursor_ = replayHeld_ = 0;
    const std::unique_ptr<FILE,decltype(&std::fclose)> file(
        std::fopen((folder_ + "MUSOURPL.BIN").c_str(), "rb"), std::fclose);
    char magic[8];
    if (!file || std::fread(magic, 1, 8, file.get()) != 8 ||
        std::memcmp(magic, "BVNRPL01", 8) || std::fseek(file.get(), 0, SEEK_END)) return;
    const auto bytes = std::ftell(file.get()) - 8;
    if (bytes <= 0 || bytes > 240000 || bytes % 2 || std::fseek(file.get(), 8, SEEK_SET)) return;
    replay_.resize(bytes / 2);
    if (std::fread(replay_.data(), 2, replay_.size(), file.get()) != replay_.size()) replay_.clear();
}

void PspMusou::replayPad(unsigned& held, unsigned& pressed) {
    if (screen_ != Screen::Fight || replayCursor_ >= replay_.size() || (pressed & PSP_CTRL_START)) return;
    held = replay_[replayCursor_++];
    pressed = held & ~replayHeld_;
    replayHeld_ = held;
}
#endif

bool PspMusou::enter(PspAudio& audio, StageRenderer& renderer) {
    const auto chosen=progress_->nextMission(area_);
    if (!chosen) { screen_=Screen::Locked; return false; }
    mission_=*chosen;
    const auto& mission=campaign_.missions()[mission_];
    std::vector<const EnemyFile*> files;
    for (const auto& wave : campaign_.waves().subspan(mission.firstWave,mission.waveCount)) {
        for (unsigned i=wave.firstEnemy;i<unsigned(wave.firstRepeat)+wave.repeatCount;++i) {
            const auto& enemy=campaign_.enemies()[i];
            const auto file=std::ranges::find(enemyFiles_,std::string_view(enemy.fighter),&EnemyFile::name);
            if (file==enemyFiles_.end()) { screen_=Screen::Missing; return false; }
            if (std::ranges::find(files,&*file)==files.end()) files.push_back(&*file);
        }
    }
    renderer.clearFighterTextures();
    screen_=Screen::Error;
    if (!stage_.open((folder_+mission.map+".BVN").c_str(),true) ||
        !mapBlurArt_.open((folder_+mission.map+".BVB").c_str(),FighterAsset::TextureResidency::LowMemory)) return false;
    mapOpticalArt_={};
    if(!stage_.optical().children().empty()&&
       (!mapOpticalArt_.open((folder_+mission.map+".BVO").c_str(),FighterAsset::TextureResidency::LowMemory)||
        !mapOpticalArt_.has(MoveClip(9))||mapOpticalArt_.clip(MoveClip(9)).count!=stage_.optical().paletteFrames()))return false;
    stage_.useOpticalPalette();
    if(!loadTeam())return false;
    enemyAssets_.resize(files.size());
    enemyBanks_.resize(files.size());
    for (unsigned i=0;i<files.size();++i) {
        const auto& file = *files[i];
        if (!loadBank(file.name,file.path.c_str(),file.health,enemyAssets_[i],enemyBanks_[i])) return false;
    }
#ifdef BVN_MUSOU_QA
    const std::uint32_t seed=0x31415926;
#else
    const std::uint32_t seed=sceKernelGetSystemTimeLow();
#endif
    battle_.emplace();
    if (!battle_->start(campaign_,mission_,stage_,teamBanks_,enemyBanks_,*progress_,members_,seed)) {
        battle_.reset();
        return false;
    }
#ifdef BVN_MUSOU_QA
    loadReplay();
#endif
    battle_->setSourceProfile(gameHz_,shineHz_);
    screen_=Screen::Start;
    clearRecorded_ = clearSaved_ = saveFailed_ = false;
    std::string theme = std::string("map-") + mission.map;
    std::ranges::transform(theme, theme.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    audio.music(theme.c_str());
    return true;
}

void PspMusou::leave(StageRenderer& renderer, PspAudio& audio) {
    renderer.clearFighterTextures();
    sprites_.clear();
    if (battle_) party_.storeBattleMembers(members_);
    battle_.reset();
    for (auto& bank : teamAssets_) bank.clear();
    enemyBanks_.clear();
    enemyAssets_.clear();
    stage_={};
    mapBlurArt_={};
    mapOpticalArt_={};
    screen_=Screen::World;
    audio.music("op");
}

bool PspMusou::step(unsigned pressed, FighterControls input, PspAudio& audio, StageRenderer& renderer, unsigned held) {
    const bool confirm=pressed&(PSP_CTRL_CROSS|PSP_CTRL_START);
    const bool back=pressed&PSP_CTRL_CIRCLE;
    if (notice_ != Notice::None) {
        if (confirm || back) notice_ = Notice::None;
        return true;
    }
    switch (screen_) {
    case Screen::World:
        if (back) { leave(renderer,audio); return false; }
        if (pressed&(PSP_CTRL_LEFT|PSP_CTRL_UP)) area_=(area_+markers_.size()-1)%markers_.size();
        if (pressed&(PSP_CTRL_RIGHT|PSP_CTRL_DOWN)) area_=(area_+1)%markers_.size();
        if (pressed & PSP_CTRL_TRIANGLE) {
            teamSlot_ = 0;
            screen_ = Screen::Team;
        } else if (confirm) {
            enter(audio,renderer);
        }
        break;
    case Screen::Team:
        if (back) { screen_ = Screen::World; break; }
        if (pressed & PSP_CTRL_LEFT) teamSlot_ = (teamSlot_ + 2) % 3;
        if (pressed & PSP_CTRL_RIGHT) teamSlot_ = (teamSlot_ + 1) % 3;
        if (pressed & PSP_CTRL_TRIANGLE) {
            auto next = party_;
            if (next.setLeader(teamSlot_) && commitParty(std::move(next))) teamSlot_ = 0;
        } else if (confirm) {
            openShop();
        }
        break;
    case Screen::Shop:
        stepShop(pressed, confirm, back);
        break;
    case Screen::Buy:
        if (back) screen_ = Screen::Shop;
        else if (confirm) {
            const auto recruit = shop_[shopIndex_];
            auto next = party_;
            if (next.acquire(recruit) && commitParty(std::move(next), party_.recruits()[recruit].price))
                screen_ = Screen::Shop;
        }
        break;
    case Screen::Start:
    case Screen::Result:
    case Screen::Fight: {
        if(saveFailed_){
            battle_->pausedDisplayFrame();
            if(back){leave(renderer,audio);break;}
            if(confirm)saveFailed_=false;
            returnToWorld(renderer,audio);break;
        }
        if((pressed&PSP_CTRL_START)&&battle_->allowUiPause()){
            battle_->stopInput();battle_->manualPause();battle_->pausedDisplayFrame();
            sourceSignals(audio);screen_=Screen::Pause;returnToWorld(renderer,audio);break;
        }
        bool switchPressed=(pressed&PSP_CTRL_TRIANGLE)&&!(held&PSP_CTRL_LTRIGGER);
        if(input.sourceHuman) {
            const bool dashHeld=input.heldKeys&(1U<<unsigned(HumanKey::Dash));
            input.assistHeld=(input.heldKeys&(1U<<unsigned(HumanKey::Assist)))&&dashHeld;
            switchPressed=(input.pressedKeys&(1U<<unsigned(HumanKey::Assist)))&&!dashHeld;
        } else input.assistHeld=(held&PSP_CTRL_TRIANGLE)&&(held&PSP_CTRL_LTRIGGER);
        const auto previous=battle_->bossCue();
        if(battle_->step(input,switchPressed)){
            const auto cue=battle_->bossCue();
            if(cue.kind==MusouBossCue::Kind::Entrance&&previous.kind!=cue.kind)audio.music("");
            if(cue.kind==MusouBossCue::Kind::Knockout&&previous.kind!=cue.kind)audio.play(Sound::OverHit);
            sourceSignals(audio);
            for(auto n=battle_->takeRushSounds();n;--n)audio.play(Sound::Rush);
            for(auto n=battle_->takeGhostStepSounds();n;--n)audio.play(Sound::GhostStep);
            for(const auto name:battle_->takeCombatSounds())audio.sourceEffect(name);
            audio.footsteps(battle_->takeWalkSounds(),true);
            audio.ghostFootsteps(battle_->takeGhostFootSounds(),true);
            const auto counter=battle_->takeCounterSounds();
            if(counter&1)audio.play(Sound::CounterReplace);
            if(counter&2)audio.play(Sound::CounterCharge);
            if(counter&4)audio.play(Sound::CounterBurst);
        }
        if(battle_->wave().result()!=MusouResult::Playing){
            if(screen_!=Screen::Result&&battle_->wave().result()==MusouResult::Won)audio.music("win");
            screen_=Screen::Result;
        }else screen_=battle_->presentation().starting()?Screen::Start:Screen::Fight;
        returnToWorld(renderer,audio);break;
    }
    case Screen::Pause:
        battle_->pausedDisplayFrame();sourceSignals(audio);
        if(back)leave(renderer,audio);
        else if(pressed&PSP_CTRL_START){
            battle_->stopInput();battle_->manualResume();
            screen_=battle_->presentation().ending()?Screen::Result:Screen::Fight;
        }
        if(battle_)returnToWorld(renderer,audio);
        break;
    case Screen::Locked:
    case Screen::Missing:
    case Screen::Error:
        if (confirm || back) leave(renderer,audio);
        break;
    }
    return true;
}

void PspMusou::sourceSignals(PspAudio& audio){
    const auto signals=battle_->takeSourceSignals();
    if(signals&SourceMusouPresentation::Fight)audio.play(Sound::Fight);
    if(signals&SourceMusouPresentation::Warning)audio.play(Sound::BossWarning);
    if(signals&SourceMusouPresentation::Ko)audio.play(battle_->bossCue().super?Sound::KOBisha:Sound::KO);
    if(signals&SourceMusouPresentation::BossMusic)
        if(const auto* boss=battle_->boss())audio.music(boss->bank->faction==1?"boss-naruto":"boss-bleach");
}

void PspMusou::returnToWorld(StageRenderer& renderer,PspAudio& audio){
    if(battle_->presentation().returnRequested()&&!saveFailed_){
        if(battle_->presentation().passRequested()&&!clearRecorded_){
            clearRecorded_=progress_->passMission(area_,mission_);
            party_.storeBattleMembers(members_);
            if(!clearRecorded_){saveFailed_=true;return;}
        }
        if(battle_->presentation().passRequested()&&!clearSaved_){
            if(!saveProgress(party_,progress_->money())){saveFailed_=true;return;}
            clearSaved_=true;
        }
        battle_->acknowledgeReturn();
    }
    if(!saveFailed_&&battle_->leaveReady())leave(renderer,audio);
}

void PspMusou::label(StageRenderer& renderer,const char* key,float x,float y) const {
    const auto* image=ui_.find(key);
    if (!image) return;
    const float width=image->width/StageTextureScale, height=image->height/StageTextureScale;
    art(renderer,ui_,key,x-width/2,y-height/2,width,height);
}

void PspMusou::titleLabel(StageRenderer& renderer,float y) const {
    label(renderer,"musou",400,y);
}

void PspMusou::drawWorld(StageRenderer& renderer,const UiAsset& common) const {
    constexpr float size=1536, zoom=1.5f;
    const auto focus=markers_[area_];
    const float left=std::clamp(400.f-float(focus.x*zoom),800-size,0.f);
    const float top=std::clamp(300.f-float(focus.y*zoom),600-size,0.f);
    art(renderer,ui_,"world",left,top,size,size);
    for (unsigned i=0;i<markers_.size();++i) {
        const auto point=markers_[i];
        const float x=left+point.x*zoom,y=top+point.y*zoom;
        if (i==area_) renderer.box(x-30,y-30,60,60,0x9cf9b049);
        if (progress_->areaOpen(i)) {
            const auto& area=campaign_.areas()[i];
            const bool cleared=progress_->stars()[area.firstMission]!=0;
            art(renderer,ui_,cleared?"point-done":"point",x-20,y-22,40,44);
        } else renderer.box(x-5,y-5,10,10,0xbb4e4a60);
    }
    renderer.box(0,0,800,57,ColorPanel);
    renderer.box(0,550,800,50,ColorPanel);
    label(renderer,"musou",400,28);
    label(renderer,"money",40,28);
    number(renderer,common,progress_->money(),77,14);
    label(renderer,"world-control",400,574);
    if (const auto mission=progress_->nextMission(area_)) {
        label(renderer,"mission",680,28);
        number(renderer,common,*mission-campaign_.areas()[area_].firstMission+1,725,14);
    }
    const char* message=screen_==Screen::Locked?"locked":screen_==Screen::Missing?"missing":
                        screen_==Screen::Error?"error":nullptr;
    if (message) {
        renderer.box(170,225,460,155,ColorPanel);
        label(renderer,message,400,273);
        label(renderer,"result-control",400,336);
    }
}

void PspMusou::drawParty(StageRenderer& renderer, const UiAsset& common) const {
    art(renderer, ui_, "team-panel", 29, 59, 742, 482);
    renderer.box(48, 78, 704, 440, 0xce131422);
    label(renderer, "team-title", 400, 100);
    art(renderer, ui_, "coin-icon", 600, 86, 28, 30);
    number(renderer, common, progress_->money(), 635, 88);
    for (unsigned slot = 0; slot < party_.team().size(); ++slot) {
        const auto recruit = party_.team()[slot];
        const auto roster = party_.recruits()[recruit].roster;
        const float x = 59 + slot * 232;
        const auto* member = party_.member(recruit);
        renderer.box(x, 134, 217, 187, slot == teamSlot_ ? 0xffe0bb48 : 0x96537b97);
        renderer.box(x + 3, 137, 211, 181, 0xff1c2030);
        portrait(renderer, common, roster, "card", x + 24, 151, 169, 113);
        fighterName(renderer, common, roster, x + 108, 287, 197);
        if (!slot) art(renderer, ui_, "leader-star", x + 8, 142, 26, 25);
        number(renderer, common, member->level, x + 174, 143, 23);
    }
    const auto* active = party_.member(party_.team()[teamSlot_]);
    const auto maxExperience = MusouMember::levelUpExp(active->level);
    label(renderer, "experience", 109, 352);
    gauge(renderer, double(active->experience) / maxExperience, 168, 344, 354, 16, 0xffe0bb48);
    number(renderer, common, active->experience, 535, 340, 23);
    number(renderer, common, maxExperience, 653, 340, 23);
    label(renderer, "experience-slash", 638, 351);
    label(renderer, "team-hp", 101, 410);
    number(renderer, common, 1000 + 150 * active->level, 141, 398, 24);
    label(renderer, "team-qi", 283, 410);
    number(renderer, common, std::min(300U, 100 + 20 * active->level), 323, 398, 24);
    label(renderer, "team-energy", 447, 410);
    number(renderer, common, 50 + 10 * active->level, 485, 398, 24);
    label(renderer, "team-attack", 610, 410);
    number(renderer, common, 100 * active->level, 653, 398, 24);
    label(renderer, "team-control", 400, 488);
}

void PspMusou::drawShop(StageRenderer& renderer, const UiAsset& common) const {
    art(renderer, ui_, "shop-panel", 29, 59, 742, 482);
    label(renderer, "shop-title", 203, 101);
    art(renderer, ui_, "coin-icon", 600, 86, 28, 30);
    number(renderer, common, progress_->money(), 635, 88);
    if (shop_.empty()) {
        label(renderer, "shop-back-control", 400, 510);
        return;
    }
    const unsigned first = shopIndex_ / ShopPageSize * ShopPageSize;
    const unsigned last = std::min(first + ShopPageSize, unsigned(shop_.size()));
    for (unsigned index = first; index < last; ++index) {
        const auto id = shop_[index];
        const unsigned cell = index - first;
        const float x = 50 + (cell % ShopColumns) * 69;
        const float y = 129 + (cell / ShopColumns) * 66;
        art(renderer, ui_, "shop-face-frame", x, y, 62, 62);
        portrait(renderer, common, party_.recruits()[id].roster, "icon", x + 5, y + 5, 52, 52);
        if (!party_.member(id)) renderer.box(x + 5, y + 5, 52, 52, 0x80000000);
        if (index == shopIndex_) {
            renderer.box(x, y, 62, 3, 0xffe0bb48);
            renderer.box(x, y + 59, 62, 3, 0xffe0bb48);
        }
    }
    const unsigned id = shop_[shopIndex_];
    const auto& recruit = party_.recruits()[id];
    const auto* member = party_.member(id);
    art(renderer, ui_, "shop-feature", 415, 129, 323, 317);
    if (party_.available(id)) portrait(renderer, common, recruit.roster, "card", 457, 166, 238, 159);
    else portrait(renderer, common, recruit.roster, "icon", 502, 164, 145, 145);
    fighterName(renderer, common, recruit.roster, 577, 366, 247);
    if (member) {
        label(renderer, "level", 530, 413);
        number(renderer, common, member->level, 576, 400, 26);
    } else {
        art(renderer, ui_, "coin-icon", 514, 398, 28, 30);
        number(renderer, common, recruit.price, 550, 399, 26);
    }
    number(renderer, common, first / ShopPageSize + 1, 173, 467, 21);
    label(renderer, "experience-slash", 201, 478);
    number(renderer, common, (shop_.size() + ShopPageSize - 1) / ShopPageSize, 222, 467, 21);
    label(renderer, !party_.available(id) ? "shop-missing-control" :
                      member ? "shop-choose-control" : "shop-buy-control", 400, 512);
}

void PspMusou::drawPartyPrompt(StageRenderer& renderer, const UiAsset& common) const {
    renderer.box(0, 0, 800, 600, 0xa0000000);
    art(renderer, ui_, "shop-confirm", 152, 157, 496, 286);
    if (notice_ != Notice::None) {
        const char* text = notice_ == Notice::Coins ? "shop-coins" :
                           notice_ == Notice::Asset ? "shop-missing" : "save-failed";
        label(renderer, text, 400, 268);
        label(renderer, "shop-dismiss-control", 400, 355);
        return;
    }
    const auto& recruit = party_.recruits()[shop_[shopIndex_]];
    label(renderer, "shop-confirm-title", 400, 217);
    fighterName(renderer, common, recruit.roster, 400, 262, 240);
    art(renderer, ui_, "coin-icon", 322, 297, 28, 30);
    number(renderer, common, recruit.price, 365, 298, 26);
    label(renderer, "shop-confirm-control", 400, 382);
}

void PspMusou::drawFight(StageRenderer& renderer,const UiAsset& common) {
    const auto& battle=*battle_;
    sprites_.clear();
    if(battle.sourceControl()){
        for(auto key:battle.sourceScene().paint().players())if(SourcePaint::kind(key)==SourcePaint::Kind::Sprite){
            const auto id=SourcePaint::id(key);
            if(id&SourceScene::Object){
                const auto index=battle.sourceScene().storage(id);if(index>=battle.objects().size())continue;
                std::visit([&](const auto& object){sprites_.push_back({object.asset(),object.pose(),object.position(),object.facing(),nullptr,255,{},key});},battle.objects()[index]);
            }else if(id<3){
                const auto& actor=battle.member(id);sprites_.push_back({actor.asset(),actor.pose(),actor.motion().position,actor.facing(),actor.screenPose(),actor.opacity(),actor.visualTint(),key,id!=battle.leader()||!battle.hidesPlayer()});
            }else{
                const auto enemy=std::ranges::find(battle.enemies(),id,&MusouOpponent::id);if(enemy==battle.enemies().end())continue;
                const auto& actor=enemy->motion;sprites_.push_back({actor.asset(),actor.pose(),actor.motion().position,actor.facing(),actor.screenPose(),enemy->displayOpacity(),actor.visualTint(),key,!battle.hidesEnemy(id)});
            }
        }
    }else{
    const auto appendObject=[&](const FightObject& value) {
        std::visit([&](const auto& object) {
            sprites_.push_back({object.asset(),object.pose(),object.position(),object.facing()});
        },value);
    };
    for (const auto& value:battle.objects()) {
        if (const auto* attacker=std::get_if<FightAttacker>(&value);attacker&&attacker->behindFighters()) appendObject(value);
    }
    const auto& player=battle.player();
    if (!battle.hidesPlayer())
        sprites_.push_back({player.asset(),player.pose(),player.motion().position,player.facing(),player.screenPose(),player.opacity(),player.visualTint()});
    for (const auto& enemy:battle.enemies()) {
        if (battle.hidesEnemy(enemy.id)) continue;
        const auto& actor=enemy.motion;
        const unsigned alpha=battle.sourceControl()?enemy.displayOpacity():enemy.fade ? actor.opacity()*(enemy.fade-1)/20 : actor.opacity();
        sprites_.push_back({actor.asset(),actor.pose(),actor.motion().position,actor.facing(),actor.screenPose(),alpha,actor.visualTint()});
    }
    for (const auto& value:battle.objects()) {
        if (const auto* attacker=std::get_if<FightAttacker>(&value);attacker&&attacker->behindFighters()) continue;
        appendObject(value);
    }
    }
    const auto scene=battle.scene();
    auto arenaEffects=scene;arenaEffects.shines={};arenaEffects.shineAlpha=0;
    renderer.arena(stage_,battle.camera(),sprites_,arenaEffects,&common,reactionArt_,combatArt_,&mapBlurArt_,&mapOpticalArt_);
    renderer.beginUi();
    for (unsigned i=0;i<3;++i) {
        const auto& actor=battle.member(i);
        const float y=12+38*i;
        number(renderer,common,i+1,14,y,18);
        if (i==battle.leader()) renderer.box(37,y-2,204,35,0x9764aef2);
        gauge(renderer,actor.health()/actor.maxHealth(),40,y,198,12,0xff439bea);
        gauge(renderer,actor.qi()/actor.maxQi(),40,y+14,198,7,0xffe3cc49);
        gauge(renderer,actor.energy()/actor.maxEnergy(),40,y+22,198,7,0xff58bd91);
    }
    const auto& wave=battle.wave();
    number(renderer,common,(wave.timeLeft()+29)/30,367,15,34);
    label(renderer,"wave",591,28);
    number(renderer,common,wave.currentWave(),634,16,23);
    label(renderer,"kills",712,28);
    number(renderer,common,battle.kills(),752,16,23);
    label(renderer,"battle-control",400,575);
    drawBoss(renderer,common);
    if(combo_)sourceComboDraw(battle.comboUi(),true,0,[&](const char* key,double x,double y,double w,double h){renderer.image(*combo_,key,x,y,w,h);});
    if (screen_==Screen::Pause || saveFailed_) {
        renderer.box(0,0,800,600,0x99000000);
        renderer.box(212,211,376,188,ColorPanel);
        if (screen_==Screen::Pause) {
            art(renderer,common,"label-pause",358,240,84,34);
            art(renderer,common,"label-resume",300,316,200,30);
        } else {
            label(renderer,"save-failed",400,274);
            label(renderer,"save-control",400,346);
        }
    }
    renderer.shine(scene);
    if(transition_&&battle.closingTransition().visible())renderer.transition(*transition_,battle.closingTransition().frame());
}

void PspMusou::drawBoss(StageRenderer& renderer, const UiAsset& common) const {
    if (const auto* boss = battle_->boss();boss&&!boss->deadHandled) {
        constexpr float x = 400.15f, y = 71.55f;
        art(renderer,ui_,"boss-hp",x,y,402,49);
        art(renderer,ui_,"boss-hp-shadow",x+12.05f,y+15.2f,349,14);
        const float width = 340 * std::clamp(float(boss->motion.health()/boss->motion.maxHealth()),0.f,1.f);
        if (width > 0) art(renderer,ui_,"boss-hp-bar",x+356.7f-width,y+14.2f,width,11);
        if (boss->bank->rosterIndex) {
            char key[32];
            std::snprintf(key,sizeof(key),"fighter-icon-%u",*boss->bank->rosterIndex);
            art(renderer,common,key,x+356.5f,y+3.2f,37,37);
        }
    }
    const auto frame=battle_->presentation().frame();
    if(!hud_||frame>=MusouCueRanges.size())return;
    const auto range=MusouCueRanges[frame];
    const auto draw=[&](const MusouCueDraw& pose){
        const bool flip=pose.a<0;
        const float x=float(flip?pose.x+pose.a*pose.w:pose.x);
        renderer.imageTint(*hud_,pose.key,x,float(pose.y),float(std::abs(pose.a)*pose.w),float(pose.d*pose.h),flip,
            unsigned(std::clamp(std::lround(pose.alpha*255),0l,255l)),unsigned(std::clamp(std::lround(pose.offset),0l,255l)));
    };
    for(unsigned i=0;i<range.count;++i){
        const auto& pose=MusouCueDraws[range.first+i];
        if(pose.key[0]!='@'){draw(pose);continue;}
        const auto age=battle_->presentation().stripFrame();
        const auto children=pose.key[1]=='0'?MusouStripRanges0[age]:MusouStripRanges1[age];
        for(unsigned j=0;j<children.count;++j){
            auto child=MusouCueDraws[children.first+j];
            child.x=pose.x+pose.a*child.x;child.y=pose.y+pose.d*child.y;child.a*=pose.a;child.d*=pose.d;
            child.alpha*=pose.alpha;child.offset+=pose.offset;draw(child);
        }
    }

}

void PspMusou::record() {
    g_musouAnimationClockQa={};
    MusouQA next{};
    MusouCombatQA combat{};
    MusouControlQA control{};
    MusouPresentationQA presentation{};
    MusouLifecycleQA lifecycle{};
    next.screen=unsigned(screen_); next.draws=++draws_; next.area=area_; next.mission=mission_;
    lifecycle.draws=next.draws;lifecycle.clearRecorded=clearRecorded_;lifecycle.clearSaved=clearSaved_;lifecycle.saveFailed=saveFailed_;
    g_musouComboQa={next.draws,0,0,1,0,0,0};
    recordSourceSceneQa(g_musouSceneQa,next.draws,battle_?&battle_->sourceScene():nullptr);
    recordSourcePaintQa(g_musouPaintQa,next.draws,battle_?&battle_->sourceScene():nullptr);
    combat.draws = next.draws;
    control.draws=next.draws;presentation.draws=next.draws;
    next.money=progress_->money();
    next.stars=progress_->stars()[mission_];
    next.saveSerial = saveSerial_;
    const auto currentMembers = battle_ ? members_ : party_.battleMembers();
    for (unsigned i = 0; i < currentMembers.size(); ++i) {
        next.levels[i] = currentMembers[i].level;
        next.experience[i] = currentMembers[i].experience;
        next.team[i] = party_.recruits()[party_.team()[i]].roster;
    }
    for (unsigned i = 0; i < party_.recruits().size(); ++i) next.owned += party_.member(i) != nullptr;
    next.teamSlot = teamSlot_;
    next.notice = unsigned(notice_);
    if ((screen_ == Screen::Shop || screen_ == Screen::Buy) && !shop_.empty()) {
        const auto id = shop_[shopIndex_];
        next.recruit = party_.recruits()[id].roster;
        next.recruitPrice = party_.recruits()[id].price;
        if (const auto* member = party_.member(id)) next.recruitLevel = member->level;
    }
    if (battle_) {
        const auto& battle=*battle_;
        const auto& combo=battle.comboUi();
        g_musouComboQa={next.draws,battle.comboHits().count(),combo.number(),combo.frame(),combo.shown(),combo.visible(),unsigned(battle.comboHits().targets().size())};
        if(const auto* clocks=battle.gameClocks())g_musouAnimationClockQa=clocks->view();
        const auto& actor=battle.player();
        const auto& ui=battle.presentation();const auto& transition=battle.closingTransition();
        lifecycle.result=unsigned(battle.wave().result());
        if(const auto& result=battle.wave().sourceResult()){
            lifecycle.revision=result->revision();lifecycle.complete=result->completeAccepted();lifecycle.finished=result->finished();
        }
        lifecycle.passRequested=ui.passRequested();lifecycle.returnSerial=ui.returnSerial();lifecycle.endListeners=ui.endListeners();
        for(const auto& enemy:battle.enemies())if(enemy.sourceFade.active()){
            if(!lifecycle.activeFades)lifecycle.firstFadeAlpha=enemy.motion.displayAlpha();
            ++lifecycle.activeFades;
        }
        presentation={next.draws,battle.ticks(),ui.roots(),ui.frame(),unsigned(ui.kind()),ui.starting(),ui.ending(),ui.returnRequested(),
            battle.leaveReady(),ui.koListeners(),ui.completions(),ui.timeoutRoots(),transition.frame(),transition.visible(),transition.rendering(),battle.allowUiPause(),ui.stripFrame()};
        if(const auto* source=battle.sourceControl()){
            control.ticks=battle.ticks();control.running=source->running();control.renderHit=source->renderHit();
            control.animations=source->animations().size();control.renders=source->renders().size();
            const auto& counters=source->counters();control.replaceFrame=counters.replaceFrame;
            control.replaceHold=counters.replaceHold;control.explodeFrame=counters.explodeFrame;
            control.retained=battle.retainedSourceActors();control.gravity=actor.motion().gravity();
            if(const auto* boss=battle.boss();boss&&boss->bossAI){control.bossLevel=boss->bossAI->level();
                control.bossDecisions=boss->bossAI->decisions();control.bossIntents=boss->bossAI->intents();}
        }
        const auto scene = battle.scene();
        combat.live = scene.combat.size();
        combat.freeze = battle.hitFreezeFrames();
        combat.paused = battle.hitFreeze();
        combat.enabled = battle.freezeEnabled();
        combat.emitted = battle.emittedEffects();
        const auto tint = actor.visualTint();
        combat.playerTint[0] = tint.red; combat.playerTint[1] = tint.green; combat.playerTint[2] = tint.blue;
        combat.playerState = unsigned(actor.aiState());
        for (const auto& view : scene.combat) {
            if (combat.count == std::size(combat.views)) break;
            unsigned target = 0xffffffff;
            if (view.target) {
                for (unsigned i = 0; i < 3; ++i) if (view.target == battle.member(i).effectTarget()) target = i;
                for (const auto& enemy : battle.enemies()) if (view.target == enemy.motion.effectTarget()) target = enemy.id;
            }
            combat.views[combat.count++] = {unsigned(view.effect - combatEffects().data()), view.art->clip, view.frame(),
                view.cursor, unsigned(view.finished), target, float(view.origin.x), float(view.origin.y), view.facing};
        }
        next.ticks=battle.ticks(); next.wave=battle.wave().currentWave(); next.kills=battle.kills(); next.spawned=battle.spawned();
        next.time=battle.wave().timeLeft(); next.leader=battle.leader(); next.result=unsigned(battle.wave().result());
        next.x=float(actor.motion().position.x); next.y=float(actor.motion().position.y); next.hp=float(actor.health());
        next.qi=float(actor.qi()); next.maxHp=float(actor.maxHealth()); next.facing=unsigned(actor.facing());
        next.action=actor.timelineFrame(); next.level=members_[battle.leader()].level;
        next.bossCue=unsigned(battle.bossCue().kind);
        next.bossCueTicks=battle.bossCue().ticks;
        next.zoom=float(battle.camera().zoom);
        if (const auto* boss=battle.boss()) {
            next.bossId=boss->id;
            next.bossPose=boss->motion.timelineFrame();
            next.bossHp=float(boss->motion.health());
            next.bossMaxHp=float(boss->motion.maxHealth());
        }
        for (const auto& enemy:battle.enemies()) {
            if (next.enemyCount>=std::size(next.foes)) break;
            const auto& e=enemy.motion;
            const auto tint = e.visualTint();
            combat.foes[next.enemyCount] = {enemy.id, tint.red, tint.green, tint.blue};
            next.foes[next.enemyCount++]={float(e.motion().position.x),float(e.motion().position.y),float(e.health()),
                                         enemy.id,e.timelineFrame(),enemy.spawn.enemy};
        }
    }
    std::memcpy(const_cast<MusouPresentationQA*>(&g_musouPresentationQa),&presentation,sizeof(presentation));
    // A single memory snapshot provides frame-consistent fields to the emulator verifier.
    std::memcpy(const_cast<MusouQA*>(&g_musouQa),&next,sizeof(next));
    std::memcpy(const_cast<MusouCombatQA*>(&g_musouCombatQa),&combat,sizeof(combat));
    std::memcpy(const_cast<MusouControlQA*>(&g_musouControlQa),&control,sizeof(control));
    g_musouLifecycleQa=lifecycle;
}

void PspMusou::draw(StageRenderer& renderer,const UiAsset& common,const FighterAsset* reactionArt,
                    const FighterAsset* combatArt) {
    reactionArt_=reactionArt;
    combatArt_=combatArt;
    if (battle_) drawFight(renderer,common);
    else {
        renderer.beginUi();
        drawWorld(renderer,common);
        if (screen_ == Screen::Team || screen_ == Screen::Shop || screen_ == Screen::Buy) {
            renderer.box(0, 0, 800, 600, 0xb0000000);
            if (screen_ == Screen::Team) drawParty(renderer, common);
            else drawShop(renderer, common);
            if (screen_ == Screen::Buy || notice_ != Notice::None) drawPartyPrompt(renderer, common);
        }
    }
    record();
}

} // namespace bvn
