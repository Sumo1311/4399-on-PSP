#pragma once

#include "musou_party.hpp"
#include "psp_audio.hpp"
#include "psp_stage_renderer.hpp"
#include "roster_asset.hpp"

#include <string>

namespace bvn {

class PspMusou {
public:
    bool open(std::string folder, const RosterAsset& roster);
    void presentationAssets(const UiAsset& hud,TransitionAsset& transition,const UiAsset& combo){hud_=&hud;transition_=&transition;combo_=&combo;}
    bool ready() const { return progress_.has_value(); }
    void gameHz(unsigned hz,unsigned shine){gameHz_=hz;shineHz_=shine;}
    bool step(unsigned pressed, FighterControls input, PspAudio& audio, StageRenderer& renderer, unsigned held = 0);
    void draw(StageRenderer& renderer, const UiAsset& common, const FighterAsset* reactionArt = nullptr,
              const FighterAsset* combatArt = nullptr);
    void titleLabel(StageRenderer& renderer, float y) const;
#ifdef BVN_MUSOU_QA
    void replayPad(unsigned& held, unsigned& pressed);
#endif

private:
    const FighterAsset* reactionArt_ = nullptr;
    const FighterAsset* combatArt_ = nullptr;
    enum class Screen : unsigned { World, Start, Fight, Pause, Result, Locked, Missing, Error, Team, Shop, Buy };
    enum class Notice : unsigned { None, Coins, Asset, Save };
    struct EnemyFile { std::string name, path; unsigned health; };

    bool loadTeam();
    bool loadBank(std::string_view name, const char* path, unsigned hp,
                  std::vector<FighterAsset>& assets, MusouFighterBank& bank);
    void loadProgress();
    bool saveProgress(const MusouParty& party, unsigned money);
    bool commitParty(MusouParty next, unsigned cost = 0);
    void openShop();
    void stepShop(unsigned pressed, bool confirm, bool back);
    bool enter(PspAudio& audio, StageRenderer& renderer);
    void leave(StageRenderer& renderer, PspAudio& audio);
    void drawWorld(StageRenderer& renderer, const UiAsset& common) const;
    void drawParty(StageRenderer& renderer, const UiAsset& common) const;
    void drawShop(StageRenderer& renderer, const UiAsset& common) const;
    void drawPartyPrompt(StageRenderer& renderer, const UiAsset& common) const;
    void drawFight(StageRenderer& renderer, const UiAsset& common);
    void drawBoss(StageRenderer& renderer, const UiAsset& common) const;
    void label(StageRenderer& renderer, const char* key, float x, float y) const;
    void record();
    void sourceSignals(PspAudio& audio);
    void returnToWorld(StageRenderer& renderer,PspAudio& audio);

    std::string folder_;
    std::string saveFolder_;
    const RosterAsset* roster_ = nullptr;
    MusouCampaign campaign_;
    std::optional<MusouProgress> progress_;
    MusouParty party_;
    std::array<MusouMember,3> members_;
    std::array<std::vector<FighterAsset>,3> teamAssets_;
    std::array<MusouFighterBank,3> teamBanks_{};
    std::vector<EnemyFile> enemyFiles_;
    std::vector<std::vector<FighterAsset>> enemyAssets_;
    std::vector<MusouFighterBank> enemyBanks_;
    std::vector<Point> markers_;
    UiAsset ui_;
    const UiAsset* hud_=nullptr;
    const UiAsset* combo_=nullptr;
    TransitionAsset* transition_=nullptr;
    StageAsset stage_;
    FighterAsset mapBlurArt_;
    FighterAsset mapOpticalArt_;
    std::optional<MusouBattle> battle_;
    std::vector<FighterView> sprites_;
    Screen screen_ = Screen::World;
    Notice notice_ = Notice::None;
    unsigned area_ = 0, mission_ = 0, draws_ = 0;
    unsigned teamSlot_ = 0, shopIndex_ = 0;
    std::vector<unsigned> shop_;
    std::uint32_t saveSerial_ = 0;
    bool clearRecorded_ = false, clearSaved_ = false, saveFailed_ = false;
    unsigned gameHz_=30;
    unsigned shineHz_=15;
#ifdef BVN_MUSOU_QA
    void loadReplay();
    std::vector<std::uint16_t> replay_;
    unsigned replayCursor_ = 0, replayHeld_ = 0;
#endif
};

} // namespace bvn
