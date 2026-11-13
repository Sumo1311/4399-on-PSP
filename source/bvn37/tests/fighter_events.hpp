// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "musou_battle.hpp"
#include <cstdio>
#include <cmath>
#include <string>
namespace bvn::fighter_events_qa {
struct Checks {
    unsigned error=0,rows=0,actors=0,bosses=0,minions=0,selfs=0;
    std::uint64_t hash=14695981039346656037ull;
    void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(8*i))&255))*1099511628211ull;}
#define EVT_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
    bool reference(const std::string& folder){
        FILE* f=std::fopen((folder+"EVENTS.IN").c_str(),"r");EVT_REQUIRE(f);unsigned count=0;EVT_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==200);
        SourceEnemyFade fade;unsigned removed=0;
        for(unsigned row=0;row<count;++row){double initial,alpha;unsigned tick,active,callbacks;EVT_REQUIRE(std::fscanf(f,"%lf %u %lf %u %u",&initial,&tick,&alpha,&active,&callbacks)==5);
            if(!tick){fade={};fade.start(initial);removed=0;}if(fade.render())++removed;
            EVT_REQUIRE(std::abs(fade.alpha()-alpha)<1e-12&&fade.active()==bool(active)&&removed==callbacks);
            word(unsigned(std::llround(fade.alpha()*1000000)));word(active);word(callbacks);++rows;
        }
        std::fclose(f);return true;
    }
    struct Observer {
        FighterMotion* actor;std::array<SourceFighterEvent,32> events{};unsigned size=0;
        bool valid=true;double hurtQi=-1;int hurtCache=-1;
        void on(SourceFighterEvent event){
            if(size>=events.size()){valid=false;return;}events[size++]=event;
            if(event==SourceFighterEvent::Hurt){valid&=actor->aiState()==AiState::Hurt||actor->aiState()==AiState::Flying;hurtQi=actor->qi();hurtCache=actor->currentHurtDamage();}
            if(event==SourceFighterEvent::HurtDown)valid&=actor->aiState()==AiState::Down;
            if(event==SourceFighterEvent::Dead)valid&=actor->aiState()==AiState::Dead&&!actor->alive();
            if(event==SourceFighterEvent::Idle)valid&=actor->aiState()==AiState::Normal;
        }
    };
    bool actorCases(const StageAsset& stage,const FighterAsset& art){
        for(unsigned hz:{30u,60u})for(unsigned kind=0;kind<4;++kind)for(bool fatal:{false,true}){
            FighterMotion actor(art);actor.sourceHz(hz);actor.reset({450,stage.geometry().playerBottom},-1);actor.setHealth(fatal?1:1000);actor.setQi(0);
            Observer observation{&actor};actor.sourceHooks({&observation,0,nullptr,nullptr,[](void* p,unsigned,SourceFighterEvent event){static_cast<Observer*>(p)->on(event);}});
            FighterStrike hit{};hit.key=2500+kind;hit.damage=10;hit.hurtFrames=4;hit.hitType=kind==1?11:1;hit.hitX=6;hit.hitY=kind==3?8:0;hit.flags=kind>=2?1:0;
            EVT_REQUIRE(actor.receiveStrike({hit,1,400}));EVT_REQUIRE(observation.size==1&&observation.events[0]==SourceFighterEvent::Hurt&&observation.hurtQi==0&&observation.hurtCache==0);
            bool finished=false;for(unsigned i=0;i<1200;++i){actor.step(stage.geometry(),{},hz==30||i%2==0);if(fatal?actor.deathComplete():actor.standing()){finished=true;break;}}
            EVT_REQUIRE(finished&&observation.valid);
            if(fatal){EVT_REQUIRE(observation.size==3&&observation.events[1]==SourceFighterEvent::HurtDown&&observation.events[2]==SourceFighterEvent::Dead);for(unsigned i=0;i<40;++i)actor.step(stage.geometry(),{},true);EVT_REQUIRE(observation.size==3);}
            else if(kind<2)EVT_REQUIRE(observation.size==3&&observation.events[1]==SourceFighterEvent::Idle&&observation.events[2]==SourceFighterEvent::HurtResume);
            else EVT_REQUIRE(observation.size==3&&observation.events[1]==SourceFighterEvent::HurtDown&&observation.events[2]==SourceFighterEvent::Idle);
            for(unsigned i=0;i<observation.size;++i)word(unsigned(observation.events[i]));word(hz);word(fatal);++actors;
        }
        return true;
    }
    bool sessions(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
        for(unsigned hz:{30u,60u})for(unsigned kind=0;kind<3;++kind){
            MusouCampaign campaign;EVT_REQUIRE(campaign.open((folder+(kind==1?"MUSMIN.BVM":"MUSLETH.BVM")).c_str()));
            MusouProgress progress(campaign);std::array<MusouMember,3> members{};
            const MusouFighterBank bank{"qa-player",std::span(&art,1),0,1000,0},foe{"qa-enemy",std::span(&art,1),0,1000,0};
            MusouBattle game;EVT_REQUIRE(game.start(campaign,0,stage,{bank,bank,bank},std::span(&foe,1),progress,members,0x12345678));game.setSourceProfile(hz,30);
            for(unsigned tick=0;tick<1800;++tick){game.step({},false);for(const auto& e:game.enemies())const_cast<MusouOpponent&>(e).bossAI.reset();if(!game.presentation().starting()&&game.enemies().size()==(kind==1?6U:1U)&&game.bossCue().kind==MusouBossCue::Kind::None)break;}
            EVT_REQUIRE(game.enemies().size()==(kind==1?6U:1U)&&game.bossCue().kind==MusouBossCue::Kind::None);
            auto& player=const_cast<FighterMotion&>(game.member(0));player.reset({350,stage.geometry().playerBottom},1);
            for(const auto& e:game.enemies()){auto& enemy=const_cast<MusouOpponent&>(e);enemy.brain={};enemy.bossAI.reset();enemy.motion.reset({1400,stage.geometry().playerBottom},-1);}
            auto& enemy=const_cast<MusouOpponent&>(game.enemies().front());auto& victim=kind==2?player:enemy.motion;
            const auto money=progress.money();victim.die();EVT_REQUIRE(!victim.deathComplete()&&game.kills()==0&&progress.money()==money);
            // Invoke only this actor's real animation bridge. Receipt consumers
            // must complete before this call returns, without a scene-tail poll.
            for(unsigned call=0;call<4;++call){victim.step(stage.geometry(),{},true);if(call<3)EVT_REQUIRE(game.kills()==0&&progress.money()==money&&game.wave().result()==MusouResult::Playing);}
            EVT_REQUIRE(victim.deathComplete()&&victim.sourceEvents(SourceFighterEvent::Dead)==1);
            if(kind==2){EVT_REQUIRE(game.wave().result()==MusouResult::Defeated&&game.presentation().ending()&&game.kills()==0);++selfs;}
            else{
                const auto& definition=campaign.enemies()[enemy.spawn.enemy];const unsigned earned=definition.money+campaign.missions()[0].enemyLevel*(enemy.boss?5:1);
                EVT_REQUIRE(game.kills()==1&&progress.money()==money+earned&&enemy.deadHandled);
                if(kind==0){
                    EVT_REQUIRE(game.enemies().size()==1&&!enemy.sourceFade.active()&&enemy.displayOpacity()==victim.opacity()&&game.wave().result()==MusouResult::Won&&game.presentation().ending());
                    EVT_REQUIRE(!player.sourceActionEnabled()&&!victim.sourceActionEnabled());
                    for(unsigned i=0;i<100;++i)game.step({},false);
                    EVT_REQUIRE(game.enemies().size()==1&&game.enemies().front().motion.deathComplete()&&game.kills()==1&&progress.money()==money+earned);++bosses;
                }else{
                    const auto id=enemy.id;EVT_REQUIRE(enemy.sourceFade.active()&&enemy.sourceFade.alpha()==1&&game.wave().result()==MusouResult::Playing);
                    game.manualPause();unsigned ticks=0;
                    for(;ticks<120&&std::ranges::any_of(game.enemies(),[&](const auto& e){return e.id==id;});++ticks)game.step({},false);
                    EVT_REQUIRE(ticks<120&&game.enemies().size()==5&&game.kills()==1&&progress.money()==money+earned);game.manualResume();++minions;
                }
            }
            word(hz);word(kind);word(game.kills());word(progress.money());word(unsigned(game.wave().result()));
        }
        return true;
    }
    bool run(const std::string& folder){
        if(!reference(folder))return false;StageAsset stage;FighterAsset art;EVT_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));EVT_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str()));return actorCases(stage,art)&&sessions(folder,stage,art);
    }
#undef EVT_REQUIRE
};
}
