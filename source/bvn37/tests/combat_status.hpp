// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include "shadow_pixels.hpp"
#include <bit>
#include <cstdio>
#include <cmath>

namespace bvn::status_qa {
struct Checks {
    unsigned error=0,states=0,colors=0,views=0,callbacks=0,zeroAngles=0,integrated=0,defenses=0;
    std::uint64_t hash=14695981039346656037ULL;
    void word(unsigned v){for(unsigned i=0;i<4;++i)hash=(hash^((v>>(8*i))&255))*1099511628211ULL;}
#define STATUS_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
    bool reference(const std::string& folder){
        FILE* f=std::fopen((folder+"STATUS.IN").c_str(),"r");STATUS_REQUIRE(f);
        unsigned count;STATUS_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==7200);
        FightEffects fx;std::array<std::shared_ptr<EffectTarget>,2> targets;
        constexpr std::string_view names[]{"fire_ing","ice_ing","shock_ing","steel_hit_kan"};
        for(unsigned row=0;row<count;++row){
            unsigned scenario,hz,seed,tick,emit,target,freeze,animate,yellow;
            STATUS_REQUIRE(std::fscanf(f,"%u %u %u %u %u %u %u %u %u",&scenario,&hz,&seed,&tick,&emit,&target,&freeze,&animate,&yellow)==9);
            STATUS_REQUIRE(scenario==row/240&&tick==row%240);
            if(!tick){
                fx=FightEffects{};fx.setProfile(hz,30);fx.seedCosmetics(seed);
                for(unsigned id=0;id<2;++id){
                    targets[id]=std::make_shared<EffectTarget>();targets[id]->baseTint={int(id*7),int(id*11),int(id*13)};
                    targets[id]->resumeColor();targets[id]->facing=id?-1:1;
                }
            }
            std::array<ShadowTint,2> expectedTint;
            for(unsigned id=0;id<2;++id){
                auto& t=*targets[id];unsigned super;int r,g,b;
                STATUS_REQUIRE(std::fscanf(f,"%lf %lf %d %u %d %d %d",&t.position.x,&t.position.y,&t.state,&super,&r,&g,&b)==7);
                t.steel=true;t.superSteel=super;expectedTint[id]={r,g,b};
            }
            if(emit){
                STATUS_REQUIRE(emit<=4&&target<2);const auto* effect=combatEffect(names[emit-1]);STATUS_REQUIRE(effect);
                if(emit==4){STATUS_REQUIRE(fx.emit(*effect,targets[target]->position,targets[target]->facing));fx.setLastTarget(targets[target]);}
                else fx.special(*effect,targets[target]);
                if(yellow){
                    STATUS_REQUIRE(emit==4&&fx.scene().shines.back().color==0xffff00&&fx.scene().shines.back().alpha==76);
                }
                if(effect->randomRotation&&fx.scene().combat.back().rotation==0)++zeroAngles;
            }
            if(freeze){fx.freezeOver();++callbacks;}
            fx.step(animate);
            for(unsigned id=0;id<2;++id){
                if(!(targets[id]->tint==expectedTint[id]))std::fprintf(stderr,"tint row %u target %u expected %d %d %d actual %d %d %d\n",row,id,
                    expectedTint[id].red,expectedTint[id].green,expectedTint[id].blue,targets[id]->tint.red,targets[id]->tint.green,targets[id]->tint.blue);
                STATUS_REQUIRE(targets[id]->tint==expectedTint[id]);
                for(int v:{targets[id]->tint.red,targets[id]->tint.green,targets[id]->tint.blue}){word(unsigned(v));}
                ++colors;
            }
            unsigned active;STATUS_REQUIRE(std::fscanf(f,"%u",&active)==1);
            const auto actual=fx.scene().combat;
            if(actual.size()!=active)std::fprintf(stderr,"active row %u expected %u actual %u\n",row,active,unsigned(actual.size()));
            STATUS_REQUIRE(actual.size()==active);
            for(unsigned i=0;i<active;++i){
                unsigned effect,id,frame,cursor,rotation,finished;int facing,offsetFacing;double x,y;
                STATUS_REQUIRE(std::fscanf(f,"%u %u %lf %lf %u %u %d %u %d %u",&effect,&id,&x,&y,&frame,&cursor,&facing,&rotation,&offsetFacing,&finished)==10);
                const auto& v=actual[i];
                if(v.frame()!=frame||v.cursor!=cursor)std::fprintf(stderr,"frame row %u view %u expected %u/%u actual %u/%u\n",row,i,frame,cursor,v.frame(),v.cursor);
                STATUS_REQUIRE(v.effect==&combatEffects()[effect]&&v.target==targets[id]);
                STATUS_REQUIRE(v.origin.x==x&&v.origin.y==y&&v.frame()==frame&&v.cursor==cursor);
                if(v.facing!=facing||v.rotation!=rotation||v.offsetFacing!=offsetFacing||v.finished!=bool(finished))
                    std::fprintf(stderr,"pose row %u view %u expected %d %u %d %u actual %d %u %d %u\n",row,i,
                        facing,rotation,offsetFacing,finished,v.facing,v.rotation,v.offsetFacing,unsigned(v.finished));
                STATUS_REQUIRE(v.facing==facing&&v.rotation==rotation&&v.offsetFacing==offsetFacing&&v.finished==bool(finished));
                for(unsigned n:{effect,id,frame,cursor,rotation,finished}){word(n);}
                ++views;
            }
            ++states;
        }
        std::fclose(f);STATUS_REQUIRE(zeroAngles==6&&callbacks==60);return true;
    }
    bool matches(const std::string& folder){
        FighterAsset asset;StageAsset stage;
        STATUS_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        STATUS_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        for(unsigned type:{7U,8U,9U})for(unsigned hz:{30U,60U}){
            FightMatch match(asset,asset);match.reset(stage,{350,stage.geometry().playerBottom},{390,stage.geometry().playerBottom});
            match.setEffectProfile(hz,30);for(unsigned side=0;side<2;++side)match.actor(side).setGameHz(hz);
            FighterStrike hit{};hit.damage=1;hit.hitType=type;hit.key=7;hit.hurtFrames=30;hit.hitX=1;
            match.actor(1).receiveStrike({hit,1,350,1,-1,{},true,{370,500,395,550}});
            match.step(stage.geometry(),{},false,Contacts::Locked);
            const auto* extra=combatEffect(type==7?"fire_ing":type==8?"ice_ing":"shock_ing");
            STATUS_REQUIRE(match.scene().combat.size()==2&&match.scene().combat.back().effect==extra);
            const auto& tint=extra->targetTint;
            STATUS_REQUIRE((match.actor(1).visualTint()==ShadowTint{tint[0],tint[1],tint[2]}));
            bool followed=false,finished=false,restored=false;
            for(unsigned tick=0;tick<260;++tick){
                match.step(stage.geometry(),{},tick%2==0,Contacts::Locked);
                for(const auto& v:match.scene().combat)if(v.effect==extra){
                    if(!v.finished){
                        // SpecialEffectView.render follows at FPS_GAME, not at
                        // every 60 Hz native bridge substep.
                        if(hz==60||tick%2==0)STATUS_REQUIRE(v.origin.x==v.target->position.x&&v.origin.y==v.target->position.y);
                        followed=true;
                    }
                    else finished=true;
                }
                restored|=match.actor(1).visualTint()==ShadowTint{};
            }
            STATUS_REQUIRE(followed&&finished&&restored&&match.scene().combat.empty());++integrated;
        }
        for(unsigned hz:{30U,60U}){
            FightMatch match(asset,asset);match.reset(stage,{350,stage.geometry().playerBottom},{390,stage.geometry().playerBottom});
            match.setEffectProfile(hz,30);match.actor(1).setQi(300);match.actor(1).present(FighterCommand::SkillDown);
            match.actor(1).step(stage.geometry(),{},true);match.actor(1).takeEffects();STATUS_REQUIRE(match.actor(1).steel());
            FighterStrike hit{};hit.damage=10;hit.hitType=1;hit.key=7;hit.hurtFrames=10;
            match.actor(1).receiveStrike({hit,1,350,1,-1,{},true,{370,500,395,550}});
            match.step(stage.geometry(),{},false,Contacts::Locked);
            STATUS_REQUIRE((match.actor(1).visualTint()==ShadowTint{150,150,150}));
            STATUS_REQUIRE(match.hitFreezeFrames()==400*hz/1000);
            for(unsigned tick=0;match.hitFreeze();++tick){
                STATUS_REQUIRE(tick<100);match.step(stage.geometry(),{},tick%2==0,Contacts::Locked);
                if(match.hitFreeze())STATUS_REQUIRE((match.actor(1).visualTint()==ShadowTint{150,150,150}));
            }
            match.step(stage.geometry(),{},false,Contacts::Locked);
            STATUS_REQUIRE(match.actor(1).visualTint()==ShadowTint{});++integrated;
        }
        return true;
    }
    bool defense(const std::string& folder){
        FighterAsset asset;STATUS_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        const StageGeometry stage{0,4000,2000,1000,{}};
        FILE* f=std::fopen((folder+"DEFENSE.IN").c_str(),"r");STATUS_REQUIRE(f);
        unsigned count;STATUS_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==152);
        for(unsigned row=0;row<count;++row){
            unsigned type,time;int defense,facing,direct;char id[64];double x,y,px,py;
            STATUS_REQUIRE(std::fscanf(f,"%u %d %d %63s %lf %lf %u %lf %lf %d",&type,&defense,&facing,id,&x,&y,&time,&px,&py,&direct)==10);
            FighterMotion actor(asset);actor.reset({1700,1000},-facing);actor.setDefenseType(defense);
            for(unsigned tick=0;tick<8;++tick)actor.step(stage,{.vertical=-1},tick%2==0);
            actor.takeEffects();FighterStrike hit{};hit.damage=1;hit.hitType=type;hit.key=7;hit.hurtFrames=10;
            actor.receiveStrike({hit,facing,1700-facing*40.,1,-1,{},true,{1691.5,945,1712.5,985}});
            const auto requests=actor.takeEffects();STATUS_REQUIRE(requests.contactEffects.size()==1);
            const auto& r=requests.contactEffects.front();
            STATUS_REQUIRE(!r.target);
            STATUS_REQUIRE(r.effect->id==id&&r.shakeX()==x&&r.shakeY()==y&&r.effect->shakeTimeMs==time);
            STATUS_REQUIRE(r.origin.x==px&&r.origin.y==py&&r.facing==direct);
            word(type);word(unsigned(defense));word(unsigned(y));++defenses;
        }
        std::fclose(f);return true;
    }
    bool run(const std::string& folder){return reference(folder)&&matches(folder)&&defense(folder);}
#undef STATUS_REQUIRE
};
}
