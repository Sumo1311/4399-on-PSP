// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include "versus_round.hpp"
#include "musou_battle.hpp"
#include <cstdio>
#include <string>

namespace bvn::controller_clock_qa {
struct Checks {
    unsigned error=0,rows=0,display=0,actor=0,effect=0,split=0,matches=0,paused=0,knockouts=0,musouCases=0;
    std::uint64_t hash=14695981039346656037ull;
    void word(unsigned value){for(unsigned i=0;i<4;++i)hash=(hash^((value>>(8*i))&255))*1099511628211ull;}
#define CLOCK_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
    bool reference(const std::string& folder){
        FILE* input=std::fopen((folder+"GAMECLOCK.IN").c_str(),"r");CLOCK_REQUIRE(input);
        unsigned count=0;CLOCK_REQUIRE(std::fscanf(input,"%u",&count)==1&&count==900);
        FightEffects effects;
        for(unsigned i=0;i<count;++i){
            unsigned fps,scenario,tick,action,manual,logical,wantActor,wantEffect;
            int milliseconds,actorGap,actorPhase,effectGap,effectPhase,slowFrames;double rate,slowRate;
            CLOCK_REQUIRE(std::fscanf(input,"%u%u%u%u%lf%d%u%u%u%u%d%d%d%d%lf%d",&fps,&scenario,&tick,&action,&rate,&milliseconds,&manual,&logical,&wantActor,&wantEffect,&actorGap,&actorPhase,&effectGap,&effectPhase,&slowRate,&slowFrames)==16);
            if(!tick){effects=FightEffects{};effects.setProfile(fps,30);effects.enableGameClocks();}
            if(action==1)effects.slow(rate,milliseconds);if(action==2)effects.resumeSlow();
            unsigned gotActor=0,gotEffect=0;
            for(unsigned sub=0;sub<60/fps;++sub){
                if(manual)continue;
                // Deliberately false: the playable controller must own both
                // signals, regardless of its caller's former global modulus.
                const bool fx=effects.effectAnimation(false);
                effects.step(fx,{},std::uint32_t((display+1)*1000/60));
                const bool main=logical&&effects.actorAnimation(false);
                gotActor+=main;gotEffect+=fx;++display;
            }
            CLOCK_REQUIRE(gotActor==wantActor&&gotEffect==wantEffect);
            const auto& c=effects.gameClocks()->clocks();
            CLOCK_REQUIRE(c.actor().gap()==actorGap&&c.actor().frame()==actorPhase);
            CLOCK_REQUIRE(c.effect().gap()==effectGap&&c.effect().frame()==effectPhase);
            CLOCK_REQUIRE(c.slowRate()==slowRate&&c.slowFrames()==slowFrames);
            const auto blur=effects.scene().blur;
            CLOCK_REQUIRE(blur.slowRate==slowRate&&blur.slowFrames==slowFrames);
            for(unsigned value:{wantActor,wantEffect,unsigned(actorGap),unsigned(actorPhase),unsigned(effectGap),unsigned(effectPhase),unsigned(slowRate*4),unsigned(slowFrames)})word(value);
            ++rows;actor+=gotActor;effect+=gotEffect;split+=gotActor!=gotEffect;
        }
        std::fclose(input);return true;
    }
    bool match(const StageAsset& stage,const FighterAsset& asset){
        for(unsigned fps:{30u,60u}){
            FightMatch m(asset,asset);const auto floor=stage.geometry().playerBottom;
            m.reset(stage,{350,floor},{700,floor});m.setControllerProfile(fps,30);
            for(unsigned side=0;side<2;++side)m.actor(side).setGameHz(fps);
            m.actor(0).setQi(300);
            bool sawCutin=false,endedCutin=false;unsigned casterFrames=0,otherFrames=0;
            for(unsigned tick=0;tick<600;++tick){
                const bool cutin=m.cinematic();
                const int phase=m.gameClocks()->clocks().actor().frame();
                const auto other=m.aiAnimation(1),caster=m.aiAnimation(0);
                const auto otherPoint=m.actor(1).motion().position;
                m.step(stage.geometry(),{{{.superPressed=tick<2}, {}}},false,Contacts::Round);
                if(cutin){
                    sawCutin=true;++paused;
                    CLOCK_REQUIRE(m.gameClocks()->clocks().actor().frame()==phase);
                    CLOCK_REQUIRE(m.aiAnimation(1)==other);
                    CLOCK_REQUIRE(m.actor(1).motion().position.x==otherPoint.x&&m.actor(1).motion().position.y==otherPoint.y);
                    casterFrames+=m.aiAnimation(0)-caster;
                    if(!m.cinematic())endedCutin=true;
                }else otherFrames+=m.aiAnimation(1)-other;
                if(endedCutin&&tick>200)break;
            }
            CLOCK_REQUIRE(sawCutin&&endedCutin&&casterFrames>0&&otherFrames>0);
            word(fps);word(casterFrames);word(otherFrames);++matches;

            // Controlled source hit metadata exercises the real Match dispatch,
            // not just MapBlurState: ordinary long freeze has no blur; a hit
            // slow request is applied first and selects width six.
            for(unsigned hitSlow:{0u,500u}){
                FightMatch hit(asset,asset);hit.reset(stage,{350,floor},{500,floor});hit.setControllerProfile(fps,30);
                for(unsigned side=0;side<2;++side)hit.actor(side).setGameHz(fps);
                FighterStrike strike{};strike.key=17;strike.hitType=3;strike.damage=1;strike.hurtFrames=6;strike.slowTimeMs=hitSlow;
                CLOCK_REQUIRE(hit.actor(1).receiveStrike({strike,1,350,1,-1,{},true,{480,floor-80,520,floor}}));
                hit.step(stage.geometry(),{},false,Contacts::Locked);
                CLOCK_REQUIRE(hit.hitFreezeFrames()>0);
                CLOCK_REQUIRE(hit.scene().blur.x==(hitSlow?6:0));
                CLOCK_REQUIRE(hit.gameClocks()->clocks().slowRate()==(hitSlow?1.5:0));
                unsigned held=0;
                for(unsigned tick=0;tick<180;++tick){
                    const bool frozen=hit.hitFreeze();
                    const auto before=hit.aiAnimation(0);
                    hit.step(stage.geometry(),{},false,Contacts::Locked);
                    if(frozen){CLOCK_REQUIRE(hit.aiAnimation(0)==before);++held;}
                }
                CLOCK_REQUIRE(held>0&&hit.gameClocks()->clocks().slowRate()==0&&hit.scene().blur.slowRate==0);
                word(fps);word(hitSlow);word(held);++matches;
            }
        }
        return true;
    }
    bool knockout(const StageAsset& stage,const FighterAsset& asset){
        for(unsigned fps:{30u,60u}){
            RoundRules rules{};rules.sourceQuality=true;rules.gameHz=fps;rules.shineHz=30;rules.maxHealth=1;
            VersusRound round(stage,std::span(&asset,1),std::span(&asset,1),-1,{},BattleMode::Versus,{}, {},rules);
            bool sawFall=false,sawWinner=false,sawReady=false;
            for(unsigned tick=0;tick<2200;++tick){
                if(round.phase()==RoundPhase::Playing&&!sawFall){
                    // Fix starting spacing for this controlled KO route.
                    auto& m=const_cast<FightMatch&>(round.match());const double floor=stage.geometry().playerBottom;
                    m.actor(0).place({350,floor});m.actor(1).place({390,floor});
                }
                const auto before=round.phase();
                std::array<FighterControls,2> controls{};
                if(before==RoundPhase::Playing)controls[0].attackPressed=true;
                round.step(controls,before<=RoundPhase::IntroPan||before==RoundPhase::Victory);
                const auto after=round.phase();
                if(after==RoundPhase::KnockoutFall){
                    sawFall=true;CLOCK_REQUIRE(round.match().gameClocks()->clocks().slowRate()==2);
                }
                if(before==RoundPhase::KnockoutFall&&after==RoundPhase::WinnerDelay){
                    sawWinner=true;CLOCK_REQUIRE(round.match().gameClocks()->clocks().slowRate()==0);
                    CLOCK_REQUIRE(round.match().scene().blur.slowRate==0);
                }
                if(sawWinner&&after==RoundPhase::Ready){sawReady=true;break;}
            }
            CLOCK_REQUIRE(sawFall&&sawWinner&&sawReady);word(fps);++knockouts;
        }
        return true;
    }
    bool musou(const std::string& folder,const StageAsset& stage,const FighterAsset& asset){
        MusouCampaign campaign;CLOCK_REQUIRE(campaign.open((folder+"MUSLETH.BVM").c_str()));
        const MusouFighterBank bank{"qa-player",std::span(&asset,1),0,1000,0};
        const MusouFighterBank enemyBank{"qa-enemy",std::span(&asset,1),0,1000,0};
        for(unsigned fps:{30u,60u}){
            MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle run;
            CLOCK_REQUIRE(run.start(campaign,0,stage,{bank,bank,bank},std::span(&enemyBank,1),progress,members,0x12345678));
            run.setControllerProfile(fps,30);
            for(unsigned tick=0;tick<500;++tick){
                run.step({},false);
                if(run.enemies().size()==1&&run.bossCue().kind==MusouBossCue::Kind::None)break;
            }
            CLOCK_REQUIRE(run.enemies().size()==1&&run.bossCue().kind==MusouBossCue::Kind::None);
            auto& player=const_cast<FighterMotion&>(run.member(run.leader()));
            auto& target=const_cast<MusouOpponent&>(run.enemies().front());
            target.bossAI.reset();player.reset({1000,stage.geometry().playerBottom},1);player.setQi(300);
            target.motion.reset({1040,stage.geometry().playerBottom},-1);target.motion.setHealth(1);
            player.present(FighterCommand::SuperUp);
            unsigned cutin=0,ko=0;bool sawKo=false;
            for(unsigned tick=0;tick<700;++tick){
                const auto kind=run.bossCue().kind;
                const bool casting=player.cutin().kind!=CutinKind::None;
                const int phase=run.gameClocks()->clocks().actor().frame();
                if(!sawKo)target.motion.place({1040,stage.geometry().playerBottom});
                run.step({},false);
                if(casting&&kind==MusouBossCue::Kind::None){
                    CLOCK_REQUIRE(run.gameClocks()->clocks().actor().frame()==phase);++cutin;
                }
                if(kind==MusouBossCue::Kind::Knockout){
                    CLOCK_REQUIRE(run.gameClocks()->clocks().actor().frame()==phase);++ko;
                }
                sawKo|=run.bossCue().kind==MusouBossCue::Kind::Knockout;
                if(sawKo&&run.bossCue().kind==MusouBossCue::Kind::None)break;
            }
            CLOCK_REQUIRE(cutin>0&&ko>0&&sawKo);word(fps);word(cutin);word(ko);++musouCases;
        }
        return true;
    }
    bool run(std::string folder){
        if(!folder.empty()&&folder.back()!='/')folder+='/';
        if(!reference(folder))return false;
        StageAsset stage;FighterAsset asset;
        CLOCK_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        CLOCK_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        return match(stage,asset)&&knockout(stage,asset)&&musou(folder,stage,asset);
    }
#undef CLOCK_REQUIRE
};
} // namespace bvn::controller_clock_qa
