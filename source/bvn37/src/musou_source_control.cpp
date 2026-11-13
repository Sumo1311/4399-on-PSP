// Adapted from GameCtrl, MusouFightSession, MusouCtrl and EffectBlackBackHandler.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "musou_battle.hpp"
#include <algorithm>
#include <iterator>

namespace bvn {
void MusouBattle::bindSourceHook(FighterMotion& actor,unsigned id){
    actor.sourceHooks({this,id,
        [](void* p,unsigned id){static_cast<MusouBattle*>(p)->sourceActorDied(id);},
        [](void* p,FighterEffects&& effect){static_cast<MusouBattle*>(p)->play(std::move(effect));},
        [](void* p,unsigned id,SourceFighterEvent event){static_cast<MusouBattle*>(p)->sourceActorEvent(id,event);},
        [](void* p,unsigned actor,unsigned target,const StrikeContact& hit){static_cast<MusouBattle*>(p)->sourceComboHit(actor,target,hit);},
        [](void* p,unsigned id,const ObjectSpawn& spawn,const FighterMotion* target){
            auto& game=*static_cast<MusouBattle*>(p);
            if(!target){const auto* enemy=id<game.party_.size()?game.chooseTarget():nullptr;target=id<game.party_.size()?(enemy?&enemy->motion:nullptr):&game.player();}
            game.addObjects(std::span(&spawn,1),id,target?target->motion().position:spawn.origin);},
        [](void* p,unsigned id,std::uint32_t key){
            const auto& game=*static_cast<MusouBattle*>(p);
            return std::ranges::any_of(game.objects_,[&](const auto& object){const auto* actor=std::get_if<FightAttacker>(&object);return actor&&actor->active()&&!actor->assister()&&(actor->owner()<game.party_.size())==(id<game.party_.size())&&actor->key()==key;});},
        [](void* p,const BuffEffectRequest& request){auto& game=*static_cast<MusouBattle*>(p);game.playContact({request.burst,ContactEffectKind::Hurt,request.origin,1,{}});game.effects_.buff(request);}});
}
void MusouBattle::sourceActorDied(unsigned id){
    if(id>=party_.size()){if(sourceHits_.remove(id))sourceHitsUi_.hide();beginBossKo();return;}
    if(selfDieShown_)return;
    selfDieShown_=true;for(auto& actor:party_)actor.sourceActionEnable(false);for(auto& enemy:enemies_)enemy.motion.sourceActionEnable(false);sourceCameraFocus_=party_[id].effectTarget();updateSourceCameraPoint();camera_.focusOne();
    if(const auto* end=combatEffect("hit_end"))playContact({end,ContactEffectKind::Hurt,party_[id].motion().position,1,{}});
    FighterEffects effect;effect.addShine(0xff0000,80);play(std::move(effect));
    effects_.slow(2,5000);syncSourceSlow(2);
}

void MusouBattle::sourceActorEvent(unsigned id,SourceFighterEvent event){
    if(id>=party_.size()&&(event==SourceFighterEvent::Idle||event==SourceFighterEvent::HurtResume||event==SourceFighterEvent::HurtDown))
        if(sourceHits_.remove(id))sourceHitsUi_.hide();
    if(event!=SourceFighterEvent::Dead)return;
    if(id<party_.size()){
        wave_.playerDefeated();syncSourceResult();return;
    }
    const auto found=std::ranges::find(enemies_,id,&MusouOpponent::id);
    if(found==enemies_.end()||found->deadHandled)return;
    auto& enemy=*found;enemy.deadHandled=true;
    reward(enemy);
    if(enemy.boss){
        // onBossDead updates the camera and checks completion. It does not
        // remove the boss from GameStage or TeamVO: the dead body remains.
        resetSourceCamera(true);populateStates();const bool cleared=wave_.enemyRemoved(states_,true);
        if(cleared)for(auto& remaining:enemies_)
            if(remaining.motion.alive())remaining.motion.die();
        syncSourceResult();
    }else{
        enemy.sourceFade.start(enemy.motion.displayAlpha());enemy.fade=1;
    }
}

void MusouBattle::sourceComboHit(unsigned actor,unsigned target,const StrikeContact&){
    if(actor>=party_.size()||target<party_.size())return;
    const auto hits=sourceHits_.add(target);if(hits>1)sourceHitsUi_.show(hits,false);
}


FighterMotion* MusouBattle::sourceActor(const SourceControlTarget& target,unsigned& id){
    for(unsigned i=0;i<party_.size();++i)if(party_[i].effectTarget()==target){id=i;return &party_[i];}
    for(auto& enemy:enemies_)if(enemy.motion.effectTarget()==target){id=enemy.id;return &enemy.motion;}
    for(auto& enemy:retainedEnemies_)if(enemy.motion.effectTarget()==target){id=enemy.id;return &enemy.motion;}
    return nullptr;
}

void MusouBattle::resetSourceCamera(bool update){
    sourceCameraFocus_.reset();
    if(update){
        sourceCameraBase_={player().effectTarget(),{}};sourceCameraBaseLast_={player().motion().position,{}};sourceCameraCount_=1;
        const auto boss=std::ranges::find_if(enemies_,[](const auto& enemy){return enemy.boss&&enemy.motion.alive();});
        if(boss!=enemies_.end()){sourceCameraBase_[1]=boss->motion.effectTarget();sourceCameraBaseLast_[1]=boss->motion.motion().position;sourceCameraCount_=2;}
        camera_.updateFocus(sourceCameraCount_);
    }else if(sourceCameraCount_>1)camera_.resumeFocus();else camera_.resumeSoloFocus();
}

void MusouBattle::applyController(const SourceControlRequest& request){
    const auto change=sourceControl_.apply(request);
    if(request.op==SourceControlOp::Bisha||request.op==SourceControlOp::SuperBisha||request.op==SourceControlOp::WanKai||request.op==SourceControlOp::Ghost||request.op==SourceControlOp::Replace||request.op==SourceControlOp::Explode)effects_.blackBack(true);
    else if(change.mapChanged&&change.mapVisible)effects_.blackBack(false);
    if(change.mapChanged)effects_.mapVisible(change.mapVisible);
    if(change.cameraFocus){sourceCameraFocus_=request.target;updateSourceCameraPoint();camera_.focusOne();}
    if(change.cameraResume)resetSourceCamera();
    syncSourceCounters();
}

void MusouBattle::syncSourceCounters(){
    for(auto& actor:party_)actor.syncSourceCounter(sourceControl_.counters());
    for(auto& enemy:enemies_)enemy.motion.syncSourceCounter(sourceControl_.counters());
    for(auto& enemy:retainedEnemies_)enemy.motion.syncSourceCounter(sourceControl_.counters());
}

void MusouBattle::animateEnemyBrain(MusouOpponent& enemy){
    if(enemy.bossAI)enemy.bossAI->updateSourceBoss(enemy.motion,player(),objects_,party_.size());
    else enemy.brain.animate(enemy.motion,player(),random_,true);
}

void MusouBattle::stepSelectedAnimation(){
    for(unsigned i=0;i<sourceControl_.animations().size();++i){
        const auto selected=sourceControl_.animations()[i];unsigned id=0;
        if(auto* actor=sourceActor(selected,id)){
            auto* target=chooseTarget();auto* opponent=id<party_.size()?(target?&target->motion:nullptr):&player();
            std::array<std::uint32_t,FightMatch::ObjectLimit> names{};unsigned count=0;
            for(const auto& object:objects_)if(const auto* attacker=std::get_if<FightAttacker>(&object);
                attacker&&attacker->active()&&attacker->owner()==id&&attacker->key())names[count++]=attacker->key();
            actor->stepSourceCutinAnimation(opponent,true,stage_->geometry(),std::span(names).first(count));
            if(id>=party_.size()){
                for(auto& enemy:enemies_)if(enemy.id==id){animateEnemyBrain(enemy);break;}
                for(auto& enemy:retainedEnemies_)if(enemy.id==id){animateEnemyBrain(enemy);break;}
            }
            actor->finishGhostAnimation(opponent);
            collectSpawns(*actor,id,opponent?opponent->motion().position:actor->motion().position);
            continue;
        }
        for(unsigned object=0;object<objects_.size();++object){
            auto* attacker=std::get_if<FightAttacker>(&objects_[object]);
            if(!attacker||attacker->controllerTarget()!=selected)continue;
            const unsigned ownerId=attacker->owner();auto* source=owner(ownerId);auto* target=chooseTarget();
            auto* opponent=ownerId<party_.size()?(target?&target->motion:nullptr):&player();
            if(!source||!opponent)break;
            play(attacker->step(stage_->geometry(),true,std::nullopt,*opponent,effects_.motionRate(),spawnedObjects_,
                               source,ownerId<party_.size()?source:(target?&target->motion:nullptr)));
            addObjects(spawnedObjects_,ownerId,opponent->motion().position);spawnedObjects_.clear();break;
        }
    }
}

void MusouBattle::stepSelectedRender(){
    const auto counters=sourceControl_.renderCounters();FighterEffects requests;
    if(counters.replaceSmoke)requests.reactions.push_back({ReactionCue::ReplaceSmoke,counters.replaceOrigin});
    if(counters.explodeBurst){requests.reactions.push_back({ReactionCue::ExplodeBurst,counters.explodeOrigin});requests.counterSounds|=4;}
    play(std::move(requests));
    if(counters.change.mapChanged){effects_.mapVisible(counters.change.mapVisible);if(counters.change.mapVisible)effects_.blackBack(false);}
    syncSourceCounters();
    for(unsigned i=0;i<sourceControl_.renders().size();++i){
        const auto selected=sourceControl_.renders()[i];unsigned id=0;
        if(auto* actor=sourceActor(selected,id)){
            auto* chosen=chooseTarget();auto* target=id<party_.size()?(chosen?&chosen->motion:nullptr):&player();
            FighterControls input;
            if(sourceActions()){
                if(id<party_.size())input=sourceFrameControls_;
                else{
                    for(auto& enemy:enemies_)if(enemy.id==id)input=enemy.bossAI?enemy.bossAI->controls():enemy.brain.sourceControls(enemy.motion,player());
                    for(auto& enemy:retainedEnemies_)if(enemy.id==id)input=enemy.bossAI?enemy.bossAI->controls():enemy.brain.sourceControls(enemy.motion,player());
                }
            }
            std::array<std::uint32_t,FightMatch::ObjectLimit> names{};unsigned count=0;
            for(const auto& object:objects_)if(const auto* attacker=std::get_if<FightAttacker>(&object);
                attacker&&attacker->active()&&attacker->owner()==id&&attacker->key())names[count++]=attacker->key();
            actor->stepSourceCutinRender(stage_->geometry(),std::nullopt,effects_.motionRate(),input,target,std::span(names).first(count));
            collectSpawns(*actor,id,target?target->motion().position:actor->motion().position);
        }
    }
}

void MusouBattle::updateSourceCameraPoint(){
    if(!sourceCameraFocus_)return;
    unsigned id=0;
    if(const auto* actor=sourceActor(sourceCameraFocus_,id)){sourceCameraLast_=actor->motion().position;return;}
    for(const auto& object:objects_)if(const auto* actor=std::get_if<FightAttacker>(&object);
        actor&&actor->controllerTarget()==sourceCameraFocus_){sourceCameraLast_=actor->position();return;}
}

void MusouBattle::trackSourceCamera(){
    if(sourceCameraFocus_){updateSourceCameraPoint();camera_.track(sourceCameraLast_,sourceCameraLast_,true);return;}
    for(unsigned i=0;i<sourceCameraCount_;++i){unsigned id=0;if(const auto* actor=sourceActor(sourceCameraBase_[i],id))sourceCameraBaseLast_[i]=actor->motion().position;}
    if(sourceCameraCount_>1)camera_.track(sourceCameraBaseLast_[0],sourceCameraBaseLast_[1]);
    else if(sourceCameraCount_)camera_.trackSolo(sourceCameraBaseLast_[0]);
}

void MusouBattle::SourceUiView::victory(unsigned actor){
    if(auto* fighter=game.owner(actor)){
        fighter->present(FighterCommand::Victory);
        game.collectSpawns(*fighter,actor,fighter->motion().position);
    }
}
void MusouBattle::SourceUiView::koComplete(){
    game.freeze_.resume();game.sourceControl_.resume();
    if(game.bossCue_.kind==MusouBossCue::Kind::Knockout)game.bossCue_={};
}

bool MusouBattle::sourceActions()const{
    return !sourcePresentation_.starting()&&!sourcePresentation_.ending()&&
        bossCue_.kind!=MusouBossCue::Kind::Entrance&&!selfDieShown_;
}

void MusouBattle::sourceRoot(FighterControls controls,bool switchPressed,bool uiPaused){
    sourceHitsUi_.render();
    sourcePresentation_.beginRoot();
    sourceCallbacks_.render([&](unsigned callback){
        if(callback==1){
            if(uiPaused)return;
            if(controls.sourceHuman)controls.assistHeld=
                (controls.heldKeys&(1U<<unsigned(HumanKey::Assist)))&&
                (controls.heldKeys&(1U<<unsigned(HumanKey::Dash)));
            bool animate=false;stepOrderedFrame(controls,switchPressed,animate);
        }else if(callback==2){
            if(sourceTransition_.render(gameHz_)==SourceTransitionTimeline::Callback::End)sourceLeaveReady_=true;
            if(!sourceTransition_.rendering())sourceCallbacks_.remove(SourceRenderCallbacks::Group::Main,2);
        }
    });
    SourceUiView view{*this};sourcePresentation_.endRoot(view);
    stepDetached(true);
}

void MusouBattle::pausedDisplayFrame(){
    if(sourcePhysics_&&delivery_.advance())sourceRoot({},false,true);
}

void MusouBattle::acknowledgeReturn(){
    if(!sourcePhysics_||!sourcePresentation_.returnRequested()||sourceReturnSerial_==sourcePresentation_.returnSerial())return;
    sourceReturnSerial_=sourcePresentation_.returnSerial();
    sourceReturnClosing_=true;sourceLeaveReady_=false;sourceTransition_.in();
    sourceCallbacks_.add(SourceRenderCallbacks::Group::Main,2);
}

void MusouBattle::syncSourceResult(){
    if(wave_.result()==MusouResult::Playing||!wave_.sourceResult()||sourceResultRevision_==wave_.sourceResult()->revision())return;
    sourceResultRevision_=wave_.sourceResult()->revision();
    const bool won=wave_.result()==MusouResult::Won;
    sourcePresentation_.end(won,leader_);
    for(auto& actor:party_)actor.sourceActionEnable(false);
    for(auto& enemy:enemies_)enemy.motion.sourceActionEnable(false);
    if(won){sourceCameraFocus_=player().effectTarget();updateSourceCameraPoint();camera_.focusOne();}
}

void MusouBattle::stepSourceBossUi(){
    if(bossCue_.kind!=MusouBossCue::Kind::None)++bossCue_.ticks;
    if(sourceUiPhase_.advance()){SourceUiView view{*this};sourcePresentation_.renderAnimate(view);}
}

void MusouBattle::stepSourceSession(){
    if(bossCue_.kind==MusouBossCue::Kind::Entrance){
        auto* actor=owner(bossCue_.actor);
        if(!actor){bossCue_={};resetSourceCamera(true);}
        else if(!bossCue_.spoke){
            if(!actor->motion().airborne){actor->present(FighterCommand::Intro);bossCue_.spoke=true;
                collectSpawns(*actor,bossCue_.actor,player().motion().position);}
            return;
        }else if(sourcePresentation_.entranceDone()&&!actor->performing(FighterCommand::Intro)){
            bossCue_={};resetSourceCamera(true);
        }else return;
    }
    if(!sourceActions())return;
    populateStates();if(const auto entry=wave_.step(states_))spawn(*entry);
    recoverMembers();if(switchTicks_)--switchTicks_;
}

std::list<MusouOpponent>::iterator MusouBattle::retireEnemy(std::list<MusouOpponent>::iterator entry){
    const auto next=std::next(entry);
    if(sourcePhysics_){sourceScene_.remove(entry->id,false);retainedEnemies_.splice(retainedEnemies_.end(),enemies_,entry);}
    else enemies_.erase(entry);
    return next;
}

void MusouBattle::pruneRetainedEnemies(){
    std::erase_if(retainedEnemies_,[&](const auto& enemy){
        const auto key=enemy.motion.effectTarget();
        if(sourceCameraFocus_==key||std::ranges::find(std::span(sourceCameraBase_).first(sourceCameraCount_),key)!=std::span(sourceCameraBase_).first(sourceCameraCount_).end()||std::ranges::find(sourceControl_.animations(),key)!=sourceControl_.animations().end()||
            std::ranges::find(sourceControl_.renders(),key)!=sourceControl_.renders().end())return false;
        const bool remove=std::ranges::none_of(objects_,[&](const auto& object){
            return std::visit([&](const auto& value){return value.owner()==enemy.id;},object)&&!objectFinished(object);
        });
        if(remove){sourceScene_.forget(enemy.id);}
        return remove;
    });
}

void MusouBattle::stepOrderedFrame(FighterControls controls,bool switchPressed,bool& animate){
    ++ticks_;animate=false;sourceFrameControls_=controls;
    if(freeze_.step(gameHz_,deliveryHz_)){effects_.freezeOver();sourceControl_.resume();}
    const double previousSlow=effects_.scene().blur.slowRate;
    const bool effectAnimate=effects_.effectAnimation(false);
    if(previousSlow>0&&effects_.gameClocks()->clocks().slowRate()==0)syncSourceSlow(0);
    for(const auto& actor:party_)actor.effectTarget();
    for(const auto& enemy:enemies_)enemy.motion.effectTarget();
    shadowSources_.clear();
    const auto addShadow=[&](const FighterMotion& actor){if(actor.shadowing())shadowSources_.push_back({
        actor.shadowLifetime().get(),&actor.asset(),&actor.pose(),actor.motion().position,actor.facing(),true,
        actor.shadowTint(),actor.shadowLifetime()});};
    addShadow(player());for(const auto& enemy:enemies_)addShadow(enemy.motion);
    for(const auto& object:objects_)if(const auto* actor=std::get_if<FightAttacker>(&object);actor&&actor->shadowing())
        shadowSources_.push_back({actor->shadowLifetime().get(),&actor->asset(),&actor->pose(),actor->position(),actor->facing(),
            true,actor->shadowTint(),actor->shadowLifetime()});
    effects_.stepOrdered(effectAnimate,shadowSources_,{this,
        [](void* self){static_cast<MusouBattle*>(self)->stepSelectedAnimation();},
        [](void* self){static_cast<MusouBattle*>(self)->stepSelectedRender();},
        [](void* self){static_cast<MusouBattle*>(self)->fadeEnemies();}});
    trackSourceCamera();stepSourceBossUi();stepOptical();
    syncSourceResult();
    if(!sourceControl_.running())return;
    animate=effects_.actorAnimation(false);
    if(animate)stepSourceSession();
    syncSourceResult();
    const bool actions=sourceActions();
    player().sourceActionEnable(actions);for(auto& enemy:enemies_)enemy.motion.sourceActionEnable(actions);
    if(actions&&switchPressed)changeMember();
    auto* target=chooseTarget();auto* primary=target?&target->motion:nullptr;
    if(!stepSourceScene(actions?controls:FighterControls{},animate,primary))return;
    syncSourceResult();
    if(sourcePresentation_.afterMain())for(auto& actor:party_)actor.allowLoseHp(true);
}

} // namespace bvn
