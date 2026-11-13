// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include <cstdio>
#include <string>
#include <algorithm>

namespace bvn::buff_qa {
struct Checks {
    unsigned error=0,states=0,views=0,integrated=0,holds=0,renewals=0;
    std::uint64_t hash=14695981039346656037ULL;
    void word(unsigned v) { for(unsigned i=0;i<4;++i) hash=(hash^((v>>(8*i))&255))*1099511628211ULL; }
#define BUFF_REQUIRE(x) do { if(!(x)) { error=__LINE__;return false; } } while(false)
    bool reference(const std::string& folder) {
        FILE* file=std::fopen((folder+"BUFF.IN").c_str(),"r");BUFF_REQUIRE(file);
        unsigned count;BUFF_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==2880);
        FightEffects effects;std::array<std::shared_ptr<EffectTarget>,2> targets;
        std::array<std::shared_ptr<BuffState>,16> nodes;
        constexpr std::string_view bursts[]{"buff_effect_speed","buff_effect_power","buff_effect_defense"};
        constexpr std::string_view auras[]{"buff_speed","buff_power","buff_defense"};
        for(unsigned row=0;row<count;++row) {
            unsigned scenario,hz,tick,animate,kind,action,repeat,target,node;
            BUFF_REQUIRE(std::fscanf(file,"%u %u %u %u %u %u %u %u %u",&scenario,&hz,&tick,&animate,&kind,&action,&repeat,&target,&node)==9);
            BUFF_REQUIRE(scenario==row/240&&tick==row%240&&kind<3&&target<2);
            if(!tick) {
                effects=FightEffects{};effects.setProfile(hz,30);nodes={};
                for(auto& t:targets)t=std::make_shared<EffectTarget>();
            }
            for(auto& t:targets)BUFF_REQUIRE(std::fscanf(file,"%lf %lf %d",&t->position.x,&t->position.y,&t->facing)==3);
            unsigned n;BUFF_REQUIRE(std::fscanf(file,"%u",&n)==1&&n<=nodes.size());
            for(unsigned i=0;i<n;++i) {
                unsigned finished;BUFF_REQUIRE(std::fscanf(file,"%u",&finished)==1);
                if(!nodes[i])nodes[i]=std::make_shared<BuffState>();
                nodes[i]->finished=finished;
            }
            if(action==1) for(unsigned i=0;i<repeat;++i) {
                BUFF_REQUIRE(node<n);
                const auto* burst=combatEffect(bursts[kind]);const auto* aura=combatEffect(auras[kind]);
                BUFF_REQUIRE(burst&&aura);
                effects.emit(*burst,targets[target]->position,1);
                effects.buff({burst,aura,targets[target]->position,targets[target],nodes[node],targets[target]->facing});
            }
            effects.step(animate);
            unsigned active;BUFF_REQUIRE(std::fscanf(file,"%u",&active)==1);
            const auto actual=effects.scene().combat;
            if(actual.size()!=active)std::fprintf(stderr,"buff row %u expected %u views actual %zu\n",row,active,actual.size());
            BUFF_REQUIRE(actual.size()==active);
            for(unsigned i=0;i<active;++i) {
                unsigned effect,owner,buff,cursor,frame;double x,y;int facing;
                BUFF_REQUIRE(std::fscanf(file,"%u %u %u %lf %lf %d %u %u",&effect,&owner,&buff,&x,&y,&facing,&cursor,&frame)==8);
                const auto& v=actual[i];
                if(v.cursor!=cursor||v.frame()!=frame)std::fprintf(stderr,"buff frame row %u view %u expected %u/%u actual %u/%u\n",row,i,cursor,frame,v.cursor,v.frame());
                BUFF_REQUIRE(v.effect==&combatEffects()[effect]&&v.cursor==cursor&&v.frame()==frame);
                BUFF_REQUIRE(v.origin.x==x&&v.origin.y==y&&v.facing==facing);
                BUFF_REQUIRE(v.target==(owner==65535?nullptr:targets[owner]));
                BUFF_REQUIRE(v.buff==(buff==65535?nullptr:nodes[buff]));
                for(unsigned value:{effect,owner,buff,cursor,frame,unsigned(facing)})word(value);
                ++views;
            }
            word(active);++states;
        }
        std::fclose(file);return true;
    }
    static unsigned auras(const FightMatch& match) {
        return unsigned(std::ranges::count_if(match.scene().combat,[](const auto& v){return v.effect->buff;}));
    }
    bool matches(const std::string& folder) {
        FighterAsset asset;StageAsset stage;
        BUFF_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        BUFF_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        for(unsigned hz:{30U,60U})for(auto kind:{FighterBuff::Speed,FighterBuff::Attack,FighterBuff::Defense})for(int facing:{-1,1}) {
            FightMatch match(asset,asset);match.reset(stage,{350,stage.geometry().playerBottom},{450,stage.geometry().playerBottom});
            match.setEffectProfile(hz,30);for(unsigned side=0;side<2;++side)match.actor(side).setGameHz(hz);
            auto& actor=match.actor(0);actor.setFacing(facing);
            actor.applyBuff(kind,kind==FighterBuff::Speed?2.5:.3,120);
            const auto first=actor.buffState(kind);BUFF_REQUIRE(first&&!first->finished);
            match.step(stage.geometry(),{},false,Contacts::Locked);
            BUFF_REQUIRE(auras(match)==1&&match.emittedEffects()==2);
            for(const auto& view:match.scene().combat)if(view.effect->buff) {
                BUFF_REQUIRE(view.buff==first&&view.target==actor.effectTarget()&&view.facing==facing);
            }
            actor.applyBuff(kind,kind==FighterBuff::Speed?1.5:.6,40);
            BUFF_REQUIRE(actor.buffState(kind)==first);++renewals;
            match.step(stage.geometry(),{},false,Contacts::Locked);
            BUFF_REQUIRE(auras(match)==2&&match.emittedEffects()==4&&first->frames==39);
            for(unsigned tick=0;tick<10;++tick)match.step(stage.geometry(),{},tick%2==0,Contacts::Locked);
            if(kind==FighterBuff::Speed) {
                actor.applyBuff(kind,-4,5);BUFF_REQUIRE(actor.buffState(kind)==first);
                const auto before=match.emittedEffects();
                match.step(stage.geometry(),{},false,Contacts::Locked);
                BUFF_REQUIRE(match.emittedEffects()==before&&auras(match)==2);
                BUFF_REQUIRE(actor.speed()==asset.stats().speed-4);
            }
            for(unsigned tick=0;tick<60&&(!first->finished||auras(match));++tick)
                match.step(stage.geometry(),{},tick%2==0,Contacts::Locked);
            BUFF_REQUIRE(first->finished&&auras(match)==0);
            actor.applyBuff(kind,.2,120);const auto next=actor.buffState(kind);
            BUFF_REQUIRE(next!=first&&!next->finished&&first->finished);
            match.step(stage.geometry(),{},false,Contacts::Locked);
            BUFF_REQUIRE(auras(match)==1);
            FighterStrike hit{};hit.key=7;hit.hitType=3;hit.damage=1;hit.hurtFrames=10;
            match.actor(1).receiveStrike({hit,1,350,1,-1,{},true,{430,stage.geometry().playerBottom-80,460,stage.geometry().playerBottom}});
            match.step(stage.geometry(),{},false,Contacts::Locked);
            const unsigned remaining=next->frames;
            for(unsigned tick=0;tick<60&&match.hitFreezeFrames();++tick) {
                match.step(stage.geometry(),{},tick%2==0,Contacts::Locked);
                if(match.hitFreezeFrames()) {BUFF_REQUIRE(next->frames==remaining&&!next->finished&&auras(match)==1);++holds;}
            }
            BUFF_REQUIRE(!match.hitFreezeFrames());word(hz);word(unsigned(kind));word(next->frames);++integrated;
        }
        return true;
    }
    bool run(const std::string& folder) {return reference(folder)&&matches(folder);}
#undef BUFF_REQUIRE
};
} // namespace bvn::buff_qa
