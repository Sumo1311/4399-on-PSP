// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include <cstdio>
#include <string>

namespace bvn::combat_playback_qa {
struct Checks {
    unsigned error=0,shakeStates=0,caps=0,lifetimes=0,caches=0,freezes=0;
    std::uint64_t hash=14695981039346656037ULL;
    void word(int v){for(unsigned i=0;i<4;++i)hash=(hash^((unsigned(v)>>(i*8))&255))*1099511628211ULL;}
#define PLAY_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
    bool shake(const std::string& folder){
        FILE* file=std::fopen((folder+"SHAKE.IN").c_str(),"r");PLAY_REQUIRE(file);
        unsigned count;PLAY_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==12960);
        FightEffects fx;
        for(unsigned row=0;row<count;++row){
            unsigned scenario,tick,hold,pulse,animate;
            float hx,hy,px,py,time,bx,by,ax,ay;
            PLAY_REQUIRE(std::fscanf(file,"%u %u %u %f %f %u %f %f %f %u %f %f %f %f",
                &scenario,&tick,&hold,&hx,&hy,&pulse,&px,&py,&time,&animate,&bx,&by,&ax,&ay)==14);
            PLAY_REQUIRE(scenario==row/180&&tick==row%180);
            if(!tick){fx=FightEffects{};fx.setProfile(scenario%2?30:60,30);}
            FighterEffects request;
            if(hold){request.shakeLoopX=hx;request.shakeLoopY=hy;request.startShake=hold==1;}
            if(pulse){request.shakeX=px;request.shakeY=py;request.shakeTimeMs=time;request.shakeRequested=true;}
            fx.play(request);
            PLAY_REQUIRE(fx.scene().shiftX==bx&&fx.scene().shiftY==by);
            fx.step(animate);
            if(fx.scene().shiftX!=ax||fx.scene().shiftY!=ay)
                std::fprintf(stderr,"shake row %u expected %g %g actual %g %g\n",row,ax,ay,fx.scene().shiftX,fx.scene().shiftY);
            PLAY_REQUIRE(fx.scene().shiftX==ax&&fx.scene().shiftY==ay);
            word(int(ax));word(int(ay));++shakeStates;
        }
        std::fclose(file);return true;
    }
    bool limits(){
        const auto* effect=combatEffect("hit_1");PLAY_REQUIRE(effect);
        for(unsigned hz:{30U,60U}){
            FightEffects fx;fx.setProfile(hz,30);
            auto first=std::make_shared<int>(1),second=std::make_shared<int>(2);
            for(unsigned tick=0;tick<8;++tick){
                for(unsigned n=0;n<5;++n){
                    PLAY_REQUIRE(fx.emit(*effect,{1,2},-1,first)==(n<3));
                    PLAY_REQUIRE(fx.emit(*effect,{3,4},1,second)==(n<3));
                    PLAY_REQUIRE(fx.emit(*effect,{5,6},1)==(n<3));++caps;
                }
                fx.step(false);
                // The source frame counter clears at FPS_GAME, independently of animation.
                if(hz==30){PLAY_REQUIRE(!fx.emit(*effect,{},1,first));fx.step(false);++caps;}
            }
            PLAY_REQUIRE(fx.emitted()==8*9);
        }
        FightEffects fx;auto token=std::make_shared<int>(7);std::weak_ptr<int> old=token;
        PLAY_REQUIRE(fx.emit(*effect,{1,2},-1,token)&&fx.emit(*effect,{3,4},1,token));
        PLAY_REQUIRE(fx.scene().combat.size()==2&&fx.scene().combat[0].frame()==0);
        token.reset();PLAY_REQUIRE(!old.expired());
        const unsigned frames=fx.scene().combat[0].art->frames;
        for(unsigned age=1;age<frames;++age){
            fx.step(false);PLAY_REQUIRE(fx.scene().combat[0].age==age-1);
            fx.step(true);PLAY_REQUIRE(fx.scene().combat.size()==2);
            PLAY_REQUIRE(fx.scene().combat[0].age==age&&fx.scene().combat[0].frame()==age-1);++lifetimes;
        }
        fx.step(true);PLAY_REQUIRE(fx.scene().combat.size()==1);
        PLAY_REQUIRE(fx.scene().combat[0].age==frames-1&&!old.expired());
        fx.step(true);PLAY_REQUIRE(fx.scene().combat.empty()&&old.expired());++lifetimes;
        return true;
    }
    bool cache(const std::string& folder){
        FighterAsset asset;PLAY_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        FILE* file=std::fopen((folder+"CACHE.IN").c_str(),"r");PLAY_REQUIRE(file);
        unsigned count;PLAY_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==320);
        std::array<std::shared_ptr<RuntimeHit>,13> hits{};
        const StageGeometry stage{0,4000,2000,1000,{}};
        for(unsigned row=0;row<count;++row){
            unsigned mode,key,type,enemy;int clone,defense;char id[64];double x,y,time;
            PLAY_REQUIRE(std::fscanf(file,"%u %u %d %u %u %d %63s %lf %lf %lf",&mode,&key,&clone,&type,&enemy,&defense,id,&x,&y,&time)==10);
            PLAY_REQUIRE(key<hits.size());
            if(!hits[key]||clone>=0){
                if(clone>=0){PLAY_REQUIRE(hits[clone]);hits[key]=std::make_shared<RuntimeHit>(*hits[clone]);}
                else hits[key]=std::make_shared<RuntimeHit>();
            }
            auto& hit=*hits[key];hit.strike={};hit.strike.damage=1;hit.strike.hitType=type;hit.strike.key=7;hit.strike.hurtFrames=10;
            FighterMotion actor(asset);actor.reset({1700,1000},-1);actor.setDefenseType(defense);actor.setMusouEnemyEffects(enemy);
            if(mode)for(unsigned tick=0;tick<8;++tick)actor.step(stage,{.vertical=-1},tick%2==0);
            actor.takeEffects();
            actor.receiveStrike({hit.strike,1,1660,1,-1,hits[key],true,{1691.5,945,1712.5,985}});
            const auto output=actor.takeEffects();const bool present=std::string_view(id)!="-";
            PLAY_REQUIRE(output.contactEffects.size()==unsigned(present));
            if(present){
                const auto& r=output.contactEffects[0];
                if(r.effect->id!=id)std::fprintf(stderr,"cache row %u expected %s actual %.*s\n",row,id,int(r.effect->id.size()),r.effect->id.data());
                PLAY_REQUIRE(r.effect->id==id&&r.identity==hits[key]);
                // CACHE.IN stops at EffectManager. Defense pow promotion now
                // has a separate full EffectHitHandler-route reference.
                const double managerY=mode?r.effect->shakeY:r.shakeY();
                PLAY_REQUIRE(r.effect->shakeX==x&&managerY==y&&r.effect->shakeTimeMs==time);
                word(int(r.effect-combatEffects().data()));word(int(y));
            }
            ++caches;
        }
        std::fclose(file);return true;
    }
    bool freeze(const std::string& folder){
        FighterAsset asset;StageAsset stage;
        PLAY_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        PLAY_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        FILE* file=std::fopen((folder+"FREEZE.IN").c_str(),"r");PLAY_REQUIRE(file);
        unsigned count;PLAY_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==280);
        std::unique_ptr<FightMatch> match;
        unsigned phase=0,held=0,animated=0,resumed=0;
        for(unsigned row=0;row<count;++row){
            unsigned scenario,hz,presteps,tick,before,locked,after,paused;
            PLAY_REQUIRE(std::fscanf(file,"%u %u %u %u %u %u %u %u",&scenario,&hz,&presteps,&tick,&before,&locked,&after,&paused)==8);
            if(!tick){
                match=std::make_unique<FightMatch>(asset,asset);match->reset(stage,{350,stage.geometry().playerBottom},{390,stage.geometry().playerBottom});
                match->setEffectProfile(hz,30);for(unsigned side=0;side<2;++side)match->actor(side).setGameHz(hz);
                for(unsigned i=0;i<presteps;++i)match->step(stage.geometry(),{},false,Contacts::Locked);
                FighterStrike strike{};strike.damage=1;strike.hitType=3;strike.key=7;strike.hurtFrames=10;
                match->actor(1).receiveStrike({strike,1,350,1,-1,{},true,{370,920,395,950}});
                match->step(stage.geometry(),{},false,Contacts::Locked);
                PLAY_REQUIRE(match->emittedEffects()==1&&match->hitFreezeFrames()==400*hz/1000);
                PLAY_REQUIRE(match->takeCombatSounds().size()==1);
            }
            PLAY_REQUIRE(match->hitFreezeFrames()==before&&match->hitFreeze()==bool(locked));
            const auto position=match->actor(1).motion().position;
            const unsigned timeline=match->actor(1).timelineFrame(),ai=match->aiAnimation(1);
            const auto effects=match->scene().combat;
            const unsigned age=effects.empty()?0:effects.front().age;
            match->step(stage.geometry(),{{{.horizontal=1},{}}},tick%2==0,Contacts::Locked);
            PLAY_REQUIRE(match->hitFreezeFrames()==after&&bool(after)==bool(paused));
            if(locked){
                PLAY_REQUIRE(match->actor(1).motion().position.x==position.x&&match->actor(1).motion().position.y==position.y);
                PLAY_REQUIRE(match->actor(1).timelineFrame()==timeline&&match->aiAnimation(1)==ai);++held;
                if(!match->scene().combat.empty()&&match->scene().combat.front().age>age)++animated;
            }else if(match->actor(1).timelineFrame()!=timeline)++resumed;
            word(after);word(locked);++freezes;
            phase=scenario;
        }
        std::fclose(file);PLAY_REQUIRE(phase==3&&held&&animated&&resumed);
        const auto emitted=match->emittedEffects();match->flashKnockout(1);
        PLAY_REQUIRE(!match->hitFreezeFrames()&&match->emittedEffects()==emitted+1);
        PLAY_REQUIRE(!match->scene().shines.empty()&&match->scene().shines.back().alpha==127);
        PLAY_REQUIRE(match->scene().combat.back().effect==combatEffect("hit_end"));
        return true;
    }
    bool run(const std::string& folder){return shake(folder)&&limits()&&cache(folder)&&freeze(folder);}
#undef PLAY_REQUIRE
};
}
