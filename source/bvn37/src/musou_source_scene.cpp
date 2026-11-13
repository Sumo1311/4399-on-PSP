// Source GameStage/TeamVO traversal in the actual Musou scene.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "musou_battle.hpp"
#include "source_sprite_contact.hpp"

namespace bvn {
void MusouBattle::bindSourcePaint(FightAttacker& actor,SourceScene::Key key){
    actor.sourceEmissionHook({this,
        [](void* p,FighterEffects&& effects){static_cast<MusouBattle*>(p)->play(std::move(effects));},
        [](void* p,unsigned owner,const ObjectSpawn& spawn,const FighterMotion* target){static_cast<MusouBattle*>(p)->addObjects(std::span(&spawn,1),owner,target?target->motion().position:spawn.origin);},
        [](void* p){static_cast<MusouBattle*>(p)->syncSourceObjects();}});
    actor.sourcePaintHook({this,key,[](void* context,unsigned key,bool swap,const SourceControlTarget& target){
        auto& game=*static_cast<MusouBattle*>(context);auto& paint=game.sourceScene_.paint();const auto self=SourcePaint::key(SourcePaint::Kind::Sprite,key);
        if(!swap){paint.setIndex(self,0);return;}
        unsigned id=0;if(game.sourceActor(target,id)){paint.swap(self,SourcePaint::key(SourcePaint::Kind::Sprite,id));return;}
        paint.swap(self,SourcePaint::None);
    }});
}
void MusouBattle::initializeSourceScene(){
    sourceScene_.reset();effects_.sourcePaint(&sourceScene_.paint());sourceObjectKeys_.clear();sourceObjectKeys_.reserve(FightMatch::ObjectLimit);sourceNextObject_=0;
    sourceScene_.addTeam(0);sourceScene_.addTeam(1);sourceScene_.add(leader_,0);
    for(const auto& enemy:enemies_)sourceScene_.add(enemy.id,1);
    for(unsigned i=0;i<objects_.size();++i){const auto key=SourceScene::Object|sourceNextObject_++;sourceObjectKeys_.push_back(key);
        const auto id=std::visit([](const auto& v){return v.owner();},objects_[i]);sourceScene_.add(key,id<party_.size()?0:1,i);if(auto* actor=std::get_if<FightAttacker>(&objects_[i]))bindSourcePaint(*actor,key);}
    syncSourceObjects();
}
FightObject* MusouBattle::sourceObject(SourceScene::Key key){
    const auto index=sourceScene_.storage(key);return index<objects_.size()&&sourceObjectKeys_[index]==key?&objects_[index]:nullptr;
}
void MusouBattle::syncSourceObjects(){
    for(unsigned i=0;i<objects_.size();++i)if(sourceScene_.registered(sourceObjectKeys_[i])&&!std::visit([](const auto& v){return v.active();},objects_[i])){
        const auto* actor=std::get_if<FightAttacker>(&objects_[i]);sourceScene_.remove(sourceObjectKeys_[i],!actor||!actor->assister());
    }
}
void MusouBattle::pruneSourceObjects(){
    if(sourceCameraFocus_)updateSourceCameraPoint();
    for(unsigned i=0;i<objects_.size();){
        if(!objectFinished(objects_[i])){++i;continue;}
        sourceScene_.forget(sourceObjectKeys_[i]);objects_.erase(objects_.begin()+i);sourceObjectKeys_.erase(sourceObjectKeys_.begin()+i);
    }
    for(unsigned i=0;i<sourceObjectKeys_.size();++i)sourceScene_.storage(sourceObjectKeys_[i],i);
}
void MusouBattle::sourceContactPair(SourceScene::Key first,SourceScene::Key second,bool hits){
    const auto sprite=[&](auto key){
        const auto display=SourcePaint::key(SourcePaint::Kind::Sprite,key);
        if(!(key&SourceScene::Object)){auto* fighter=owner(key);return SourceContactSprite{fighter,nullptr,fighter,key,&sourceScene_.paint(),display};}
        auto* object=sourceObject(key);if(!object)return SourceContactSprite{};
        const auto id=std::visit([](const auto& v){return v.owner();},*object);return SourceContactSprite{nullptr,object,owner(id),id,&sourceScene_.paint(),display};
    };
    sourceSpriteContact(sprite(first),sprite(second),hits,[&](const StrikeContact& hit,const SourceContactSprite& from){return powered(hit,from.ownerId);},
        [&](FighterEffects effects){play(std::move(effects));});
    syncSourceObjects();
}
bool MusouBattle::stepSourceScene(FighterControls controls,bool animate,FighterMotion* primary){
    sourceScene_.beginFrame(animate);animate=sourceScene_.animate();sourceScene_.render([&](auto key){
        if(key&SourceScene::Object){
            auto* object=sourceObject(key);if(!object)return;
            if(auto* projectile=std::get_if<FightProjectile>(object)){
                if(projectile->step(stage_->geometry(),animate,effects_.motionRate()))effects_.play({2,200,{}});
            }else{
                auto& attacker=std::get<FightAttacker>(*object);const auto id=attacker.owner();
                auto* target=id<party_.size()?primary:&player();auto* source=owner(id);
                if(target&&source){play(attacker.step(stage_->geometry(),animate,std::nullopt,*target,effects_.motionRate(),spawnedObjects_,source,id<party_.size()?source:primary));
                    syncSourceObjects();addObjects(spawnedObjects_,id,target->motion().position);spawnedObjects_.clear();}
            }
            syncSourceObjects();return;
        }
        if(key<party_.size()){
            std::array<std::uint32_t,FightMatch::ObjectLimit> names{};unsigned count=0;
            for(const auto& object:objects_)if(const auto* attacker=std::get_if<FightAttacker>(&object);attacker&&attacker->active()&&attacker->owner()==key&&attacker->key())names[count++]=attacker->key();
            auto& actor=party_[key];actor.step(stage_->geometry(),controls,animate,std::nullopt,primary,effects_.motionRate(),std::span(names).first(count),hidesPlayer());
            if(animate){actor.finishGhostAnimation(primary);if(primary)actor.faceIdleTarget(*primary);}
            collectSpawns(actor,key,primary?primary->motion().position:actor.motion().position);return;
        }
        const auto found=std::ranges::find(enemies_,key,&MusouOpponent::id);if(found==enemies_.end())return;auto& enemy=*found;
        FighterControls input;if(sourceActions()&&enemy.motion.alive())input=enemy.bossAI?enemy.bossAI->controls():enemy.brain.sourceControls(enemy.motion,player());
        enemy.motion.step(stage_->geometry(),input,animate,std::nullopt,&player(),effects_.motionRate(),{},hidesEnemy(enemy.id));
        if(animate){animateEnemyBrain(enemy);enemy.motion.finishGhostAnimation(&player());enemy.motion.faceIdleTarget(player());}
        collectSpawns(enemy.motion,enemy.id,player().motion().position);
    });
    const bool complete=sourceScene_.pairs([&](auto a,auto b){if(sourceControl_.renderHit())sourceContactPair(a,b,animate);});
    pruneSourceObjects();return complete;
}
}
