// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "musou_battle.hpp"
#include <cstdio>
#include <string>
#include <algorithm>

namespace bvn::musou_combat_qa {
struct Checks {
    unsigned error=0, styles=0, guards=0, freezes=0, battles=0, held=0, effects=0, bosses=0, projectiles=0;
    unsigned fades=0, switches=0, deaths=0;
    std::uint64_t hash=14695981039346656037ULL;
    void word(unsigned v) { for(unsigned i=0;i<4;++i) hash=(hash^((v>>(8*i))&255))*1099511628211ULL; }
#define MUS_REQUIRE(x) do { if(!(x)) { error=__LINE__; return false; } } while(false)
    bool profiles(const std::string& folder) {
        FILE* file=std::fopen((folder+"STYLES.IN").c_str(),"r"); MUS_REQUIRE(file);
        unsigned count; MUS_REQUIRE(std::fscanf(file,"%u",&count)==1 && count==89);
        const StageGeometry stage{0,4000,2000,1000,{}};
        for(unsigned i=0;i<count;++i) {
            char name[32]; unsigned style;
            MUS_REQUIRE(std::fscanf(file,"%31s %u",name,&style)==2 && style<2);
            FighterAsset asset; MUS_REQUIRE(asset.open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory));
            MUS_REQUIRE(asset.defenseType()==style); FighterMotion actor(asset); actor.reset({1700,1000},-1);
            MUS_REQUIRE(actor.defenseType()==style); word(style); ++styles;
            if(!actor.hasAction(FighterCommand::Guard)) continue;
            for(unsigned tick=0;tick<8;++tick) actor.step(stage,{.vertical=-1},tick%2==0);
            actor.takeEffects(); FighterStrike strike{}; strike.key=7; strike.hitType=1; strike.damage=1; strike.hurtFrames=10;
            actor.receiveStrike({strike,1,1660,1,-1,{},true,{1691.5,945,1712.5,985}});
            const auto result=actor.takeEffects();
            MUS_REQUIRE(result.contactEffects.size()==1 && result.contactEffects[0].kind==ContactEffectKind::Guard);
            MUS_REQUIRE(result.contactEffects[0].effect->id==(style?"defense_2":"defense_1")); ++guards;
        }
        MUS_REQUIRE(std::fgetc(file)== '\n' && std::fgetc(file)==EOF); std::fclose(file); return true;
    }
    bool freezeReference(const std::string& folder) {
        FILE* file=std::fopen((folder+"MUSFREEZE.IN").c_str(),"r"); MUS_REQUIRE(file);
        unsigned count; MUS_REQUIRE(std::fscanf(file,"%u",&count)==1 && count==1440);
        CombatFreeze freeze; unsigned expired=0;
        for(unsigned row=0;row<count;++row) {
            unsigned scenario,hz,phase,tick,ms,mode,before,pauseBefore,after,paused,enabled,completed;
            MUS_REQUIRE(std::fscanf(file,"%u %u %u %u %u %u %u %u %u %u %u %u",&scenario,&hz,&phase,&tick,&ms,&mode,
                &before,&pauseBefore,&after,&paused,&enabled,&completed)==12);
            if(!tick) { freeze=CombatFreeze{}; expired=0; if(phase) freeze.step(hz); }
            if(mode==1) freeze.disable(); else if(mode==2) freeze.resume();
            if(ms) freeze.request(ms,hz);
            MUS_REQUIRE(freeze.remaining()==before && freeze.paused()==bool(pauseBefore && enabled));
            if(freeze.step(hz)) ++expired;
            MUS_REQUIRE(freeze.remaining()==after && freeze.paused()==bool(paused && enabled));
            MUS_REQUIRE(freeze.enabled()==bool(enabled) && expired==completed);
            word(after); word(paused); word(expired); ++freezes;
        }
        std::fclose(file); return true;
    }
    static FighterMotion& player(MusouBattle& battle) { return const_cast<FighterMotion&>(battle.member(battle.leader())); }
    static FighterMotion& opponent(const MusouOpponent& enemy) { return const_cast<FighterMotion&>(enemy.motion); }
    static void strikes(FighterAsset& asset,unsigned type) {
        // Declared controller fixture: retain authored timelines/geometry and
        // object programs while selecting a hit type and nonlethal damage.
        for(const auto& value:asset.strikes()) {
            auto& strike=const_cast<FighterStrike&>(value);
            strike.hitType=type; strike.damage=1; strike.hurtFrames=40;
            strike.hitX=strike.hitY=0; strike.flags=0;
        }
    }
    bool battle(const std::string& folder, unsigned type, unsigned hz, bool boss, bool projectile, bool lethal=false) {
        FighterAsset asset,enemyAsset; StageAsset stage; MusouCampaign campaign;
        MUS_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        MUS_REQUIRE(enemyAsset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        strikes(asset,type);
        MUS_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        MUS_REQUIRE(campaign.open((folder+(lethal?"MUSLETH.BVM":boss?"MUSBOSS.BVM":"MUSMIN.BVM")).c_str()));
        MusouProgress progress(campaign); std::array<MusouMember,3> members{};
        const MusouFighterBank bank{"qa-player",std::span(&asset,1),0,1000,0};
        const std::array<MusouFighterBank,3> team{bank,bank,bank};
        const MusouFighterBank foe{"qa-enemy",std::span(&enemyAsset,1),0,1000,0};
        MusouBattle run; MUS_REQUIRE(run.start(campaign,0,stage,team,std::span(&foe,1),progress,members,0x12345678));
        run.setGameHz(hz,30);
        for(unsigned tick=0;tick<400;++tick) {
            run.step({},false);
            if(run.enemies().size()==(boss?1U:6U) && run.bossCue().kind==MusouBossCue::Kind::None) break;
        }
        MUS_REQUIRE(run.enemies().size()==(boss?1U:6U) && run.bossCue().kind==MusouBossCue::Kind::None);
        if(!boss && !projectile && type==2 && hz==30) {
            const auto position=player(run).motion().position; const auto leader=run.leader();
            run.step({},true);
            MUS_REQUIRE(run.leader()!=leader && player(run).motion().position.x==position.x);
            MUS_REQUIRE(std::ranges::any_of(run.scene().combat,[](const auto& v){return v.effect->id=="team_change" && v.art->frames==12;}));
            const auto sounds=run.takeCombatSounds(); MUS_REQUIRE(std::ranges::find(sounds,std::string_view("snd_change1"))!=sounds.end());
            ++switches;
        }
        const double ground=stage.geometry().playerBottom;
        player(run).reset({1000,ground},1); player(run).setQi(player(run).maxQi());
        for(const auto& enemy:run.enemies()) {
            auto& actor=opponent(enemy); actor.reset({projectile?1110.:1040.,ground},-1);
            actor.setHealth(lethal?1:50000);
            auto& fixture=const_cast<MusouOpponent&>(enemy);
            fixture.brain=MusouEnemyBrain{}; fixture.bossAI.reset();
        }
        const auto* wanted=combatHitEffect(type,!boss); MUS_REQUIRE(wanted);
        unsigned attempts=0;
        for(;attempts<240;++attempts) {
            // Hold the declared collision target lane while the real authored attack advances.
            for(const auto& enemy:run.enemies()) opponent(enemy).place({projectile?1110.:1040.,ground});
            run.step({.attackPressed=!projectile && attempts==0,.specialPressed=projectile && attempts==0},false);
            const auto scene=run.scene();
            if(std::ranges::any_of(scene.combat,[&](const auto& v){return v.effect==wanted;})) break;
        }
        if(attempts==240) std::fprintf(stderr,"no contact: type=%u hz=%u boss=%u projectile=%u leaderframe=%u enemies=%zu objects=%zu\n",
            type,hz,boss,projectile,player(run).timelineFrame(),run.enemies().size(),run.objects().size());
        MUS_REQUIRE(attempts<240);
        const unsigned duration=wanted->freezeMs*hz/1000;
        if(run.hitFreezeFrames()!=duration) {
            std::fprintf(stderr,"freeze type=%u hz=%u boss=%u projectile=%u expected=%u actual=%u\n",type,hz,boss,projectile,duration,run.hitFreezeFrames());
            for(const auto& v:run.scene().combat) std::fprintf(stderr," effect %.*s\n",int(v.effect->id.size()),v.effect->id.data());
        }
        MUS_REQUIRE(run.hitFreezeFrames()==duration);
        const auto sounds=run.takeCombatSounds(); MUS_REQUIRE(!sounds.empty());
        MUS_REQUIRE(std::ranges::find(sounds,wanted->sound)!=sounds.end());
        const auto scene=run.scene();
        for(const auto& view:scene.combat) if(view.effect==wanted) {
            MUS_REQUIRE(view.target); ++effects;
            if(type>=7 && type<=9) {
                const auto tint=combatEffect(wanted->specialEffect)->targetTint;
                MUS_REQUIRE((view.target->tint==ShadowTint{tint[0],tint[1],tint[2]}));
            }
        }
        if(lethal) {
            MUS_REQUIRE(run.bossCue().kind==MusouBossCue::Kind::Knockout && !run.freezeEnabled() && !run.hitFreeze());
            MUS_REQUIRE(std::ranges::any_of(scene.combat,[](const auto& v){return v.effect->id=="hit_end";}));
            MUS_REQUIRE(std::ranges::any_of(scene.shines,[](const auto& v){return v.color==0xffffff && v.alpha==127;}));
            const auto clock=run.wave().timeLeft();
            for(unsigned tick=0;tick<MusouBossCue::KnockoutTicks;++tick) {
                run.step({.horizontal=1},false);
                MUS_REQUIRE(run.wave().timeLeft()==clock && run.hitFreezeFrames()==duration);
            }
            MUS_REQUIRE(run.freezeEnabled() && !run.hitFreeze() && run.bossCue().kind==MusouBossCue::Kind::None);
            for(unsigned tick=0;tick<4;++tick) run.step({.horizontal=1},false);
            MUS_REQUIRE(run.wave().timeLeft()<clock && run.hitFreezeFrames()>0 && !run.hitFreeze()); ++bosses;
        } else {
            const bool fadeCase=!boss && !projectile && type==7;
            if(fadeCase) {
                auto& fading=const_cast<MusouOpponent&>(run.enemies().back());
                fading.fade=3; fading.motion.setHealth(0);
            }
            bool animated=false; unsigned last=scene.combat.front().age;
            for(unsigned tick=0;tick<80 && run.hitFreeze();++tick) {
                const auto position=player(run).motion().position;
                const auto frame=player(run).timelineFrame(),clock=run.wave().timeLeft(),spawned=run.spawned(),leader=run.leader();
                run.step({.horizontal=1},true);
                if(run.hitFreeze()) {
                    MUS_REQUIRE(player(run).motion().position.x==position.x && player(run).motion().position.y==position.y);
                    MUS_REQUIRE(player(run).timelineFrame()==frame && run.wave().timeLeft()==clock && run.spawned()==spawned && run.leader()==leader);
                    if(!run.scene().combat.empty() && run.scene().combat.front().age!=last) animated=true;
                    ++held;
                }
            }
            MUS_REQUIRE(!run.hitFreeze() && (duration<3 || animated));
            if(fadeCase) { MUS_REQUIRE(run.enemies().size()==5); ++fades; }
        }
        word(type); word(hz); word(duration); word(run.emittedEffects()); ++battles; projectiles+=projectile; return true;
    }
    bool selfDeath(const std::string& folder,unsigned hz) {
        FighterAsset asset,enemyAsset; StageAsset stage; MusouCampaign campaign;
        MUS_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        MUS_REQUIRE(enemyAsset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        strikes(enemyAsset,7);
        MUS_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()) && campaign.open((folder+"MUSSELF.BVM").c_str()));
        MusouProgress progress(campaign); std::array<MusouMember,3> members{};
        const MusouFighterBank bank{"qa-player",std::span(&asset,1)};
        const std::array<MusouFighterBank,3> team{bank,bank,bank};
        const MusouFighterBank foe{"qa-enemy",std::span(&enemyAsset,1)};
        MusouBattle run; MUS_REQUIRE(run.start(campaign,0,stage,team,std::span(&foe,1),progress,members,12345));
        run.setGameHz(hz,30); for(unsigned tick=0;tick<20 && run.enemies().empty();++tick) run.step({},false);
        MUS_REQUIRE(run.enemies().size()==1);
        const double ground=stage.geometry().playerBottom;
        player(run).reset({1040,ground},-1); player(run).setHealth(1);
        auto& enemy=opponent(run.enemies().front()); enemy.reset({1000,ground},1);
        enemy.step(stage.geometry(),{.attackPressed=true},true,std::nullopt,&player(run));
        for(unsigned tick=0;tick<100 && player(run).health()>0;++tick) {
            player(run).place({1040,ground}); enemy.place({1000,ground}); run.step({},false);
        }
        MUS_REQUIRE(player(run).health()<=0 && run.hitFreeze() && run.freezeEnabled());
        const auto scene=run.scene();
        MUS_REQUIRE(std::ranges::any_of(scene.combat,[](const auto& v){return v.effect->id=="hit_7";}));
        MUS_REQUIRE(std::ranges::any_of(scene.combat,[](const auto& v){return v.effect->id=="hit_end";}));
        MUS_REQUIRE(std::ranges::any_of(scene.shines,[](const auto& v){return v.color==0xff0000 && v.alpha==204;}));
        const auto clock=run.wave().timeLeft();
        for(unsigned tick=0;tick<1200 && run.wave().result()==MusouResult::Playing;++tick) run.step({},false);
        MUS_REQUIRE(run.wave().result()==MusouResult::Defeated && run.wave().timeLeft()==clock);
        ++deaths; word(hz); word(clock); ++battles; return true;
    }
    bool run(const std::string& folder) {
        if(!profiles(folder) || !freezeReference(folder)) return false;
        for(unsigned hz:{30U,60U}) for(unsigned type:{2U,3U,7U,8U,9U}) for(bool boss:{false,true})
            if(!battle(folder,type,hz,boss,false)) return false;
        for(unsigned hz:{30U,60U}) {
            if(!battle(folder,7,hz,false,true)) return false;
            if(!battle(folder,3,hz,true,false,true)) return false;
            if(!selfDeath(folder,hz)) return false;
        }
        return true;
    }
#undef MUS_REQUIRE
};
} // namespace bvn::musou_combat_qa
