// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "controller_order.hpp"
#include "musou_battle.hpp"

namespace bvn::controller_session_qa {
struct Checks {
    unsigned error=0,referenceRows=0,counterRows=0,duals=0,musou=0,ghosts=0,bossUi=0,manual=0,bossAi=0;
    unsigned typedBanks=0,typedEnds=0,authoredGhosts=0,transformations=0;
    std::uint64_t hash=14695981039346656037ull;
    void word(unsigned x){for(unsigned i=0;i<4;++i)hash=(hash^((x>>(8*i))&255))*1099511628211ull;}
#define SESSION_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
    bool counters(const std::string& folder){
        FILE* file=std::fopen((folder+"COUNTER.IN").c_str(),"r");SESSION_REQUIRE(file);
        unsigned count=0;SESSION_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==3840);
        const std::array<SourceControlTarget,2> keys{std::make_shared<int>(0),std::make_shared<int>(1)};
        SourceGameControl control;bool visible=true;unsigned smoke=0,burst=0;
        const auto mask=[&](const auto& list){unsigned bits=0;for(const auto& key:list)for(unsigned i=0;i<2;++i)if(key==keys[i])bits|=1<<i;return bits;};
        for(unsigned i=0;i<count;++i){
            std::array<double,25> v{};for(auto& x:v)SESSION_REQUIRE(std::fscanf(file,"%lf",&x)==1);
            if(!v[2]){control.initialize(unsigned(v[0]));visible=true;smoke=burst=0;}
            const auto tick=control.renderCounters();smoke+=tick.replaceSmoke;burst+=tick.explodeBurst;
            if(tick.change.mapChanged)visible=tick.change.mapVisible;
            for(unsigned at:{3u,7u})if(v[at]>=0){
                const unsigned op=unsigned(v[at]);
                if(op==9)control.pause();else if(op==10)control.resume();else{
                    const auto change=control.apply({SourceControlOp(op),keys[unsigned(v[at+1])],{v[at+2],v[at+3]}});
                    if(change.mapChanged)visible=change.mapVisible;
                }
            }
            const auto c=control.counters();
            const std::array<double,14> got{double(control.running()),double(control.renderHit()),double(mask(control.animations())),
                double(mask(control.renders())),double(visible),double(c.replaceFrame),double(c.replaceHold),double(c.explodeFrame),
                c.replaceOrigin.x,c.replaceOrigin.y,c.explodeOrigin.x,c.explodeOrigin.y,double(smoke),double(burst)};
            for(unsigned k=0;k<got.size();++k){SESSION_REQUIRE(got[k]==v[11+k]);word(unsigned(got[k]));}
            ++counterRows;
        }
        std::fclose(file);return true;
    }
    bool simultaneousCounters(const StageAsset& stage,const FighterAsset& asset){
        for(unsigned hz:{30u,60u})for(unsigned faction:{0u,1u}){
            FightMatch game(asset,asset);const double floor=stage.geometry().playerBottom;
            game.reset(stage,{350,floor},{700,floor});game.setSourceProfile(hz,30,stage);
            FighterStrike hit{};hit.damage=1;hit.key=17;hit.hurtFrames=30;hit.hitType=1;
            for(unsigned i=0;i<2;++i){game.actor(i).setFaction(faction);game.actor(i).setQi(300);game.actor(i).setEnergy(100);
                SESSION_REQUIRE(game.actor(i).receiveStrike({hit,1,500}));}
            game.step(stage.geometry(),{{{.assistHeld=true},{.assistHeld=true}}},false,Contacts::Locked);
            const auto& c=game.sourceControl()->counters();
            SESSION_REQUIRE(game.actor(0).counterStarts()==1&&game.actor(1).counterStarts()==1);
            SESSION_REQUIRE(!game.actor(0).counterPaused()&&game.actor(1).counterPaused());
            const auto target=faction?c.replaceTarget:c.explodeTarget;
            SESSION_REQUIRE(target==game.actor(1).effectTarget());
            SESSION_REQUIRE(faction?c.replaceFrame==0:c.explodeFrame==unsigned(.7*hz));
            game.step(stage.geometry(),{},false,Contacts::Locked);
            SESSION_REQUIRE(game.actor(1).counterGameFrame()==1);
            if(faction){unsigned smokes=0;for(const auto& view:game.scene().reactions)if(view.cue==ReactionCue::ReplaceSmoke){
                ++smokes;SESSION_REQUIRE(view.origin.x==c.replaceOrigin.x&&view.origin.y==c.replaceOrigin.y);}
                SESSION_REQUIRE(smokes==1);}
            ++duals;word(hz);word(faction);
        }
        return true;
    }
    bool enter(MusouBattle& game,const MusouCampaign& campaign,const StageAsset& stage,const FighterAsset& asset,
               unsigned hz,MusouProgress& progress,std::array<MusouMember,3>& members){
        const MusouFighterBank player{"qa-player",std::span(&asset,1),0,1000,0},enemy{"qa-enemy",std::span(&asset,1),0,1000,0};
        SESSION_REQUIRE(game.start(campaign,0,stage,{player,player,player},std::span(&enemy,1),progress,members,0x12345678));
        game.setSourceProfile(hz,30);
        for(unsigned frame=0;frame<700;++frame){
            game.step({},false);
            for(const auto& value:game.enemies())const_cast<MusouOpponent&>(value).bossAI.reset();
            if(game.enemies().size()==1&&game.bossCue().kind==MusouBossCue::Kind::None)break;
        }
        SESSION_REQUIRE(game.enemies().size()==1&&game.bossCue().kind==MusouBossCue::Kind::None);
        SESSION_REQUIRE(game.sourceControl()->animations().empty()&&game.sourceControl()->renders().empty());
        auto& actor=const_cast<FighterMotion&>(game.member(game.leader()));auto& enemyActor=const_cast<MusouOpponent&>(game.enemies().front());
        actor.reset({350,stage.geometry().playerBottom},1);actor.setQi(300);actor.setHealth(100000);
        enemyActor.motion.reset({1100,stage.geometry().playerBottom},-1);enemyActor.motion.setQi(300);enemyActor.motion.setHealth(100000);
        enemyActor.brain={};return true;
    }
    bool musouCases(const std::string& folder,const StageAsset& stage,const FighterAsset& asset){
        MusouCampaign campaign;SESSION_REQUIRE(campaign.open((folder+"MUSLETH.BVM").c_str()));
        for(unsigned hz:{30u,60u})for(unsigned who:{0u,1u,2u}){
            MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;
            if(!enter(game,campaign,stage,asset,hz,progress,members))return false;
            auto& player=const_cast<FighterMotion&>(game.member(game.leader()));auto& enemy=const_cast<MusouOpponent&>(game.enemies().front());
            if(who!=1)player.present(FighterCommand::Super);if(who!=0)enemy.motion.present(FighterCommand::Super);
            bool saw=false,resumed=false,both=false,one=false;
            for(unsigned frame=0;frame<600;++frame){
                const auto size=game.sourceControl()->animations().size();const auto before=game.gameClocks()->view();
                const auto position=enemy.motion.motion().position;
                if(!game.step({},false))continue;
                const auto after=game.gameClocks()->view();const auto next=game.sourceControl()->animations().size();
                both|=next==2;
                if(size){saw=true;
                    if(!next){SESSION_REQUIRE(after.actorFrames>before.actorFrames||after.actorPhase!=before.actorPhase);resumed=true;break;}
                    SESSION_REQUIRE(after.actorFrames==before.actorFrames&&after.actorPhase==before.actorPhase);
                    if(who==0)SESSION_REQUIRE(enemy.motion.motion().position.x==position.x&&enemy.motion.motion().position.y==position.y);
                    if(size==2&&next==1)one=true;
                }
            }
            SESSION_REQUIRE(saw&&resumed);if(who==2)SESSION_REQUIRE(both&&one);
            ++musou;word(hz);word(who);
        }
        for(unsigned hz:{30u,60u}){
            MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;
            if(!enter(game,campaign,stage,asset,hz,progress,members))return false;
            const_cast<FighterMotion&>(game.member(game.leader())).setEnergy(100);
            const FighterControls input{.aiControlled=true,.aiIntents=1U<<unsigned(AiIntent::GhostJump)};
            for(unsigned display=0;display<2;++display)if(game.step(input,false))break;
            SESSION_REQUIRE(game.member(game.leader()).ghostActive());unsigned moved=0;bool ended=false;
            for(unsigned display=0;display<120;++display){
                const auto before=game.member(game.leader()).motion();const bool active=game.member(game.leader()).ghostActive();
                const auto clock=game.gameClocks()->view();
                if(!game.step({},false))continue;
                if(active&&!game.member(game.leader()).ghostActive()){
                    const auto after=game.gameClocks()->view();SESSION_REQUIRE(after.actorFrames>clock.actorFrames||after.actorPhase!=clock.actorPhase);ended=true;break;
                }
                if(active){SESSION_REQUIRE(std::abs(game.member(game.leader()).motion().position.y-before.position.y-
                    (before.velocity.y+before.secondaryVelocity.y)*before.speed)<1e-6);
                    SESSION_REQUIRE(game.member(game.leader()).motion().gravity()==before.gravity());++moved;}
            }
            SESSION_REQUIRE(ended&&moved>3);++ghosts;word(hz);word(moved);
        }
        for(unsigned hz:{30u,60u}){
            MusouProgress progress(campaign);std::array<MusouMember,3> members{};MusouBattle game;
            if(!enter(game,campaign,stage,asset,hz,progress,members))return false;
            auto& player=const_cast<FighterMotion&>(game.member(game.leader()));auto& enemy=const_cast<MusouOpponent&>(game.enemies().front());
            enemy.motion.setHealth(1);player.present(FighterCommand::Attack);
            for(unsigned frame=0;frame<400&&game.bossCue().kind!=MusouBossCue::Kind::Knockout;++frame){
                player.place({350,stage.geometry().playerBottom});enemy.motion.place({390,stage.geometry().playerBottom});game.step({},false);
            }
            SESSION_REQUIRE(game.bossCue().kind==MusouBossCue::Kind::Knockout);
            SESSION_REQUIRE(!game.sourceControl()->running());const auto before=game.gameClocks()->view();unsigned held=0;
            for(unsigned frame=0;frame<700;++frame){
                if(!game.step({},false))continue;
                const auto after=game.gameClocks()->view();
                if(game.bossCue().kind==MusouBossCue::Kind::None){
                    SESSION_REQUIRE(after.actorFrames>before.actorFrames||after.actorPhase!=before.actorPhase);break;
                }
                SESSION_REQUIRE(after.actorFrames==before.actorFrames&&after.actorPhase==before.actorPhase);++held;
            }
            SESSION_REQUIRE(game.bossCue().kind==MusouBossCue::Kind::None&&held>2*hz);
            SESSION_REQUIRE(game.gameClocks()->view().effectFrames>before.effectFrames);++bossUi;word(hz);word(held);
        }
        return true;
    }
    bool manualAndBoss(const StageAsset& stage,const FighterAsset& asset){
        for(unsigned hz:{30u,60u}){
            FightMatch match(asset,asset);const double y=stage.geometry().playerBottom;
            match.reset(stage,{350,y},{700,y});match.setSourceProfile(hz,30,stage);match.actor(0).present(FighterCommand::Super);
            for(unsigned frame=0;frame<100&&match.sourceControl()->animations().empty();++frame)match.step(stage.geometry(),{},false,Contacts::Locked);
            SESSION_REQUIRE(!match.sourceControl()->animations().empty()&&!match.sourceControl()->running());
            match.manualPause();const auto before=match.gameClocks()->view();match.manualResume();
            SESSION_REQUIRE(match.sourceControl()->running()&&!match.sourceControl()->renderHit());
            match.step(stage.geometry(),{},false,Contacts::Locked);const auto after=match.gameClocks()->view();
            SESSION_REQUIRE(after.actorFrames>before.actorFrames||after.actorPhase!=before.actorPhase);++manual;word(hz);
            FighterMotion self(asset),target(asset);self.reset({350,y},1);target.reset({450,y},-1);
            self.sourceHz(hz);target.sourceHz(hz);self.setQi(300);
            FighterCpu boss(17,2,hz);FighterAiPolicy expected(2,17);
            const unsigned blocked=(1U<<unsigned(AiIntent::Ultimate))|(1U<<unsigned(AiIntent::Assist))|
                (1U<<unsigned(AiIntent::CatchAttack))|(1U<<unsigned(AiIntent::CatchSpecial))|
                (1U<<unsigned(AiIntent::GhostStep))|(1U<<unsigned(AiIntent::GhostJump))|(1U<<unsigned(AiIntent::GhostJumpDown));
            for(unsigned frame=0;frame<60;++frame){
                boss.updateSourceBoss(self,target,{},3);const auto reference=expected.step(boss.lastObservation());
                SESSION_REQUIRE(boss.level()==2&&boss.lastObservation().gameHz==hz);
                SESSION_REQUIRE(boss.intents()==(reference&~blocked)&&boss.policyRandomState()==expected.randomState());
                self.step(stage.geometry(),{},true,std::nullopt,&target);target.step(stage.geometry(),{},true,std::nullopt,&self);
                ++bossAi;
            }
        }
        return true;
    }
    bool authored(const std::string& folder,const StageAsset& stage,const FighterAsset& rival){
        FILE* list=std::fopen((folder+"TYPED.IN").c_str(),"r");SESSION_REQUIRE(list);
        unsigned banks=0;SESSION_REQUIRE(std::fscanf(list,"%u",&banks)==1&&banks==78);
        std::array<unsigned,4> methods{};
        for(unsigned i=0;i<banks;++i){
            char name[64]{};SESSION_REQUIRE(std::fscanf(list,"%63s",name)==1);
            FighterAsset asset;SESSION_REQUIRE(asset.open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory));
            SESSION_REQUIRE(asset.hasObjectAreas());
            for(unsigned clip=0;clip<asset.clipCount();++clip)for(unsigned phase=0;phase<asset.clip(MoveClip(clip)).count;++phase)
                for(const auto& event:asset.events(asset.frame(MoveClip(clip),phase)))if(event.op==FighterOp::EndCutin){
                    const unsigned method=unsigned(event.command);SESSION_REQUIRE(method>=1&&method<=3);
                    ++methods[method];++typedEnds;
                }
            ++typedBanks;
        }
        std::fclose(list);SESSION_REQUIRE((methods==std::array<unsigned,4>{0,296,23,2}));
        const double y=stage.geometry().playerBottom;
        for(const char* name:{"HINATA.BVF","TENSA.BVF"})for(unsigned hz:{30u,60u})for(unsigned side:{0u,1u}){
            FighterAsset asset;SESSION_REQUIRE(asset.open((folder+name).c_str(),FighterAsset::TextureResidency::LowMemory));
            FightMatch game(side?rival:asset,side?asset:rival);game.reset(stage,{350,y},{500,y});game.setSourceProfile(hz,30,stage);
            game.actor(side).present(name[0]=='H'?FighterCommand::SpecialUp:FighterCommand::SuperDown);
            bool saw=false,ended=false;unsigned selected=0;
            for(unsigned frame=0;frame<1500;++frame){
                const auto size=game.sourceControl()->renders().size();const auto before=game.gameClocks()->view();
                game.step(stage.geometry(),{},false,Contacts::Locked);
                const auto next=game.sourceControl()->renders().size();const auto after=game.gameClocks()->view();
                if(next){saw=true;++selected;SESSION_REQUIRE(!game.actor(side).ghostActive());
                    SESSION_REQUIRE(game.sourceControl()->animations().size()==1&&!game.sourceControl()->running());
                    SESSION_REQUIRE(game.sourceControl()->renders()[0]==game.actor(side).effectTarget());
                    if(size)SESSION_REQUIRE(after.actorFrames==before.actorFrames&&after.actorPhase==before.actorPhase);
                }
                if(size&&!next){SESSION_REQUIRE(game.sourceControl()->animations().empty()&&game.sourceControl()->running());
                    SESSION_REQUIRE(after.actorFrames>before.actorFrames||after.actorPhase!=before.actorPhase);
                    SESSION_REQUIRE(game.actor(side).cutin().kind!=CutinKind::GhostStep);ended=true;break;}
            }
            SESSION_REQUIRE(saw&&ended&&selected>3);++authoredGhosts;word(hz);word(side);word(selected);
        }
        for(const char* family:{"ICHIGO","NARUTO"})for(unsigned hz:{30u,60u}){
            std::array<FighterAsset,2> forms;
            const std::string name=family;
            SESSION_REQUIRE(forms[0].open((folder+name+".BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
            SESSION_REQUIRE(forms[1].open((folder+name+(name=="ICHIGO"?"B":"1")+".BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
            FightMatch game(forms,std::span(&rival,1));game.reset(stage,{350,y},{700,y});game.setSourceProfile(hz,30,stage);game.actor(0).setQi(300);
            bool saw=false,ended=false;
            for(unsigned frame=0;frame<1500;++frame){
                const auto size=game.sourceControl()->animations().size();const auto before=game.gameClocks()->view();
                game.step(stage.geometry(),{{{.transformHeld=frame<2},{}}},false,Contacts::Locked);
                const auto next=game.sourceControl()->animations().size();saw|=next!=0;
                if(size&&!next){const auto after=game.gameClocks()->view();
                    SESSION_REQUIRE(game.actor(0).form()==1&&game.sourceControl()->running()&&game.sourceControl()->renderHit());
                    SESSION_REQUIRE(after.actorFrames>before.actorFrames||after.actorPhase!=before.actorPhase);ended=true;break;}
            }
            SESSION_REQUIRE(saw&&ended);++transformations;word(hz);word(game.actor(0).form());
        }
        word(typedBanks);word(typedEnds);return true;
    }
    bool run(std::string folder){
        if(!folder.empty()&&folder.back()!='/')folder+='/';
        controller_order_qa::Checks old;
        if(!old.run(folder)){error=100000+old.error;return false;}referenceRows=old.rows;word(unsigned(old.hash));word(unsigned(old.hash>>32));
        if(!counters(folder))return false;
        StageAsset stage;FighterAsset asset;
        SESSION_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        SESSION_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        return simultaneousCounters(stage,asset)&&manualAndBoss(stage,asset)&&musouCases(folder,stage,asset)&&authored(folder,stage,asset);
    }
#undef SESSION_REQUIRE
};
} // namespace bvn::controller_session_qa
