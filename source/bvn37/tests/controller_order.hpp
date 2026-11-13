// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"
#include <cstdio>
#include <cmath>
#include <string>

namespace bvn::controller_order_qa {
struct Checks {
    unsigned error=0,rows=0,cutins=0,ghosts=0,ghostFrames=0,hooks=0,dual=0;
    std::uint64_t hash=14695981039346656037ull;
    void word(unsigned value){for(unsigned i=0;i<4;++i)hash=(hash^((value>>(i*8))&255))*1099511628211ull;}
#define ORDER_REQUIRE(v) do{if(!(v)){error=__LINE__;return false;}}while(false)
    bool reference(const std::string& folder){
        FILE* file=std::fopen((folder+"ORDER.IN").c_str(),"r");ORDER_REQUIRE(file);
        unsigned count=0;ORDER_REQUIRE(std::fscanf(file,"%u",&count)==1&&count==29160);
        const std::array<SourceControlTarget,2> targets{std::make_shared<int>(0),std::make_shared<int>(1)};
        SourceGameControl control;bool visible=true;unsigned focus=0,resume=0;std::array<unsigned,2> end{3,3};
        const auto mask=[&](const auto& list){unsigned bits=0;for(const auto& value:list)for(unsigned k=0;k<2;++k)if(value==targets[k])bits|=1<<k;return bits;};
        const auto apply=[&](unsigned op,unsigned target){
            const auto change=control.apply({SourceControlOp(op),targets[target]});
            if(change.mapChanged)visible=change.mapVisible;focus+=change.cameraFocus;resume+=change.cameraResume;
        };
        for(unsigned row=0;row<count;++row){
            std::array<unsigned,13> v{};for(auto& x:v)ORDER_REQUIRE(std::fscanf(file,"%u",&x)==1);
            if(!v[1]){control={};visible=true;focus=resume=0;end={3,3};}
            unsigned animations=0,renders=0;
            if(v[2]<=6){if(v[2]<=2||v[2]==5)end[v[3]]=v[2]==5?6:v[2]==2?4:3;apply(v[2],v[3]);}
            else if(v[2]==7)control.pause();else if(v[2]==8)control.resume();
            else if(v[2]==9){
                for(unsigned i=0;i<control.animations().size();++i){
                    const auto target=control.animations()[i];unsigned key=target==targets[1];
                    animations|=1<<key;apply(end[key],key);
                }
            }else if(v[2]==10)renders=mask(control.renders());else ORDER_REQUIRE(false);
            const std::array<unsigned,9> got{unsigned(control.running()),unsigned(control.renderHit()),mask(control.animations()),
                mask(control.renders()),unsigned(visible),focus,resume,animations,renders};
            for(unsigned i=0;i<got.size();++i){ORDER_REQUIRE(got[i]==v[4+i]);word(got[i]);}
            ++rows;
        }
        std::fclose(file);return true;
    }
    bool effectHooks(){
        struct Hook {
            FightEffects* effects;bool requested=false;
            static void animate(void* context){
                auto& self=*static_cast<Hook*>(context);if(self.requested)return;self.requested=true;
                self.effects->slow(1.5,1000);
                FighterEffects shine;shine.addShine(0xffffff,50);self.effects->play(shine);
            }
            static void render(void*){}
        };
        FightEffects effects;effects.setProfile(30,30);effects.deliveryHz(30);effects.enableGameClocks(30);
        Hook hook{&effects};const FightEffects::FrameCallbacks callbacks{&hook,Hook::animate,Hook::render};
        effects.stepOrdered(effects.effectAnimation(false),{},callbacks,0);
        // SlowHandler.renderAnimate follows the selected caster, so a newly
        // requested 250 ms blur consumes one animation immediately. Shine's
        // logical render preceded the caster, leaving this new shine intact.
        ORDER_REQUIRE(hook.requested&&effects.scene().blur.frames==6);
        ORDER_REQUIRE(effects.scene().shines.size()==1&&effects.scene().shines[0].alpha==127);
        effects.stepOrdered(effects.effectAnimation(false),{},callbacks,33);
        ORDER_REQUIRE(effects.scene().shines[0].alpha==124);
        ++hooks;word(6);word(127);word(124);return true;
    }
    bool game(const std::string& folder){
        StageAsset stage;FighterAsset asset;
        ORDER_REQUIRE(stage.open((folder+"JIANCHANG.BVN").c_str()));
        ORDER_REQUIRE(asset.open((folder+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
        const double floor=stage.geometry().playerBottom;
        for(unsigned hz:{30u,60u})for(const auto command:{FighterCommand::Super,FighterCommand::SuperUp}){
            FightMatch game(asset,asset);game.reset(stage,{350,floor},{700,floor});game.setSourceProfile(hz,30,stage);
            game.actor(0).setQi(300);
            game.step(stage.geometry(),{{{},{.horizontal=1}}},false,Contacts::Locked);
            game.actor(0).present(command);
            bool saw=false,resumed=false;
            for(unsigned tick=0;tick<240;++tick){
                const bool was=game.cinematic();const auto before=game.actor(1).motion();
                game.step(stage.geometry(),{{{},{.horizontal=1}}},false,Contacts::Locked);
                if(was){saw=true;
                    if(!game.cinematic()){
                        ORDER_REQUIRE(game.sourceControl()->running()&&game.sourceControl()->renderHit());
                        ORDER_REQUIRE(std::abs(game.actor(1).motion().position.x-before.position.x)>1e-5);
                        resumed=true;break;
                    }
                    ORDER_REQUIRE(!game.sourceControl()->running());
                    ORDER_REQUIRE(game.actor(1).motion().position.x==before.position.x);
                }
            }
            ORDER_REQUIRE(saw&&resumed);++cutins;word(hz);word(unsigned(command));
        }
        for(unsigned hz:{30u,60u}){
            FightMatch game(asset,asset);game.reset(stage,{350,floor},{700,floor});game.setSourceProfile(hz,30,stage);
            for(unsigned side=0;side<2;++side){game.actor(side).setQi(300);game.actor(side).present(FighterCommand::Super);}
            bool both=false,one=false,done=false;
            for(unsigned tick=0;tick<240;++tick){
                const auto before=game.sourceControl()->animations().size();
                game.step(stage.geometry(),{},false,Contacts::Locked);
                const auto after=game.sourceControl()->animations().size();
                both|=after==2;
                if(both&&before==2&&after==1){one=true;ORDER_REQUIRE(!game.sourceControl()->running());}
                if(one&&before==1&&!after){done=true;ORDER_REQUIRE(game.sourceControl()->running());break;}
            }
            ORDER_REQUIRE(both&&one&&done);++dual;word(hz);word(dual);
        }
        for(unsigned hz:{30u,60u})for(auto kind:{GhostKind::Jump,GhostKind::Down}){
            FightMatch game(asset,asset);game.reset(stage,{350,kind==GhostKind::Down?floor-300:floor},{700,floor});
            game.setSourceProfile(hz,30,stage);game.actor(0).setQi(300);game.actor(0).setEnergy(100);
            game.step(stage.geometry(),{{{},{.horizontal=1}}},false,Contacts::Locked);
            FighterControls request{.aiControlled=true,.aiIntents=1U<<unsigned(kind==GhostKind::Jump?AiIntent::GhostJump:AiIntent::GhostJumpDown)};
            game.step(stage.geometry(),{{request,{.horizontal=1}}},false,Contacts::Locked);
            ORDER_REQUIRE(game.actor(0).ghostActive()&&game.actor(0).ghostKind()==kind);
            unsigned checked=0;bool resumed=false;
            for(unsigned tick=0;tick<80;++tick){
                const auto before=game.actor(0).motion();const auto other=game.actor(1).motion();
                const bool was=game.actor(0).ghostActive();const auto remaining=game.actor(0).ghostFrames();
                game.step(stage.geometry(),{{{},{.horizontal=1}}},false,Contacts::Locked);
                const auto after=game.actor(0).motion();
                if(was&&!game.actor(0).ghostActive()){
                    ORDER_REQUIRE(game.sourceControl()->running());
                    ORDER_REQUIRE(std::abs(game.actor(1).motion().position.x-other.position.x)>1e-5);
                    resumed=true;break;
                }
                if(was&&remaining>1){
                    ORDER_REQUIRE(std::abs(after.position.y-before.position.y-(before.velocity.y+before.secondaryVelocity.y)*before.speed)<1e-6);
                    ORDER_REQUIRE(after.gravity()==before.gravity());++checked;++ghostFrames;
                }
            }
            ORDER_REQUIRE(checked>3&&resumed);++ghosts;word(hz);word(unsigned(kind));word(checked);
        }
        return true;
    }
    bool run(std::string folder){if(!folder.empty()&&folder.back()!='/')folder+='/';return reference(folder)&&effectHooks()&&game(folder);}
#undef ORDER_REQUIRE
};
} // namespace bvn::controller_order_qa
