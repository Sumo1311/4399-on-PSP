// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include "musou_battle.hpp"
#include <cstdio>
#include <string>

namespace bvn::ko_qa {
struct Checks {
    unsigned error=0,states=0,views=0,matches=0,holds=0;
    std::uint64_t hash=14695981039346656037ULL;
    void word(unsigned v){for(unsigned i=0;i<4;++i)hash=(hash^((v>>(i*8))&255))*1099511628211ULL;}
#define KO_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
    bool reference(const std::string& folder){
        FILE* f=std::fopen((folder+"BACK.IN").c_str(),"r");KO_REQUIRE(f);
        unsigned rows;KO_REQUIRE(std::fscanf(f,"%u",&rows)==1&&rows==1920);
        FightEffects effects;const CombatEffect* data[]{combatEffect("kobg"),combatEffect("hit_floor")};
        for(unsigned row=0;row<rows;++row){
            unsigned hz,variant,tick,animate,n;KO_REQUIRE(std::fscanf(f,"%u %u %u %u %u",&hz,&variant,&tick,&animate,&n)==5);
            if(!tick){effects=FightEffects{};effects.setProfile(hz,30);}
            for(unsigned i=0;i<n;++i){unsigned id;double hold;KO_REQUIRE(std::fscanf(f,"%u %lf",&id,&hold)==2&&id<2);effects.background(*data[id],hold);}
            effects.step(animate);unsigned visible,active;KO_REQUIRE(std::fscanf(f,"%u %u",&visible,&active)==2);
            const auto scene=effects.scene();KO_REQUIRE(scene.hideMap==!visible&&scene.backgrounds.size()==active&&scene.combat.empty());
            for(unsigned i=0;i<active;++i){unsigned id,cursor,frame;int hold;KO_REQUIRE(std::fscanf(f,"%u %u %u %d",&id,&cursor,&frame,&hold)==4);
                const auto& v=scene.backgrounds[i];
                if(v.effect!=data[id]||v.cursor!=cursor||v.frame()!=frame||v.holdFrame!=hold)std::fprintf(stderr,"background row %u view %u expected %u/%u/%d actual %u/%u/%d\n",row,i,cursor,frame,hold,v.cursor,v.frame(),v.holdFrame);
                KO_REQUIRE(v.effect==data[id]&&v.cursor==cursor&&v.frame()==frame&&v.holdFrame==hold&&v.background);
                KO_REQUIRE(v.origin.x==0&&v.origin.y==0&&v.facing==1);
                word(id);word(cursor);word(frame);word(unsigned(hold));++views;
            }
            word(visible);word(active);++states;
        }
        std::fclose(f);return true;
    }
    bool match(const std::string& folder){
        FighterAsset asset;StageAsset stage;KO_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        KO_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        for(unsigned hz:{30U,60U})for(unsigned kind=0;kind<3;++kind){
            FightMatch m(asset,asset);m.reset(stage,{350,stage.geometry().playerBottom},{500,stage.geometry().playerBottom});m.setEffectProfile(hz,30);
            FighterStrike hit{};hit.key=17;hit.hitType=3;hit.damage=1;hit.hurtFrames=6;
            KO_REQUIRE(m.actor(1).receiveStrike({hit,1,350,1,-1,{},true,
                {480,stage.geometry().playerBottom-80,520,stage.geometry().playerBottom}}));
            m.step(stage.geometry(),{},false,Contacts::Locked);
            const unsigned frozen=m.hitFreezeFrames();KO_REQUIRE(frozen>0);
            if(kind)m.actor(0).present(kind==1?FighterCommand::Super:FighterCommand::SuperDown);
            m.flashKnockout(1);KO_REQUIRE(m.superKnockout()==bool(kind)&&!m.hitFreeze()&&m.hitFreezeFrames()==frozen);
            KO_REQUIRE(m.scene().backgrounds.size()==(kind?1U:0U));
            for(unsigned tick=0;tick<120;++tick){m.stepScene(tick%2==0);KO_REQUIRE(m.hitFreezeFrames()==frozen);++holds;}
            m.resumeKnockout();KO_REQUIRE(!m.hitFreeze());
            for(unsigned tick=0;tick<120;++tick)m.stepScene(tick%2==0);
            KO_REQUIRE(!m.hitFreezeFrames()&&m.scene().backgrounds.empty());
            word(frozen);word(kind);++matches;
        }
        return true;
    }
    bool musou(const std::string& folder){
        FighterAsset asset,enemy;StageAsset stage;MusouCampaign campaign;
        KO_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        KO_REQUIRE(enemy.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        KO_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str())&&campaign.open((folder+"MUSLETH.BVM").c_str()));
        const MusouFighterBank bank{"qa-player",std::span(&asset,1),0,1000,0};
        const MusouFighterBank foe{"qa-enemy",std::span(&enemy,1),0,1000,0};
        for(unsigned hz:{30U,60U})for(unsigned kind=0;kind<2;++kind){
            MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle run;
            KO_REQUIRE(run.start(campaign,0,stage,{bank,bank,bank},std::span(&foe,1),progress,members,0x12345678));run.setGameHz(hz,30);
            for(unsigned tick=0;tick<400;++tick){run.step({},false);if(run.enemies().size()==1&&run.bossCue().kind==MusouBossCue::Kind::None)break;}
            KO_REQUIRE(run.enemies().size()==1&&run.bossCue().kind==MusouBossCue::Kind::None);
            auto& player=const_cast<FighterMotion&>(run.member(run.leader()));player.reset({1000,stage.geometry().playerBottom},1);player.setQi(300);
            auto& target=const_cast<MusouOpponent&>(run.enemies().front());target.bossAI.reset();target.motion.reset({1040,stage.geometry().playerBottom},-1);target.motion.setHealth(1);
            player.present(kind?FighterCommand::SuperUp:FighterCommand::Attack);
            for(unsigned tick=0;tick<400&&run.bossCue().kind!=MusouBossCue::Kind::Knockout;++tick){
                target.motion.place({1040,stage.geometry().playerBottom});run.step({},false);
            }
            KO_REQUIRE(run.bossCue().kind==MusouBossCue::Kind::Knockout&&run.bossCue().super==bool(kind));
            KO_REQUIRE(run.scene().backgrounds.size()==(kind?1U:0U));
            for(unsigned tick=0;tick<300;++tick)run.step({},false);
            KO_REQUIRE(run.scene().backgrounds.empty());word(kind);word(hz);++matches;
        }
        return true;
    }
    bool run(const std::string& folder){return reference(folder)&&match(folder)&&musou(folder);}
#undef KO_REQUIRE
};
} // namespace bvn::ko_qa
