// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_hud.hpp"
#include "musou_battle.hpp"
#include "versus_round.hpp"
#include "ui_asset.hpp"
#include <cstdio>
#include <string>
namespace bvn::hud_session_qa {
struct Checks {
    unsigned error=0,hudRows=0,musouRows=0,versusCases=0,musouCases=0,pauses=0,assets=0;
    std::uint64_t hash=14695981039346656037ull;
    void word(unsigned value){for(unsigned i=0;i<4;++i)hash=(hash^((value>>(8*i))&255))*1099511628211ull;}
    void number(double value){word(unsigned(std::llround(value*1000000)));}
#define HUD_REQUIRE(value) do{if(!(value)){error=__LINE__;return false;}}while(false)
    unsigned queueHash(const SourceTimeoutQueue& queue){
        unsigned h=2166136261u;const auto w=[&](unsigned v){for(unsigned i=0;i<4;++i)h=(h^((v>>(8*i))&255))*16777619u;};
        w(queue.entries().size());for(const auto& e:queue.entries()){w(bool(e));if(e){w(e->id);w(e->frames);}}return h;
    }
    bool hudReference(const std::string& folder){
        FILE* file=std::fopen((folder+"HUD.IN").c_str(),"r");HUD_REQUIRE(file);
        unsigned count=0;HUD_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==7680);
        SourceHpBar hp;SourceEnergyBar energy;SourceQiBar qi;
        const std::array<AiState,5> states{AiState::Normal,AiState::Hurt,AiState::Flying,AiState::Down,AiState::Guard};
        for(unsigned row=0;row<count;++row){
            std::array<double,27> v{};for(auto& x:v)HUD_REQUIRE(std::fscanf(file,"%lf",&x)==1);
            if(!v[2]){hp={};energy={};qi=SourceQiBar{};}
            if(v[9]==1)qi.show();if(v[9]==2)qi.hide();qi.animateNested();
            hp.render(v[3],states[unsigned(v[4])],unsigned(v[0]));energy.render(v[5],bool(v[6]));qi.render(v[7],v[8],.5+v[2]/120);
            const std::array<double,17> got{hp.green(),hp.red(),double(hp.delay()),double(hp.moving()),double(hp.justFly()),
                energy.value(),double(energy.frame()),double(energy.count()),double(energy.flashing()),double(energy.overload()),
                qi.value(),qi.assist(),double(qi.visible()),double(qi.readyVisible()),double(qi.readyFrame()),0,double(qi.maxFrame())};
            for(unsigned k=0;k<got.size();++k){
                if(k==15)continue;
                const double expected=k==13?double(bool(v[23])&&v[24]<14):v[10+k];
                HUD_REQUIRE(std::abs(got[k]-expected)<1e-11);number(got[k]);
            }
            HUD_REQUIRE(qi.pose().x==48&&qi.pose().y==534.1&&qi.pose().scale==1);++hudRows;
        }
        std::fclose(file);return true;
    }
    bool musouReference(const std::string& folder){
        FILE* file=std::fopen((folder+"MSESSION.IN").c_str(),"r");HUD_REQUIRE(file);
        unsigned count=0;HUD_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==14040);
        struct View {bool flat=true;unsigned victories=0;void koComplete(){flat=true;}void victory(unsigned actor){victories|=1u<<actor;}}view;
        SourceMusouPresentation ui;
        for(unsigned row=0;row<count;++row){
            std::array<unsigned,20> v{};for(auto& x:v)HUD_REQUIRE(std::fscanf(file,"%u",&x)==1);
            if(!v[2]){ui.initialize(v[0]);view={};}
            ui.beginRoot();
            if(v[3]==1)ui.entrance();if(v[3]==2){view.flat=false;ui.knockout();}
            if(v[3]==3)ui.end(true,2);if(v[3]==4)ui.end(false,2);
            if(!v[4]){if(v[5])ui.renderAnimate(view);if(view.flat)ui.afterMain();}
            ui.endRoot(view);
            const std::array<unsigned,14> got{ui.frame(),unsigned(ui.kind()),unsigned(ui.starting()),unsigned(ui.entranceDone()),
                unsigned(ui.ending()),unsigned(ui.returnRequested()),unsigned(view.flat),ui.koListeners(),ui.completions(),view.victories,
                ui.takeSignals(),queueHash(ui.timeouts()),ui.timeoutRoots(),ui.stripFrame()};
            for(unsigned i=0;i<got.size();++i){HUD_REQUIRE(got[i]==v[6+i]);word(got[i]);}++musouRows;
        }
        std::fclose(file);return true;
    }
    bool versus(const StageAsset& stage,const FighterAsset& art){
        for(unsigned hz:{30u,60u}){
            RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;rules.maxHealth=5000;
            VersusRound game(stage,std::span(&art,1),std::span(&art,1),60,{},BattleMode::Versus,{}, {},rules);
            for(unsigned display=0;display<200&&game.sourceTransitionRendering();++display)game.step({});
            do{}while(!game.step({},true));
            HUD_REQUIRE(game.hud().visible()&&game.phase()!=RoundPhase::Ready);
            for(unsigned display=0;display<1800&&game.phase()!=RoundPhase::Playing;++display)game.step({});
            HUD_REQUIRE(game.phase()==RoundPhase::Playing);
            auto& p1=const_cast<FighterMotion&>(game.match().actor(0));auto& p2=const_cast<FighterMotion&>(game.match().actor(1));
            p1.reset({350,stage.geometry().playerBottom},1);p2.reset({390,stage.geometry().playerBottom},-1);
            p1.present(FighterCommand::Attack);bool damage=false;
            for(unsigned display=0;display<120;++display){
                const double hp=p2.health();if(!game.step({}))continue;
                HUD_REQUIRE(game.hud().sample(1).hp==hp);
                if(p2.health()<hp){HUD_REQUIRE(game.hud().hp(1).green()>p2.health()/p2.maxHealth());damage=true;break;}
            }
            HUD_REQUIRE(damage);
            game.manualPause();const auto hp=game.hud().hp(1).green(),qi=game.hud().qi(0).value();const auto frame=game.hud().qi(0).maxFrame();
            for(unsigned display=0;display<10;++display)game.pausedDisplayFrame();
            HUD_REQUIRE(game.hud().hp(1).green()==hp&&game.hud().qi(0).value()==qi);
            HUD_REQUIRE(game.hud().qi(0).maxFrame()!=frame);game.manualResume();++pauses;++versusCases;word(hz);
        }
        return true;
    }
    bool musou(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
        MusouCampaign campaign;HUD_REQUIRE(campaign.open((folder+"MUSLETH.BVM").c_str()));
        const MusouFighterBank bank{"qa-player",std::span(&art,1),0,1000,0},foe{"qa-enemy",std::span(&art,1),0,1000,0};
        for(unsigned hz:{30u,60u}){
            MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;
            HUD_REQUIRE(game.start(campaign,0,stage,{bank,bank,bank},std::span(&foe,1),progress,members,0x12345678));game.setSourceProfile(hz,30);
            const double startX=game.member(0).motion().position.x;const auto startTime=game.wave().timeLeft();
            while(game.presentation().starting()){
                game.step({.horizontal=1},true);
                HUD_REQUIRE(game.member(0).motion().position.x==startX&&game.leader()==0&&game.wave().timeLeft()==startTime);
                HUD_REQUIRE(game.presentation().roots()<=2*hz);
            }
            HUD_REQUIRE(game.presentation().roots()==2*hz&&game.allowUiPause()&&game.member(0).allowLoseHp());
            for(unsigned display=0;display<900;++display){game.step({},false);for(const auto& e:game.enemies())const_cast<MusouOpponent&>(e).bossAI.reset();
                if(game.enemies().size()==1&&game.bossCue().kind==MusouBossCue::Kind::None)break;}
            HUD_REQUIRE(game.enemies().size()==1&&game.bossCue().kind==MusouBossCue::Kind::None);
            auto& player=const_cast<FighterMotion&>(game.member(0));auto& enemy=const_cast<MusouOpponent&>(game.enemies().front());
            const auto floor=stage.geometry().playerBottom;player.reset({350,floor},1);player.setHealth(100000);player.setQi(300);
            enemy.motion.reset({390,floor},-1);enemy.motion.setHealth(1);enemy.brain={};player.present(FighterCommand::Attack);
            for(unsigned display=0;display<400&&game.bossCue().kind!=MusouBossCue::Kind::Knockout;++display){player.place({350,floor});enemy.motion.place({390,floor});game.step({},false);}
            HUD_REQUIRE(game.bossCue().kind==MusouBossCue::Kind::Knockout&&!game.sourceControl()->running());
            game.manualPause();const auto ticks=game.ticks(),roots=game.presentation().roots();const auto clock=game.gameClocks()->view();
            for(unsigned display=0;display<180;++display)game.pausedDisplayFrame();
            HUD_REQUIRE(game.ticks()==ticks&&game.gameClocks()->view().displayFrames==clock.displayFrames);
            HUD_REQUIRE(game.presentation().roots()>roots+hz&&game.presentation().frame()==184&&game.presentation().koListeners()==1&&!game.freezeEnabled());++pauses;
            game.manualResume();
            for(unsigned display=0;display<1800&&game.wave().result()==MusouResult::Playing;++display)game.step({},false);
            HUD_REQUIRE(game.wave().result()==MusouResult::Won&&game.presentation().ending());
            const auto endTicks=game.ticks();bool victory=false;
            for(unsigned display=0;display<1800&&!game.presentation().returnRequested();++display){game.step({.horizontal=1},true);victory|=game.member(0).performing(FighterCommand::Victory);}
            HUD_REQUIRE(victory&&game.presentation().returnRequested()&&game.ticks()>endTicks+hz&&game.leader()==0);
            game.acknowledgeReturn();game.manualPause();const auto closingTicks=game.ticks();
            for(unsigned display=0;display<100&&!game.leaveReady();++display)game.pausedDisplayFrame();
            HUD_REQUIRE(game.leaveReady()&&game.closingTransition().frame()==25&&game.ticks()==closingTicks);++pauses;++musouCases;word(hz);
        }
        return true;
    }
    bool run(const std::string& folder){
        if(!hudReference(folder)||!musouReference(folder))return false;
        UiAsset ui;HUD_REQUIRE(ui.open((folder+"HUD.BVU").c_str()));for(const char* key:{"hp-red","energy-low","energy-bleach-low","energy-naruto-low","qi-max","musou-0","musou-15"}){HUD_REQUIRE(ui.find(key));++assets;}
        StageAsset stage;FighterAsset art;HUD_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));HUD_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        return versus(stage,art)&&musou(folder,stage,art);
    }
#undef HUD_REQUIRE
};
}
