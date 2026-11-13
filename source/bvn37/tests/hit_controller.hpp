// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_hit_control.hpp"
#include "fighter_motion.hpp"
#include "versus_round.hpp"
#include "musou_battle.hpp"
#include <cstdio>
#include <cmath>
#include <array>
#include <string>
namespace bvn::hit_controller_qa {
struct Checks {
    unsigned error=0,rows=0,actors=0,sessions=0,failedRow=0,failedColumn=0;double actual=0,expected=0;
    std::uint64_t hash=14695981039346656037ull;
    void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(8*i))&255))*1099511628211ull;}
    void number(double x){word(unsigned(std::llround(x*1000000)));}
#define HIT_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
    struct View {
        double hp=1000,ep=100,maximum=100,defenseRate=1,defense=0,ownX=0,qiValue=0;
        bool living=true,overloaded=false,guarding=false,steel=false,superSteel=false,allow=true,air=false,side=false,training=false,allowHit=true,hurting=false,flying=false,guardedHit=false,applyG=true,minion=false,binding=false;
        unsigned gameHz=30,energyGap=0,hitGap=0,hold=0,defHold=0,dies=0,effects=0,effectHash=2166136261u,eventHash=2166136261u,animations=0,actions=0,pushes=0,mainPlay=0,mainFrames=0,cached=0;
        std::uint32_t drawState=1;
        int actionState=0,facing=1;double vx=0,vy=0,dx=0,dy=0,sx=0,sy=0,sdx=0,sdy=0,reverseX=0;
        const SourceHitInput* hit=nullptr;
        void eventWord(unsigned value){for(unsigned i=0;i<4;++i)eventHash=(eventHash^((value>>(i*8))&255))*16777619u;}
        void event(unsigned op){eventWord(op);eventWord(living);eventWord(actionState);eventWord(unsigned(std::llround(hp*1000)));}
        bool touchingSide()const{return side;}double x()const{return 400;}double ownerX()const{return ownX;}
        void reversePush(double x){++pushes;reverseX=x;event(10);}
        bool alive()const{return living;}void alive(bool value){living=value;}bool checkDie()const{return !training&&hp<=0;}
        void dieEvent(){++dies;event(2);}void hurtHit(){}void guardHit(){}
        void loseHp(double damage){if(allow){const auto loss=damage*std::clamp(2-defenseRate,.1,1.)-defense;if(loss>=0)hp=std::max(0.,hp-loss);}}
        double energy()const{return ep;}double maxEnergy()const{return maximum;}void energy(double value){ep-=value;energyGap=24;if(ep<0){ep=0;overloaded=true;}}
        bool hittable()const{return allowHit;}void hittable(bool value){allowHit=value;}void gap(unsigned value){hitGap=value;}
        int state()const{return actionState;}void qi(double value){qiValue=std::min(300.,qiValue+value);}void cacheHit(){++cached;}
        void gravity(bool value){applyG=value;}void overload(bool value){overloaded=value;}void guard(bool value){guarding=value;}void guardHitting(bool value){guardedHit=value;}void guardHold(unsigned value){defHold=value;}unsigned hz()const{return gameHz;}
        void velocity(double x,double y){vx=x;vy=y;dx=dy=0;}void damping(double x,double y){dx=x;dy=y;}void secondary(double x,double y,double dragX,double dragY){sx=x;sy=y;sdx=dragX;sdy=dragY;}
        void clearFlags(){hurting=flying=guardedHit=false;}
        void effect(SourceHitResult type){const unsigned id=type==SourceHitResult::Hurt?1:type==SourceHitResult::Guard?4:type==SourceHitResult::GuardBreak?5:6;++effects;effectHash=(effectHash^id)*16777619u;event(id);}
        void action(){++actions;binding=false;guarding=false;allowHit=applyG=true;velocity(0,0);mainPlay=1;mainFrames=0;event(9);}
        void hurtAnimate(){
            ++animations;applyG=true;guarding=false;
            const auto v=sourceHurtVelocity(hit->x,hit->y,hit->owner,hit->direction,air,living,minion,[&]{return sourceRandomUnit(drawState);});
            clearFlags();steel=superSteel=false;
            if(hit->launch){flying=true;actionState=22;if(v.x)facing=v.x>0?-1:1;mainPlay=mainFrames=0;velocity(v.x,v.y);damping(0,.5);}
            else{hurting=true;hold=std::max(4,int(std::round(hit->hurtMillis*.001*30))+3);mainPlay=hit->hitType!=11;mainFrames=mainPlay?7:0;actionState=21;velocity(v.x,v.y);damping(.1,.5);}
            event(3);
        }
        void breakAnimate(unsigned value){clearFlags();hurting=true;hold=value;mainPlay=1;mainFrames=7;actionState=21;}
    };
    bool reference(const std::string& folder){
        FILE* file=std::fopen((folder+"HITCTRL.IN").c_str(),"r");HIT_REQUIRE(file);unsigned count=0;HIT_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==16384);View view;
        for(unsigned row=0;row<count;++row){
            std::array<double,73> v{};for(auto& x:v)HIT_REQUIRE(std::fscanf(file,"%lf",&x)==1);
            if(v[2]==0){view={};view.gameHz=unsigned(v[0]);view.hp=v[3];view.living=v[4];view.ep=v[5];view.maximum=v[6];view.overloaded=v[7];view.guarding=v[8];view.steel=v[9];view.superSteel=v[10];view.allow=v[11];view.defenseRate=v[12];view.defense=v[13];view.air=v[14];view.side=v[15];view.minion=v[18];view.facing=int(v[20]);view.ownX=v[21];view.training=v[22];view.drawState=unsigned(v[24]);view.binding=v[23];if(view.binding)view.actionState=16;}
            for(unsigned i=0;i<unsigned(v[25]);++i)if(view.hitGap&&!--view.hitGap)view.allowHit=true;
            const unsigned ownerKind=unsigned(v[16]);const bool hasOwner=ownerKind!=3;
            const bool weak=hasOwner&&v[17]&&!v[29]&&!SourceHitControl::heavy(int(v[28]));
            const int direction=unsigned(v[1])%2?-1:1;
            const bool behind=hasOwner&&direction==view.facing&&(400-view.ownX)*view.facing>5;
            const SourceHitInput hit{v[26],v[34],v[35],unsigned(v[36]),int(v[28]),direction,bool(v[29]),bool(v[30]),bool(v[31]),weak,hasOwner,ownerKind==0||ownerKind==2,bool(v[19]),view.steel,view.superSteel,view.overloaded,view.guarding,behind,bool(v[32]),bool(v[33]),view.binding};
            view.hit=&hit;SourceHitControl::receiveMain(hit,v[27],view);
            const std::array<double,36> got{view.hp,double(view.living),view.ep,double(view.overloaded),double(view.energyGap),double(view.allowHit),double(view.hitGap),double(view.actionState),double(view.hold),double(view.defHold),double(view.guarding),double(view.hurting),double(view.flying),double(view.guardedHit),double(view.applyG),view.vx,view.vy,view.dx,view.dy,view.sx,view.sy,view.sdx,view.sdy,double(view.pushes),view.reverseX,double(view.dies),double(view.effects),double(view.effectHash),double(view.eventHash),double(view.animations),double(view.actions),double(view.mainPlay),double(view.mainFrames),view.qiValue,double(view.cached),double(view.drawState)};
            for(unsigned k=0;k<got.size();++k){if(std::abs(got[k]-v[37+k])>1e-9){failedRow=row;failedColumn=k;actual=got[k];expected=v[37+k];error=__LINE__;std::fclose(file);return false;}number(got[k]);}++rows;
        }
        std::fclose(file);return true;
    }
    bool actorCases(const StageAsset& stage,const FighterAsset& art){
        const auto floor=stage.geometry().playerBottom;
        for(unsigned hz:{30u,60u}){
            std::uint32_t seed=0x12345678;FighterMotion attacker(art),target(art);attacker.reset({400,floor},1);target.reset({450,floor},-1);attacker.sourceHz(hz);target.sourceHz(hz);
            attacker.setMusouEnemyEffects(true);
            FighterStrike hit{};hit.key=1200;hit.damage=100;hit.hurtFrames=20;hit.hitType=1;hit.hitX=6;
            HIT_REQUIRE(target.receiveStrike({hit,1,400},&attacker));
            HIT_REQUIRE(target.alive()&&target.health()==900&&target.aiState()==AiState::Normal&&!target.sourceHurt().active()&&target.hitGap()==4);
            for(unsigned t=0;t<8;++t)target.step(stage.geometry(),{},hz==30||t%2==0);
            hit.key++;hit.flags=64;
            HIT_REQUIRE(target.receiveStrike({hit,1,400},&attacker));HIT_REQUIRE(!target.motion().applyGravity&&target.aiState()==AiState::Normal);
            for(unsigned t=0;t<8;++t)target.step(stage.geometry(),{},hz==30||t%2==0);
            hit.key++;hit.flags=0;hit.hitType=7;
            HIT_REQUIRE(target.receiveStrike({hit,1,400},&attacker));HIT_REQUIRE(target.aiState()==AiState::Hurt&&target.sourceHurt().hurting());
            ++actors;word(hz);word(unsigned(target.health()));
            // Nonlaunch gravity is restored by the real hurt-resume/idle path.
            target.reset({450,floor},-1);hit.hitType=1;hit.flags=64;attacker.setMusouEnemyEffects(false);
            HIT_REQUIRE(target.receiveStrike({hit,1,400},&attacker)&&!target.motion().applyGravity);
            bool idle=false;for(unsigned t=0;t<200;++t){target.step(stage.geometry(),{},hz==30||t%2==0);if(target.standing()){idle=true;break;}}
            HIT_REQUIRE(idle&&target.motion().applyGravity);++actors;
            for(bool air:{false,true})for(double y:{0.,8.}){
                target.reset({450,floor-(air?50:0)},-1);target.setMusouEnemyEffects(true);target.setHealth(1);
                auto& motion=const_cast<Motion&>(target.motion());motion.airborne=air;
                seed=0x12345678;std::uint32_t expectedSeed=seed;target.hurtRandomState(seed);hit.hitY=y;hit.flags=1;
                const auto expectedVelocity=sourceHurtVelocity(6,y,true,1,air,false,true,[&]{return sourceRandomUnit(expectedSeed);});
                HIT_REQUIRE(target.receiveStrike({hit,1,400},&attacker));
                HIT_REQUIRE(!target.alive()&&target.dieEvents()==1&&seed==expectedSeed&&target.aiState()==AiState::Flying);
                HIT_REQUIRE(std::abs(target.motion().velocity.x-expectedVelocity.x)<1e-12&&std::abs(target.motion().velocity.y-expectedVelocity.y)<1e-12);++actors;
            }
            for(bool super:{false,true}){
                target.reset({450,floor},-1);target.setMusouEnemyEffects(false);target.setSteelBody(true,super);target.setHealth(1);hit.flags=0;hit.hitY=0;hit.hitType=1;
                HIT_REQUIRE(target.receiveStrike({hit,1,400},&attacker));HIT_REQUIRE(!target.alive()&&target.dieEvents()==1&&target.energy()==100&&target.aiState()==AiState::Hurt);++actors;
            }
            for(unsigned mode=0;mode<3;++mode){
                target.reset({450,floor},-1);target.setHealth(1);target.setMusouEnemyEffects(false);hit.flags=0;hit.hitType=1;hit.damage=100;
                if(mode==1)target.setSteelBody(true);if(mode==2){for(unsigned d=0;d<12;++d)target.step(stage.geometry(),{.vertical=-1},hz==30||d%2==0);HIT_REQUIRE(target.guarding());}
                struct Order {FighterMotion* actor;std::array<unsigned,6> event{};unsigned size=0;}order{&target};
                target.sourceHooks({&order,0,
                    [](void* p,unsigned){auto& o=*static_cast<Order*>(p);o.event[o.size++]=10+o.actor->alive();},
                    [](void* p,FighterEffects&& effect){auto& o=*static_cast<Order*>(p);if(!effect.contactEffects.empty())o.event[o.size++]=20+o.actor->alive();}});
                HIT_REQUIRE(target.receiveStrike({hit,1,400,1,-1,{},true,target.body()},&attacker));if(order.size!=2)std::printf("hook hz=%u mode=%u count=%u events=%u,%u,%u,%u hp=%.2f alive=%u\n",hz,mode,order.size,order.event[0],order.event[1],order.event[2],order.event[3],target.health(),target.alive());HIT_REQUIRE(order.size==2);
                if(mode==0)HIT_REQUIRE(order.event[0]==21&&order.event[1]==10);
                else HIT_REQUIRE(order.event[0]==11&&order.event[1]==20);
                HIT_REQUIRE(!target.alive()&&target.dieEvents()==1);target.sourceHooks({});++actors;
            }
            // At a wall, main fighters and attached attackers reverse-push;
            // Bullet implements IGameSprite rather than BaseGameSprite.
            for(auto kind:{StrikeOwnerKind::Fighter,StrikeOwnerKind::Bullet,StrikeOwnerKind::Attacker}){
                target.reset({stage.geometry().left,floor},-1);attacker.reset({stage.geometry().left+50,floor},1);
                target.step(stage.geometry(),{},false);HIT_REQUIRE(target.motion().touchingSide);
                StrikeContact contact{hit,1,attacker.motion().position.x};contact.ownerKind=kind;
                double recoil=0;if(kind==StrikeOwnerKind::Attacker)contact.recoil={&recoil,[](void* p,double x){*static_cast<double*>(p)=x;}};
                target.receiveStrike(contact,&attacker);
                if(kind==StrikeOwnerKind::Fighter)HIT_REQUIRE(std::abs(attacker.motion().secondaryVelocity.x+8.4)<1e-12);
                if(kind==StrikeOwnerKind::Bullet)HIT_REQUIRE(attacker.motion().secondaryVelocity.x==0&&recoil==0);
                if(kind==StrikeOwnerKind::Attacker)HIT_REQUIRE(std::abs(recoil+8.4)<1e-12&&attacker.motion().secondaryVelocity.x==0);++actors;
            }
            // Use normal guard input. A guard break that reaches zero HP does
            // not itself dispatch DIE in the source; the next hit does.
            target.reset({450,floor},-1);target.setHealth(10);target.setEnergy(0);hit.damage=100;hit.hitX=0;
            for(unsigned t=0;t<12;++t)target.step(stage.geometry(),{.vertical=-1},hz==30||t%2==0);
            target.setEnergy(0);HIT_REQUIRE(target.guarding());HIT_REQUIRE(target.receiveStrike({hit,1,400},&attacker));
            HIT_REQUIRE(target.health()==0&&target.alive()&&target.dieEvents()==0&&target.sourceHurt().hold()==42&&target.aiState()==AiState::Hurt);
            for(unsigned t=0;t<8;++t)target.step(stage.geometry(),{},hz==30||t%2==0);
            HIT_REQUIRE(target.receiveStrike({hit,1,400},&attacker));HIT_REQUIRE(!target.alive()&&target.dieEvents()==1);++actors;
            target.reset({450,floor},-1);
            for(unsigned t=0;t<12;++t)target.step(stage.geometry(),{.vertical=-1},hz==30||t%2==0);
            bool released=false;for(unsigned t=0;t<80;++t){target.step(stage.geometry(),{},hz==30||t%2==0);if(target.timelineFrame()==art.clip(art.command(FighterCommand::GuardRecover)).timelineStart){released=true;break;}}
            HIT_REQUIRE(released&&!target.standing());unsigned animateCalls=0;
            for(unsigned t=0;t<8&&!target.standing();++t){const bool animate=hz==30||t%2==0;target.step(stage.geometry(),{},animate);if(animate)++animateCalls;}
            HIT_REQUIRE(target.standing()&&animateCalls==2);++actors;
        }
        return true;
    }
    bool sessionCases(const std::string& folder,const StageAsset& stage,const FighterAsset& art){
        const double floor=stage.geometry().playerBottom;
        unsigned attackDamage=0;const auto attack=art.command(FighterCommand::Attack);
        for(unsigned phase=0;phase<art.clip(attack).count&&!attackDamage;++phase)for(const auto& hit:art.hits(art.frame(attack,phase)))if(hit.strike()){attackDamage=art.strike(hit.strikeIndex).damage;break;}
        HIT_REQUIRE(attackDamage>0);
        for(unsigned hz:{30u,60u}){
            RoundRules rules;rules.sourceQuality=true;rules.gameHz=hz;rules.maxHealth=5000;
            VersusRound game(stage,std::span(&art,1),std::span(&art,1),60,{},BattleMode::Versus,{}, {},rules);
            for(unsigned d=0;d<200&&game.sourceTransitionRendering();++d)game.step({});do{}while(!game.step({},true));
            for(unsigned d=0;d<1800&&game.phase()!=RoundPhase::Playing;++d)game.step({});HIT_REQUIRE(game.phase()==RoundPhase::Playing);
            auto& a=const_cast<FighterMotion&>(game.match().actor(0));auto& b=const_cast<FighterMotion&>(game.match().actor(1));
            a.reset({350,floor},1);b.reset({390,floor},-1);
            for(unsigned d=0;d<12;++d)game.step({FighterControls{},FighterControls{.vertical=-1}});
            HIT_REQUIRE(b.guarding());b.setHealth(attackDamage*.1);b.setEnergy(0);a.present(FighterCommand::Attack);
            for(unsigned d=0;d<200&&b.health()>0;++d)game.step({FighterControls{},FighterControls{.vertical=-1}});
            HIT_REQUIRE(b.health()==0&&b.alive()&&b.dieEvents()==0&&game.phase()==RoundPhase::Playing);
            game.manualPause();const auto age=b.sourceHurt().age();for(unsigned d=0;d<12;++d)game.pausedDisplayFrame();HIT_REQUIRE(b.alive()&&b.dieEvents()==0&&b.sourceHurt().age()==age&&game.phase()==RoundPhase::Playing);game.manualResume();
            a.present(FighterCommand::Attack);for(unsigned d=0;d<300&&b.alive();++d)game.step({});
            HIT_REQUIRE(!b.alive()&&b.dieEvents()==1&&game.phase()==RoundPhase::KnockoutFlash);++sessions;word(hz);word(b.dieEvents());
        }
        MusouCampaign campaign;HIT_REQUIRE(campaign.open((folder+"MUSLETH.BVM").c_str()));
        const MusouFighterBank bank{"qa-player",std::span(&art,1),0,1000,0},foe{"qa-enemy",std::span(&art,1),0,1000,0};
        for(unsigned hz:{30u,60u}){
            MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;HIT_REQUIRE(game.start(campaign,0,stage,{bank,bank,bank},std::span(&foe,1),progress,members,0x12345678));game.setSourceProfile(hz,30);
            for(unsigned d=0;d<1500;++d){game.step({},false);for(const auto& e:game.enemies())const_cast<MusouOpponent&>(e).bossAI.reset();if(!game.presentation().starting()&&game.enemies().size()==1&&game.bossCue().kind==MusouBossCue::Kind::None)break;}
            HIT_REQUIRE(game.enemies().size()==1&&game.enemies().front().boss&&game.bossCue().kind==MusouBossCue::Kind::None);
            auto& player=const_cast<FighterMotion&>(game.member(0));auto& target=const_cast<MusouOpponent&>(game.enemies().front());player.reset({350,floor},1);player.setHealth(100000);target.motion.setRoundStats(1000,1);target.motion.reset({1100,floor},-1);target.brain={};
            for(unsigned d=0;d<12;++d)target.motion.step(stage.geometry(),{.vertical=-1},hz==30||d%2==0);
            HIT_REQUIRE(target.motion.guarding());target.motion.setHealth(10);target.motion.setEnergy(0);FighterStrike hit{};hit.key=1300;hit.damage=100;hit.hitType=1;hit.hurtFrames=4;
            HIT_REQUIRE(target.motion.receiveStrike({hit,1,350},&player));HIT_REQUIRE(target.motion.health()==0&&target.motion.alive()&&target.motion.dieEvents()==0);
            for(unsigned d=0;d<100&&target.motion.hitGap();++d){game.step({},false);for(const auto& e:game.enemies())const_cast<MusouOpponent&>(e).bossAI.reset();HIT_REQUIRE(game.bossCue().kind==MusouBossCue::Kind::None&&game.wave().result()==MusouResult::Playing&&game.kills()==0);}
            HIT_REQUIRE(target.motion.hitGap()==0&&target.motion.alive());HIT_REQUIRE(target.motion.receiveStrike({hit,1,350},&player));HIT_REQUIRE(!target.motion.alive()&&target.motion.dieEvents()==1&&game.bossCue().kind==MusouBossCue::Kind::Knockout);
            game.step({},false);HIT_REQUIRE(game.bossCue().kind==MusouBossCue::Kind::Knockout&&game.kills()==0&&game.wave().result()==MusouResult::Playing);++sessions;word(hz);word(target.motion.dieEvents());
        }
        return true;
    }
    bool run(const std::string& folder){
        if(!reference(folder))return false;
        StageAsset stage;FighterAsset art;HIT_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));HIT_REQUIRE(art.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        return actorCases(stage,art)&&sessionCases(folder,stage,art);
    }
#undef HIT_REQUIRE
};
}
