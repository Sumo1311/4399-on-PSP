// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "combat_effects.hpp"
#include "combat_effect_art.hpp"
#include "fighter_motion.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>
#include <bit>

namespace bvn::combat_effects_qa {
struct Checks {
    unsigned error=0,definitions=0,queries=0,clips=0,frames=0,pixels=0,contacts=0,geometry=0;
    std::uint64_t hash=14695981039346656037ULL;
#define COMBAT_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
    void byte(unsigned v){hash=(hash^(v&255))*1099511628211ULL;}
    void word(unsigned v){for(unsigned i=0;i<4;++i)byte(v>>(8*i));}
    void string(std::string_view value){for(const char c:value)byte(unsigned(c));byte(0);}
    static std::string_view optional(const char* value){return std::strcmp(value,"-")?value:"";}
    bool values(FILE* file,unsigned count) {
        COMBAT_REQUIRE(count==combatEffects().size());
        for(unsigned i=0;i<count;++i) {
            char id[64],cls[64],sound[64],special[64];unsigned blend,freeze,shine,color,time,slowTime,flags;
            double alpha,x,y,power,rate;int r,g,b;
            COMBAT_REQUIRE(std::fscanf(file,"%63s %63s %63s %63s %u %u %u %u %lf %lf %lf %lf %u %lf %u %u %d %d %d",
                id,cls,sound,special,&blend,&freeze,&shine,&color,&alpha,&x,&y,&power,&time,&rate,&slowTime,&flags,&r,&g,&b)==19);
            const auto* e=combatEffect(id);COMBAT_REQUIRE(e&&e==&combatEffects()[i]);
            COMBAT_REQUIRE(e->className==cls&&e->sound==optional(sound)&&e->specialEffect==optional(special));
            COMBAT_REQUIRE(unsigned(e->blend)==blend&&e->freezeMs==freeze&&e->hasShine==bool(shine));
            COMBAT_REQUIRE(e->shineColor==color&&e->shineAlpha==alpha);
            COMBAT_REQUIRE(e->shakeX==x&&e->shakeY==y&&e->shakePower==power&&e->shakeTimeMs==time);
            COMBAT_REQUIRE(e->slowRate==rate&&e->slowTimeMs==slowTime);
            COMBAT_REQUIRE(e->randomRotation==bool(flags&1)&&e->followDirection==bool(flags&2)&&e->special==bool(flags&4));
            COMBAT_REQUIRE(e->buff==bool(flags&8)&&e->steel==bool(flags&16)&&e->hasTargetTint==bool(flags&32));
            COMBAT_REQUIRE((e->targetTint==std::array<int,3>{r,g,b}));
            string(e->id);word(freeze);word(color);word(unsigned(std::lround(alpha*10000)));word(flags);++definitions;
        }
        return true;
    }
    bool selections(FILE* file,unsigned count) {
        for(unsigned i=0;i<count;++i) {
            char kind[16],id[64];int type,defense;unsigned enemy;
            COMBAT_REQUIRE(std::fscanf(file,"%15s %d %u %d %63s",kind,&type,&enemy,&defense,id)==5);
            const auto* e=std::strcmp(kind,"hit")==0?combatHitEffect(type,enemy):combatDefenseEffect(type,defense,enemy);
            COMBAT_REQUIRE((e?e->id:std::string_view{})==optional(id));string(e?e->id:std::string_view{});++queries;
        }
        COMBAT_REQUIRE(!combatEffect("unknown")&&!combatSteelEffect(0));
        for(int type=-1;type<17;++type) {
            if(!type)continue;
            const auto* e=combatSteelEffect(type);COMBAT_REQUIRE(e&&e->steel&&e->freezeMs==400&&e->sound=="snd_hit11");
        }
        return true;
    }
    bool art(FILE* file,unsigned count,unsigned frameCount,const std::string& path) {
        FighterAsset asset;COMBAT_REQUIRE(asset.open(path.c_str(),FighterAsset::TextureResidency::LowMemory));
        COMBAT_REQUIRE(count==std::size(CombatArtData)&&asset.clipCount()==count+9);
        std::vector<std::byte> buffer;
        for(unsigned i=0;i<count;++i) {
            char cls[64];unsigned clip,n;
            COMBAT_REQUIRE(std::fscanf(file,"%63s %u %u",cls,&clip,&n)==3);
            const auto& a=CombatArtData[i];COMBAT_REQUIRE(a.className==cls&&a.clip==clip&&a.frames==n);
            COMBAT_REQUIRE(asset.has(MoveClip(clip))&&asset.clip(MoveClip(clip)).count==n);
            for(unsigned j=0;j<n;++j) {
                unsigned phase,parts,bytes;unsigned long long expected;
                COMBAT_REQUIRE(std::fscanf(file,"%u %u %u %llx",&phase,&parts,&bytes,&expected)==4&&phase==j);
                const auto tiles=asset.poseTiles(asset.frame(MoveClip(clip),phase).pose);
                COMBAT_REQUIRE(tiles.size()==parts);std::uint64_t actual=14695981039346656037ULL;unsigned size=0;
                for(const auto& pose:tiles) {
                    buffer.resize(FighterAsset::textureSize(pose));COMBAT_REQUIRE(asset.decodePixels(pose,buffer));
                    for(const auto value:buffer)actual=(actual^std::to_integer<unsigned>(value))*1099511628211ULL;
                    size+=buffer.size();
                }
                COMBAT_REQUIRE(size==bytes&&actual==expected);pixels+=size/4;++frames;word(unsigned(actual));word(unsigned(actual>>32));
            }
            ++clips;
        }
        return frames==frameCount;
    }
    bool hitRequests(const std::string& bank) {
        FighterAsset asset;COMBAT_REQUIRE(asset.open(bank.c_str(),FighterAsset::TextureResidency::LowMemory));
        for(bool enemy:{false,true})for(unsigned type=0;type<17;++type)for(int dir:{-1,1}) {
            FighterMotion actor(asset);actor.reset({100,200},-dir);actor.setMusouEnemyEffects(enemy);
            FighterStrike strike{};strike.damage=1;strike.hitType=type;strike.key=7;strike.hurtFrames=10;
            const StrikeContact contact{strike,dir,50,1,-1,{},true,{91.5,145,112.5,185}};
            actor.receiveStrike(contact);const auto output=actor.takeEffects();const auto* expected=combatHitEffect(type,enemy);
            COMBAT_REQUIRE(output.contactEffects.size()==unsigned(expected!=nullptr));
            if(expected) {
                const auto& request=output.contactEffects[0];
                COMBAT_REQUIRE(request.effect==expected&&request.kind==ContactEffectKind::Hurt);
                COMBAT_REQUIRE(request.origin.x==102&&request.origin.y==165&&request.facing==1);
            }
            COMBAT_REQUIRE(actor.takeEffects().contactEffects.empty());++contacts;
        }
        const StageGeometry stage{0,4000,2000,1000,{}};
        for(bool enemy:{false,true})for(int defense:{0,1})for(unsigned type=0;type<17;++type)
        for(int dir:{-1,1})for(bool breaks:{false,true}) {
            FighterMotion actor(asset);actor.reset({1700,1000},-dir);
            actor.setDefenseType(defense);actor.setMusouEnemyEffects(enemy);
            for(unsigned tick=0;tick<8;++tick)actor.step(stage,{.vertical=-1},tick%2==0);
            COMBAT_REQUIRE(actor.aiState()==AiState::Guard);actor.takeEffects();actor.setEnergy(breaks?0:100);
            FighterStrike strike{};strike.damage=100;strike.hitType=type;strike.key=7;strike.hurtFrames=10;
            actor.receiveStrike({strike,dir,1700-dir*40.,1,-1,{},true,{1691.5,945,1712.5,985}});
            const auto output=actor.takeEffects();COMBAT_REQUIRE(output.contactEffects.size()==(breaks?2U:1U));
            const auto& guard=output.contactEffects[0];
            COMBAT_REQUIRE(guard.effect==combatDefenseEffect(type,defense)&&guard.kind==ContactEffectKind::Guard);
            COMBAT_REQUIRE(guard.origin.x==1702&&guard.origin.y==965&&guard.facing==dir);
            if(breaks) {
                const auto& broken=output.contactEffects[1];
                COMBAT_REQUIRE(broken.effect==combatEffect("break_def")&&broken.kind==ContactEffectKind::GuardBreak);
                COMBAT_REQUIRE(broken.facing==-dir&&broken.origin.x==1702&&broken.origin.y==965);
            }
            ++contacts;
        }
        for(unsigned type=0;type<17;++type)for(bool breaks:{false,true})for(int dir:{-1,1}) {
            FighterMotion actor(asset);actor.reset({1700,1000},-dir);actor.setQi(300);
            actor.present(FighterCommand::SkillDown);actor.step(stage,{},true);actor.takeEffects();
            FighterStrike strike{};strike.damage=1;strike.hitType=type;strike.key=7;strike.hurtFrames=10;
            strike.flags=breaks?2:0;
            actor.receiveStrike({strike,dir,1700-dir*40.,1,-1,{},true,{1691.5,945,1712.5,985}});
            const auto output=actor.takeEffects();const bool caught=type==11&&breaks;
            const auto* expected=caught?combatHitEffect(type):combatSteelEffect(type);
            COMBAT_REQUIRE(output.contactEffects.size()==unsigned(expected!=nullptr));
            COMBAT_REQUIRE(std::abs(actor.health()-(1000-(caught?1:.65)))<.000001);
            if(expected) {
                const auto& request=output.contactEffects[0];
                COMBAT_REQUIRE(request.effect==expected&&request.kind==(caught?ContactEffectKind::Hurt:ContactEffectKind::Steel));
                COMBAT_REQUIRE(request.facing==1&&request.origin.x==1702&&request.origin.y==965);
            }
            ++contacts;
        }
        return true;
    }
    bool locations(const std::string& bank,FILE* trace) {
        FighterAsset asset;COMBAT_REQUIRE(asset.open(bank.c_str(),FighterAsset::TextureResidency::LowMemory));
        const StageGeometry stage{0,4000,2000,1000,{}};
        for(int dir:{-1,1})for(unsigned box=0;box<4;++box) {
            FighterMotion actor(asset);actor.reset({1700,1000},dir);unsigned observed=0;
            const WorldBox target{1660.+dir*20.*box,870.+10*box,1740.+dir*20.*box,1010.-5*box};
            for(unsigned tick=0;tick<200;++tick) {
                actor.step(stage,{.attackPressed=tick%60==0,.attackHeld=true},tick%2==0);
                const auto contact=actor.strikeAgainst({1700,1000},target);if(!contact)continue;
                const FighterFrame* active=nullptr;
                for(unsigned c=0;c<asset.clipCount()&&!active;++c)
                    for(unsigned p=0;p<asset.clip(MoveClip(c)).count;++p) {
                        const auto& f=asset.frame(MoveClip(c),p);
                        if(&f.pose==&actor.pose()){active=&f;break;}
                    }
                COMBAT_REQUIRE(active);WorldBox attack{};
                for(const auto& hit:asset.hits(*active)) {
                    if(!hit.strike())continue;
                    const auto box=worldBox(hit.box,actor.motion().position,dir);
                    if(box.overlaps(target)){attack=box;break;}
                }
                COMBAT_REQUIRE(!attack.empty()&&!contact->overlap.empty());
                const double fields[]{attack.left,attack.top,attack.right-attack.left,attack.bottom-attack.top,
                    target.left,target.top,target.right-target.left,target.bottom-target.top,
                    contact->overlap.left,contact->overlap.top,contact->overlap.right-contact->overlap.left,
                    contact->overlap.bottom-contact->overlap.top};
                if(trace)for(unsigned i=0;i<12;++i)std::fprintf(trace,i==11?"%.17g\n":"%.17g ",fields[i]);
                for(double value:fields) {
                    const auto bits=std::bit_cast<std::uint64_t>(value);word(unsigned(bits));word(unsigned(bits>>32));
                }
                ++observed;++geometry;
            }
            COMBAT_REQUIRE(observed>0);
        }
        return true;
    }
    bool run(const std::string& folder,FILE* trace=nullptr) {
        auto* f=std::fopen((folder+"EFFECT.IN").c_str(),"r");COMBAT_REQUIRE(f);
        char magic[16];unsigned n,q,c,a;
        const bool header=std::fscanf(f,"%15s %u %u %u %u",magic,&n,&q,&c,&a)==5&&std::strcmp(magic,"BVNEFFECT1")==0;
        const bool pass=header&&values(f,n)&&selections(f,q)&&art(f,c,a,folder+"COMBAT.BVF");
        std::fclose(f);if(!pass)return false;
        return hitRequests(folder+"ICHIGO.BVF")&&locations(folder+"ICHIGO.BVF",trace);
    }
#undef COMBAT_REQUIRE
};
} // namespace bvn::combat_effects_qa
