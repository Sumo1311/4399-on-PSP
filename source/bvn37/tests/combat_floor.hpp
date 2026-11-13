// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "floor_effects.hpp"
#include "fight_match.hpp"
#include <cstdio>
#include <cmath>
#include <string>

namespace bvn::floor_qa {
struct Checks {
    unsigned error=0,states=0,banks=0,flights=0,impacts=0,sounds=0;
    std::uint64_t hash=14695981039346656037ULL;
    void word(unsigned v){for(unsigned i=0;i<4;++i)hash=(hash^((v>>(8*i))&255))*1099511628211ULL;}
#define FLOOR_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
    static constexpr std::string_view ids[]{"hit_floor","hit_floor_low","hit_floor_heavy","hit_floor_yan"};
    bool reference(const std::string& folder) {
        FILE* f=std::fopen((folder+"FLOOR.IN").c_str(),"r");FLOOR_REQUIRE(f);
        unsigned count;FLOOR_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==772);
        for(unsigned row=0;row<count;++row) {
            unsigned mode,type,kind,hasShake,n;double distance,y,power,velocity,shake;
            FLOOR_REQUIRE(std::fscanf(f,"%u %u %u %lf %lf %lf %lf %u %lf %u",&mode,&type,&kind,&distance,&y,&power,&velocity,&hasShake,&shake,&n)==10);
            FighterEffects requests{};double actual=0;const Point origin{-25.5,720.125};
            if(mode==0)appendFloorImpact(requests,FloorImpact(type),origin,power,kind==2);
            else if(mode==1)actual=appendLandingImpact(requests,type!=0,origin,distance,y,kind==2);
            else appendFloorImpact(requests,FloorImpact::Normal,origin,2,kind==2);
            FLOOR_REQUIRE(actual==velocity&&requests.shakeRequested==bool(hasShake));
            FLOOR_REQUIRE(requests.shakeY==float(shake)&&requests.shakeX==0&&requests.shakeTimeMs==0);
            FLOOR_REQUIRE(requests.contactEffects.size()==n);
            for(unsigned i=0;i<n;++i){unsigned id;FLOOR_REQUIRE(std::fscanf(f,"%u",&id)==1&&id<4);
                const auto& r=requests.contactEffects[i];FLOOR_REQUIRE(r.effect==combatEffect(ids[id]));
                FLOOR_REQUIRE(r.origin.x==origin.x&&r.origin.y==origin.y&&r.facing==1&&!r.identity&&!r.target);
                word(id);++impacts;
            }
            word(hasShake);word(unsigned(shake*8+100));++states;
        }
        std::fclose(f);return true;
    }
    bool flight(const FighterAsset& asset,unsigned hz,int facing,bool enemy,unsigned kind) {
        StageGeometry stage{0,2000,800,500,{}};
        FighterMotion actor(asset);actor.reset({900,500},-facing);actor.setGameHz(hz);actor.setMusouEnemyEffects(enemy);
        FighterStrike hit{};hit.key=17;hit.damage=kind==2?60000:1;hit.flags=kind==2?0:1;
        hit.hitX=4;hit.hitY=kind==1?8:-8;hit.hurtFrames=6;
        FLOOR_REQUIRE(actor.receiveStrike({hit,facing,700}));actor.takeEffects();
        unsigned found[4]{};
        for(unsigned tick=0;tick<400;++tick) {
            actor.step(stage,{},tick%2==0);
            const auto requests=actor.takeEffects();
            for(const auto& r:requests.contactEffects)for(unsigned id=0;id<4;++id)if(r.effect->id==ids[id]){
                ++found[id];++impacts;word(id);word(tick);word(hz);
                FLOOR_REQUIRE(r.facing==1&&!r.target&&!r.identity);
                FLOOR_REQUIRE(r.origin.x==actor.motion().position.x&&r.origin.y==actor.motion().position.y);
                FLOOR_REQUIRE(requests.shakeRequested&&requests.shakeY>0);
                FLOOR_REQUIRE(!enemy||requests.shakeY==2);
                if(id!=3){FLOOR_REQUIRE(!r.effect->sound.empty());++sounds;}
            }
            if((kind==0&&found[1])||(kind==1&&found[3]))break;
        }
        if(kind==0)FLOOR_REQUIRE(found[0]==1&&found[1]==1&&!found[2]&&!found[3]);
        else if(kind==1)FLOOR_REQUIRE(!found[0]&&!found[1]&&found[2]==1&&found[3]==1);
        else FLOOR_REQUIRE(!found[0]&&found[1]==1&&!found[2]&&!found[3]);
        ++flights;return true;
    }
    bool actors(const std::string& folder) {
        FILE* f=std::fopen((folder+"BANKS.IN").c_str(),"r");FLOOR_REQUIRE(f);char name[80];
        while(std::fscanf(f,"%79s",name)==1){
            FighterAsset asset;FLOOR_REQUIRE(asset.open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory));
            for(unsigned hz:{30U,60U})for(int facing:{-1,1})for(bool enemy:{false,true})for(unsigned kind=0;kind<3;++kind)
                FLOOR_REQUIRE(flight(asset,hz,facing,enemy,kind));
            ++banks;
        }
        std::fclose(f);FLOOR_REQUIRE(banks==78);return true;
    }
    bool match(const std::string& folder) {
        FighterAsset asset;StageAsset stage;
        FLOOR_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        FLOOR_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        FightMatch match(asset,asset);match.reset(stage,{350,stage.geometry().playerBottom},{500,stage.geometry().playerBottom});
        FighterStrike hit{};hit.key=17;hit.damage=1;hit.flags=1;hit.hitX=0;hit.hitY=8;hit.hurtFrames=6;
        FLOOR_REQUIRE(match.actor(1).receiveStrike({hit,1,350}));
        bool heavy=false,smoke=false,heard=false,cleared=false;
        for(unsigned tick=0;tick<180;++tick){
            match.step(stage.geometry(),{},tick%2==0,Contacts::Locked);
            for(const auto& v:match.scene().combat){heavy|=v.effect->id==ids[2];smoke|=v.effect->id==ids[3];}
            for(auto s:match.takeCombatSounds())heard|=s=="snd_hitfloor_heavy";
            if(heavy&&smoke&&match.scene().combat.empty())cleared=true;
        }
        FLOOR_REQUIRE(heavy&&smoke&&heard&&cleared);word(match.emittedEffects());return true;
    }
    bool run(const std::string& folder){return reference(folder)&&actors(folder)&&match(folder);}
#undef FLOOR_REQUIRE
};
} // namespace bvn::floor_qa
