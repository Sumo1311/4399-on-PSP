// Source GameStage/TeamVO traversal in the actual ordinary fight scene.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_match.hpp"
#include "fighter_cpu.hpp"
#include "source_sprite_contact.hpp"

namespace bvn {
void FightMatch::bindSourcePaint(FightAttacker& actor,SourceScene::Key key){
    actor.sourceEmissionHook({this,
        [](void* p,FighterEffects&& effects){static_cast<FightMatch*>(p)->play(std::move(effects));},
        [](void* p,unsigned owner,const ObjectSpawn& spawn,const FighterMotion*){static_cast<FightMatch*>(p)->addObjects(std::span(&spawn,1),owner);},
        [](void* p){static_cast<FightMatch*>(p)->syncSourceObjects();}});
    actor.sourcePaintHook({this,key,[](void* context,unsigned key,bool swap,const SourceControlTarget& target){
        auto& game=*static_cast<FightMatch*>(context);auto& paint=game.sourceScene_.paint();const auto self=SourcePaint::key(SourcePaint::Kind::Sprite,key);
        if(!swap){paint.setIndex(self,0);return;}
        for(unsigned i=0;i<2;++i)if(game.actors_[i].effectTarget()==target){paint.swap(self,SourcePaint::key(SourcePaint::Kind::Sprite,i));return;}
        paint.swap(self,SourcePaint::None);
    }});
}
void FightMatch::initializeSourceScene(){
    sourceScene_.reset();effects_.sourcePaint(&sourceScene_.paint());sourceObjectKeys_.clear();sourceObjectKeys_.reserve(ObjectLimit);sourceNextObject_=0;
    sourceScene_.addTeam(0);sourceScene_.addTeam(1);sourceScene_.add(0,0);sourceScene_.add(1,1);
    for(unsigned i=0;i<objects_.size();++i){const auto key=SourceScene::Object|sourceNextObject_++;sourceObjectKeys_.push_back(key);
        sourceScene_.add(key,std::visit([](const auto& v){return v.owner();},objects_[i]),i);if(auto* actor=std::get_if<FightAttacker>(&objects_[i]))bindSourcePaint(*actor,key);}
    syncSourceObjects();
}
FightObject* FightMatch::sourceObject(SourceScene::Key key){
    const auto index=sourceScene_.storage(key);return index<objects_.size()&&sourceObjectKeys_[index]==key?&objects_[index]:nullptr;
}
void FightMatch::syncSourceObjects(){
    for(unsigned i=0;i<objects_.size();++i)if(sourceScene_.registered(sourceObjectKeys_[i])&&!std::visit([](const auto& v){return v.active();},objects_[i])){
        const auto* actor=std::get_if<FightAttacker>(&objects_[i]);sourceScene_.remove(sourceObjectKeys_[i],!actor||!actor->assister());
    }
}
void FightMatch::pruneSourceObjects(){
    if(sourceCameraFocus_)updateSourceCameraPoint();
    // Logical removal already spliced the live source lists. Move storage only
    // after no sprite/collision callback can still hold an object reference.
    for(unsigned i=0;i<objects_.size();){
        if(!objectFinished(objects_[i])){++i;continue;}
        sourceScene_.forget(sourceObjectKeys_[i]);objects_.erase(objects_.begin()+i);sourceObjectKeys_.erase(sourceObjectKeys_.begin()+i);
    }
    for(unsigned i=0;i<sourceObjectKeys_.size();++i)sourceScene_.storage(sourceObjectKeys_[i],i);
}
void FightMatch::sourceContactPair(SourceScene::Key first,SourceScene::Key second,bool hits){
    const auto sprite=[&](auto key){
        const auto display=SourcePaint::key(SourcePaint::Kind::Sprite,key);
        if(!(key&SourceScene::Object))return SourceContactSprite{&actors_[key],nullptr,&actors_[key],key,&sourceScene_.paint(),display};
        auto* object=sourceObject(key);if(!object)return SourceContactSprite{};
        return SourceContactSprite{nullptr,object,&actors_[std::visit([](const auto& v){return v.owner();},*object)],0,&sourceScene_.paint(),display};
    };
    sourceSpriteContact(sprite(first),sprite(second),hits,[](const StrikeContact& hit,const SourceContactSprite&){return hit;},
        [&](FighterEffects effects){play(std::move(effects));});
    syncSourceObjects();
}
bool FightMatch::stepSourceScene(const StageGeometry& stage,const std::array<FighterControls,2>& controls,bool animate,Contacts contacts,std::optional<CameraLimits> limits){
    sourceScene_.beginFrame(animate);animate=sourceScene_.animate();
    sourceScene_.render([&](auto key){
        if(key&SourceScene::Object){
            auto* object=sourceObject(key);if(!object)return;
            if(auto* projectile=std::get_if<FightProjectile>(object)){
                if(projectile->step(stage,animate,effects_.motionRate()))effects_.play({2,200,{}});
                syncSourceObjects();
            }else{std::vector<ObjectSpawn> shots;advanceAttacker(std::get<FightAttacker>(*object),stage,animate,limits,effects_.motionRate(),shots);}
            return;
        }
        const unsigned side=key;std::array<std::uint32_t,ObjectLimit> names{};unsigned count=0;FightAttacker* summoned=nullptr;unsigned summonSlot=0;
        for(unsigned n=0;n<objects_.size();++n)if(auto* actor=std::get_if<FightAttacker>(&objects_[n]);actor&&actor->active()&&actor->owner()==side){
            if(actor->key())names[count++]=actor->key();
            if(actor->assister()){summoned=actor;summonSlot=n;}}
        const auto input=contacts!=Contacts::Locked&&cpus_[side]?cpus_[side]->controls():controls[side];
        const auto* assist=assisters_[side].asset;
        const bool call=input.assistHeld&&assist&&actors_[side].canCallAssist()&&sourceActionsEnabled_&&
            (summoned||objects_.size()<ObjectLimit)&&contacts!=Contacts::Locked;
        if(call){
            if(!assistHits_[side])assistHits_[side]=makeRuntimeHitModel(*assist);
            const ObjectSpawn spawn{FighterOp::Summon,0,actors_[side].motion().position,actors_[side].facing(),assist,1,{},FinishClip,0,false,assistHits_[side]};
            if(summoned){
                const auto id=sourceObjectKeys_[summonSlot];sourceScene_.remove(id,false);
                *summoned=FightAttacker(*assist,side,spawn,{},nextObjectSeed());summoned->sourceHz(effectGameHz_);sourceScene_.add(id,side,summonSlot);bindSourcePaint(*summoned,id);
            }else{
                auto& object=objects_.emplace_back(std::in_place_type<FightAttacker>,*assist,side,spawn,Point{},nextObjectSeed());
                std::get<FightAttacker>(object).sourceHz(effectGameHz_);const auto id=SourceScene::Object|sourceNextObject_++;
                sourceObjectKeys_.push_back(id);sourceScene_.add(id,side,objects_.size()-1);bindSourcePaint(std::get<FightAttacker>(object),id);
            }
            playAssistEffect(side,actors_[side].motion().position);actors_[side].spendAssist();
        }
        actors_[side].step(stage,input,animate,limits,&actors_[1-side],effects_.motionRate(),std::span(names).first(count),capturesTarget(1-side));
        if(animate){++aiAnimations_[side];if(cpus_[side])cpus_[side]->update(*this,side);actors_[side].finishGhostAnimation(&actors_[1-side]);}
        if(call)actors_[side].recordAssist();
        if(animate)actors_[side].faceIdleTarget(actors_[1-side]);
        play(actors_[side].takeEffects());addObjects(actors_[side].takeSpawns(),side);
    });
    bool complete=true;
    complete=sourceScene_.pairs([&](auto a,auto b){if(sourceControl_.renderHit())sourceContactPair(a,b,animate&&contacts!=Contacts::Locked);});
    for(auto& actor:actors_){play(actor.takeEffects());}
    pruneSourceObjects();return complete;
}
}
