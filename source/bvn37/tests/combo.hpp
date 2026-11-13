// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_combo.hpp"
#include "fight_match.hpp"
#include "musou_battle.hpp"
#include "ui_asset.hpp"
#include <cmath>
#include <cstdio>
#include <string>
namespace bvn::combo_qa {
struct Checks {
    unsigned error=0,rows=0,actors=0,matches=0,musou=0,assets=0,failedRow=0,failedColumn=0;
    double actual=0,expected=0;std::uint64_t hash=14695981039346656037ull;
    void word(unsigned v){for(unsigned i=0;i<4;++i)hash=(hash^((v>>(8*i))&255))*1099511628211ull;}
    void number(double v){word(unsigned(std::llround(v*1000000)));}
#define COMBO_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
    bool reference(const std::string& folder){
        FILE* f=std::fopen((folder+"COMBO.IN").c_str(),"r");COMBO_REQUIRE(f);unsigned count;COMBO_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==12288);
        SourceGameHits hits;SourceMusouHits shared;std::array<SourceHitsDisplay,3> ui;unsigned score=0,updates=0,calls=0;
        for(unsigned row=0;row<count;++row){
            std::array<double,51> v{};for(auto& x:v)COMBO_REQUIRE(std::fscanf(f,"%lf",&x)==1);
            if(v[1]==0){hits={};shared={};ui={};score=updates=calls=0;}
            for(auto& view:ui)view.render();
            const auto op=unsigned(v[2]),a=unsigned(v[3]),b=unsigned(v[4]),c=unsigned(v[5]),value=unsigned(v[6]),flags=unsigned(v[8]);
            if(op==1){const auto n=hits.add(a,b,c+1);if(n>1)ui[c].show(n,c==0);}
            if(op==2)if(const auto id=hits.clearTarget(b))ui[*id-1].hide();
            if(op==3){const auto n=shared.add(b);if(n>1)ui[2].show(n,false);}
            if(op==4&&shared.remove(b))ui[2].hide();
            if(op==5)ui[c].show(value,c==0);if(op==6)ui[c].hide();
            if(op==7){score+=sourceHitScore(v[7],flags&1,flags&2,flags&4,flags&8,flags&32);++updates;}
            if(op==8&&a==0&&b==0&&sourceHitTargetState(AiState(c)))++calls;
            std::array<double,42> got{};unsigned col=0;
            for(unsigned id=0;id<4;++id){const auto e=std::ranges::find(hits.entries(),id,&SourceGameHits::Entry::actor);got[col++]=e==hits.entries().end()?0:e->hits;got[col++]=e==hits.entries().end()?0:e->target+1;got[col++]=e==hits.entries().end()?0:e->ui;}
            unsigned bits=0;for(unsigned id:shared.targets())bits|=1U<<id;got[col++]=shared.count();got[col++]=bits;got[col++]=shared.targets().size();
            for(const auto& view:ui){got[col++]=view.number();got[col++]=view.frame();got[col++]=view.shown();got[col++]=view.visible();got[col++]=view.playing();got[col++]=view.offset();got[col++]=view.shift();got[col++]=SourceHitsDisplay::numberWidth(view.number());}
            got[col++]=score;got[col++]=updates;got[col++]=calls;COMBO_REQUIRE(col==42);
            for(unsigned i=0;i<42;++i){if(std::abs(got[i]-v[9+i])>1e-9){failedRow=row;failedColumn=i;actual=got[i];expected=v[9+i];error=__LINE__;std::fclose(f);return false;}number(got[i]);}
            ++rows;
        }
        std::fclose(f);return true;
    }
    static FighterMotion& actor(const FighterMotion& a){return const_cast<FighterMotion&>(a);}
    static StrikeContact hit(unsigned key,bool launch=false){FighterStrike h{};h.key=key;h.damage=10;h.hurtFrames=180;h.hurtTimeMs=5900;h.hitType=1;h.hitY=launch?8:0;h.flags=launch?1:0;return {h,1,400};}
    bool actorCases(const StageAsset& stage,const FighterAsset& art){
        for(unsigned hz:{30u,60u}){
            FighterMotion a(art),b(art);a.sourceHz(hz);b.sourceHz(hz);a.reset({400,stage.geometry().playerBottom},1);b.reset({450,stage.geometry().playerBottom},-1);a.setQi(0);
            struct Observer{FighterMotion* actor;unsigned calls=0,owner=0,target=0;double qi=0;}o{&a};
            a.sourceHooks({&o,7,nullptr,nullptr,nullptr,[](void* p,unsigned owner,unsigned target,const StrikeContact&){auto& o=*static_cast<Observer*>(p);++o.calls;o.owner=owner;o.target=target;o.qi=o.actor->qi();}});b.sourceHooks({nullptr,9});
            const auto first=hit(700);COMBO_REQUIRE(b.receiveStrike(first,&a));a.scoreStrike(first,&b);
            COMBO_REQUIRE(o.calls==1&&o.owner==7&&o.target==9&&std::abs(o.qi-1.7)<1e-12);++actors;
            // A minion's weak hit leaves a normal target outside HitTarget's gate.
            b.reset({450,stage.geometry().playerBottom},-1);a.setMusouEnemyEffects(true);a.setQi(0);COMBO_REQUIRE(b.receiveStrike(first,&a));COMBO_REQUIRE(b.aiState()==AiState::Normal);a.scoreStrike(first,&b);COMBO_REQUIRE(o.calls==1&&std::abs(a.qi()-1.7)<1e-12);++actors;
        }
        return true;
    }
    bool matchCases(const StageAsset& stage,const FighterAsset& art){
        for(unsigned hz:{30u,60u}){
            FightMatch match(art,art);match.reset(stage,{400,stage.geometry().playerBottom},{450,stage.geometry().playerBottom});match.setSourceProfile(hz,30,stage);match.sourceArcadeScoring(true);match.setClassicScore(false);
            auto& a=match.actor(0);auto& b=match.actor(1);
            for(unsigned serial=0;serial<2;++serial){const auto h=hit(800+serial);COMBO_REQUIRE(b.receiveStrike(h,&a));a.scoreStrike(h,&b);for(unsigned i=0;i<4;++i)b.step(stage.geometry(),{},true);}
            COMBO_REQUIRE(match.comboHits().count(0)==2&&match.comboUi(0).shown()&&match.comboUi(0).number()==2&&match.comboUi(0).frame()==10&&match.pointsThisStep()==20);
            // Ordinary Versus does not clear a combo merely on HURT_DOWN.
            auto launch=hit(900,true);COMBO_REQUIRE(b.receiveStrike(launch,&a));a.scoreStrike(launch,&b);bool down=false,cleared=false;
            for(unsigned i=0;i<900;++i){b.step(stage.geometry(),{},true);match.advanceComboUi();if(b.aiState()==AiState::Down){down=true;COMBO_REQUIRE(match.comboHits().count(0)==3&&match.comboUi(0).shown());}if(down&&b.standing()){cleared=true;COMBO_REQUIRE(match.comboHits().count(0)==0&&!match.comboUi(0).shown());break;}}
            COMBO_REQUIRE(down&&cleared);++matches;word(hz);word(match.comboUi(0).frame());
        }
        return true;
    }
    bool musouCases(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
        for(unsigned hz:{30u,60u}){
            MusouCampaign campaign;COMBO_REQUIRE(campaign.open((folder+"MUSMIN.BVM").c_str()));MusouProgress progress(campaign);std::array<MusouMember,3> members{};
            const MusouFighterBank bank{"qa-player",std::span(&art,1),0,1000,0},foe{"qa-enemy",std::span(&art,1),0,1000,0};MusouBattle game;
            COMBO_REQUIRE(game.start(campaign,0,stage,{bank,bank,bank},std::span(&foe,1),progress,members,0x12345678));game.setSourceProfile(hz,30);
            for(unsigned i=0;i<1500&&(game.enemies().size()<6||game.presentation().starting());++i)game.step({},false);COMBO_REQUIRE(game.enemies().size()==6);
            auto& a=actor(game.member(0));auto e=game.enemies().begin();auto& b=actor(e++->motion);auto& c=actor(e->motion);
            a.reset({400,stage.geometry().playerBottom},1);b.reset({450,stage.geometry().playerBottom},-1);c.reset({550,stage.geometry().playerBottom},-1);
            auto first=hit(1000,true),second=hit(1000);COMBO_REQUIRE(b.receiveStrike(first,&a));a.scoreStrike(first,&b);COMBO_REQUIRE(c.receiveStrike(second,&a));a.scoreStrike(second,&c);
            COMBO_REQUIRE(game.comboHits().count()==2&&game.comboHits().targets().size()==2&&game.comboUi().shown());
            bool down=false;for(unsigned i=0;i<900;++i){b.step(stage.geometry(),{},true);if(b.aiState()==AiState::Down){down=true;break;}}
            COMBO_REQUIRE(down&&game.comboHits().count()==2&&game.comboHits().targets().size()==1&&game.comboUi().shown());
            for(unsigned i=0;i<8;++i)c.step(stage.geometry(),{},true);COMBO_REQUIRE(game.comboHits().count()==2&&game.comboHits().targets().size()==1);c.setHealth(1);auto fatal=hit(1001);COMBO_REQUIRE(c.receiveStrike(fatal,&a));
            // DIE removes the last target before the attacker's subsequent
            // HitTarget callback adds the newly hurting dead actor again.
            COMBO_REQUIRE(game.comboHits().count()==0&&game.comboHits().targets().empty()&&!game.comboUi().shown());a.scoreStrike(fatal,&c);
            COMBO_REQUIRE(game.comboHits().count()==1&&game.comboHits().targets().size()==1&&!game.comboUi().shown());++musou;word(hz);word(game.comboHits().count());
        }
        return true;
    }
    bool artCases(const std::string& folder){
        UiAsset art;COMBO_REQUIRE(art.open((folder+"COMBO.BVU").c_str()));
        for(const auto& digit:ComboDigitArt){const auto* sprite=art.find(digit.key);COMBO_REQUIRE(sprite&&sprite->width==unsigned(std::round(digit.w*272/600))&&sprite->height==unsigned(std::round(digit.h*272/600)));++assets;}
        COMBO_REQUIRE(art.find("combo-label"));++assets;return true;
    }
    bool run(const std::string& folder){
        if(!reference(folder)||!artCases(folder))return false;StageAsset stage;FighterAsset art;COMBO_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));COMBO_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str()));return actorCases(stage,art)&&matchCases(stage,art)&&musouCases(folder,stage,art);
    }
#undef COMBO_REQUIRE
};
}
