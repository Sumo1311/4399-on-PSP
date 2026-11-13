// Adapted from 5DPLAY Game Studio's GameMainLogicCtrl.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_match.hpp"
#include "combat_dispatch.hpp"
#include "fighter_cpu.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace bvn {
namespace {

unsigned ownerOf(const FightObject& object) {
    return std::visit([](const auto& member) { return member.owner(); }, object);
}

constexpr std::uint32_t strikeKey(std::string_view id) {
    std::uint32_t hash = 2166136261;
    for (const char ch : id) hash = (hash ^ std::uint8_t(ch)) * 16777619;
    return hash;
}

unsigned hitPoints(const StrikeContact& contact,bool classic) {
    // The source's combo lookup uses a numeric key while the hit table uses UUIDs.
    const auto& strike = contact.strike;
    const bool throwSkill = strike.key == strikeKey("sh1") || strike.key == strikeKey("sh2");
    return sourceHitScore(contact.power(),strike.bisha(),throwSkill,strike.breaksGuard(),strike.launches(),classic);
}

unsigned checkHit(FightProjectile& projectile,FighterMotion& target,FighterMotion& owner,bool classic,bool source,const auto& emit) {
    if (!inStrikeRange(projectile.position(),target.motion().position)) return 0;
    const auto strike = projectile.strikeAgainst(target.motion().position, target.hurtBody());
    const auto counter = target.strikeAgainst(projectile.position(), projectile.body());
    if (counter && projectile.owner() == 1) {
        target.noteStrike(*counter);
        projectile.receiveHit(counter->damage());
    }
    unsigned points = 0;
    if (strike) {
        const bool hurt=target.receiveStrike(*strike,&owner);
        if(!source&&hurt)points=hitPoints(*strike,classic);
        emit(target.takeEffects());
        projectile.hitFighter(target.motion().touchingSide);
        if(!source||projectile.sourceMainOwner())owner.scoreObjectStrike(*strike,.1,&target);
    }
    if (counter && projectile.owner() == 0) {
        target.noteStrike(*counter);
        projectile.receiveHit(counter->damage());
    }
    return points;
}

void checkHit(FightProjectile& first, FightProjectile& second) {
    const auto forward = first.strikeAgainst(second.position(), second.body());
    const auto backward = second.strikeAgainst(first.position(), first.body());
    if (forward) {
        second.receiveHit(first.maxHealth());
        first.hitProjectile();
    }
    if (backward) {
        first.receiveHit(second.maxHealth());
        second.hitProjectile();
    }
}

unsigned checkHit(FightAttacker& attacker,FighterMotion& target,FighterMotion& owner,bool classic,bool source,const auto& emit) {
    if (!inStrikeRange(attacker.position(),target.motion().position)) return 0;
    if (auto strike = attacker.strikeAgainst(target.motion().position, target.hurtBody())) {
        strike->recoil={&attacker,[](void* p,double x){static_cast<FightAttacker*>(p)->sourceRecoil(x);}};
        const bool hurt = target.receiveStrike(*strike, &owner);
        emit(target.takeEffects());
        owner.scoreObjectStrike(*strike,attacker.asset().assister()?.15:.13,&target);
        return !source&&hurt?hitPoints(*strike,classic):0;
    }
    return 0;
}

void checkHit(FightAttacker& attacker, FightProjectile& target, FighterMotion& owner) {
    if (const auto strike = attacker.strikeAgainst(target.position(), target.body())) {
        target.receiveHit(strike->damage());
        owner.scoreObjectStrike(*strike, attacker.asset().assister() ? 0.15 : 0.13);
    }
}

void checkHit(FightObject& first, FightObject& second, FighterMotion* actors) {
    auto* firstShot = std::get_if<FightProjectile>(&first);
    auto* secondShot = std::get_if<FightProjectile>(&second);
    if (firstShot && secondShot) checkHit(*firstShot, *secondShot);
    else if (firstShot) checkHit(std::get<FightAttacker>(second), *firstShot, actors[1]);
    else if (secondShot) checkHit(std::get<FightAttacker>(first), *secondShot, actors[0]);
}

} // namespace

void FightMatch::setSourceProfile(unsigned hz,unsigned shineHz,const StageAsset& stage){
    if(!sourcePhysics_)initializeSourceScene();
    bindSourceHooks();
    hz=hz==30?30:60;sourcePhysics_=true;deliveryHz_=hz;setEffectProfile(hz,shineHz);
    sourceControl_.initialize(hz);sourceStage_=stage.geometry();
    effects_.deliveryHz(hz);effects_.enableGameClocks(hz);
    for(auto& actor:actors_){actor.sourceHz(hz);actor.orderedSourceControl();}
    for(auto& object:objects_)std::visit([&](auto& value){value.sourceHz(hz);},object);
    camera_.sourceHz(hz);camera_.reset(stage);
}

void FightMatch::syncSourceSlow(double rate){
    if(!sourcePhysics_)return;
    const double speed=rate>0?1/rate:1;
    camera_.slow(rate);
    for(auto& actor:actors_)actor.speedRate(speed);
    for(auto& object:objects_)if(auto* actor=std::get_if<FightAttacker>(&object))actor->speedRate(speed);
}

void FightMatch::bindSourceHooks(){
    for(unsigned i=0;i<2;++i)actors_[i].sourceHooks({this,i,
        [](void* p,unsigned side){auto& match=*static_cast<FightMatch*>(p);if(match.sourceHooks_.death)match.sourceHooks_.death(match.sourceHooks_.context,side);},
        [](void* p,FighterEffects&& effect){static_cast<FightMatch*>(p)->play(std::move(effect));},
        [](void* p,unsigned id,SourceFighterEvent event){static_cast<FightMatch*>(p)->sourceComboEvent(id,event);},
        [](void* p,unsigned actor,unsigned target,const StrikeContact& contact){static_cast<FightMatch*>(p)->sourceComboHit(actor,target,contact);},
        [](void* p,unsigned owner,const ObjectSpawn& spawn,const FighterMotion*){static_cast<FightMatch*>(p)->addObjects(std::span(&spawn,1),owner);},
        [](void* p,unsigned owner,std::uint32_t key){
            const auto& objects=static_cast<FightMatch*>(p)->objects_;
            return std::ranges::any_of(objects,[&](const auto& object){const auto* actor=std::get_if<FightAttacker>(&object);return actor&&actor->active()&&!actor->assister()&&actor->owner()==owner&&actor->key()==key;});},
        [](void* p,const BuffEffectRequest& request){auto& game=*static_cast<FightMatch*>(p);game.playContact({request.burst,ContactEffectKind::Hurt,request.origin,1,{}});game.effects_.buff(request);}});
}

void FightMatch::sourceComboEvent(unsigned target,SourceFighterEvent event){
    if(event!=SourceFighterEvent::Idle&&event!=SourceFighterEvent::HurtResume&&event!=SourceFighterEvent::Dead)return;
    if(const auto ui=sourceHits_.clearTarget(target);ui&&*ui>=1&&*ui<=2)sourceHitsUi_[*ui-1].hide();
}
void FightMatch::sourceComboHit(unsigned actor,unsigned target,const StrikeContact& contact){
    const auto count=sourceHits_.add(actor,target,actor+1);
    if(count>1)sourceHitsUi_[actor].show(count,actor==0);
    // Original actor IDs are UUID strings; GameLogic's getHitsObj(1) score
    // lookup therefore has no matching entry. Preserve its zero bonus.
    if(actor==0&&sourceArcadeScoring_)pointsThisStep_+=hitPoints(contact,classicScore_);
}

void FightMatch::reset(const StageAsset& stage, Point first, Point second, bool keepGauges) {
    sourceControl_={};sourceStage_=stage.geometry();
    sourceHitsUi_={};
    objects_.clear();sourceObjectKeys_.clear();sourceScene_.reset();sourceNextObject_=0;
    actors_[0].reset(first, 1, keepGauges);
    actors_[1].reset(second, -1, keepGauges);
    auto optical=effects_.opticalState();effects_={};
    if(opticalDefinition_==&stage.optical())effects_.opticalState()=std::move(optical);
    else{
        opticalDefinition_=&stage.optical();effects_.opticalState().initialize(stage.optical().children());
        if(!stage.optical().children().empty())opticalSprites_.reserve(ObjectLimit+2);
    }
    slowTicks_ = slowFrame_ = shade_ = assistSounds_ = pointsThisStep_ = rushSounds_ = ghostStepSounds_ = 0;
    camera_.reset(stage);
    focus_.reset();
    sourceCameraFocus_.reset();sourceCameraLast_={};
    counterSounds_ = 0;
    walkSounds_ = ghostFootSounds_ = 0;
    shadowSources_.clear();
    freeze_={};superKnockout_=false;combatSounds_.clear();
    effectAnimate_=false;
    if(sourcePhysics_)initializeSourceScene();
}

std::optional<unsigned> FightMatch::counterSide() const {
    for (unsigned side = 0; side < 2; ++side) if (actors_[side].counterPaused()) return side;
    return {};
}

std::optional<unsigned> FightMatch::cutinSide() const {
    for (unsigned i = 0; i < 2; ++i) {
        if (actors_[i].cutin().kind != CutinKind::None) return i;
    }
    return {};
}

const FightAttacker* FightMatch::assistCutin() const {
    for (const auto& object : objects_) {
        if (const auto* attacker = std::get_if<FightAttacker>(&object); attacker && attacker->active() && attacker->cutin()) {
            return attacker;
        }
    }
    return nullptr;
}

SceneEffects FightMatch::scene() const {
    auto view = effects_.scene();
    view.shadeAlpha = shade_;
    view.fillBackdrop = counterSide().has_value() || assistCutin() != nullptr || actors_[0].hidesMap() || actors_[1].hidesMap();
    if(!sourcePhysics_)view.hideMap = view.hideMap || view.fillBackdrop;
    view.backdropColor = actors_[actors_[0].hidesMap() ? 0 : 1].backdropColor();
    if (const auto side = cutinSide()) {
        const auto& actor = actors_[*side];
        const auto cutin = actor.cutin();
        if(!sourcePhysics_)view.hideMap = true;
        view.fillBackdrop = true;
        view.backdropColor = actor.backdropColor();
        if (cutin.asset && cutin.asset->has(cutin.face)) {
            view.portraitAsset = cutin.asset;
            view.portrait = &cutin.asset->frame(cutin.face, 0).pose;
            view.portraitFrame = cutin.frame;
            view.portraitRight = actor.motion().position.x > actors_[1 - *side].motion().position.x;
        }
    }
    return view;
}

bool FightMatch::roundClockTick(bool animate) const {
    if(effects_.controllerClocks())return !cinematic()&&!hitFreeze()&&effects_.nextActorAnimation(animate);
    return !cinematic()&&!hitFreeze() && (slowTicks_ > 1 ? slowFrame_ == 2 : animate);
}

bool FightMatch::hitFreeze() const {
    return freeze_.nextPaused(effectGameHz_,deliveryHz_);
}

void FightMatch::flashKnockout(unsigned loser) {
    // FightUI disables hit freezes, then adds hit_end and a separate 50% white shine.
    freeze_.disable();
    superKnockout_=std::ranges::any_of(actors_,[](const auto& actor){
        return actor.aiState()==AiState::Super||actor.aiState()==AiState::Ultimate;
    });
    if(const auto* effect=combatEffect("hit_end"))playContact({effect,ContactEffectKind::Hurt,
        actors_[loser].motion().position,1,{}});
    FighterEffects shine{};shine.addShine(0xffffff,50);
    shine.shakeRequested=true;shine.shakeX=5;shine.shakeTimeMs=1;effects_.play(shine);
    if(superKnockout_)if(const auto* effect=combatEffect("kobg"))effects_.background(*effect,2);
    if(!superKnockout_)effects_.blur(10,0,2000);
    effects_.blurEnabled(false);
}

bool FightMatch::capturesTarget(unsigned owner) const {
    return std::ranges::any_of(objects_, [owner](const FightObject& object) {
        const auto* attacker = std::get_if<FightAttacker>(&object);
        return attacker && attacker->owner() == owner && attacker->holdsTarget();
    });
}

bool FightMatch::hidesTarget(unsigned owner) const {
    return actors_[owner].hidesTarget() || capturesTarget(owner);
}

unsigned FightMatch::takeRushSounds() {
    return std::exchange(rushSounds_, 0);
}

unsigned FightMatch::takeGhostStepSounds() {
    return std::exchange(ghostStepSounds_, 0);
}

void FightMatch::play(FighterEffects requests) {
    if(sourcePhysics_)for(const auto& request:requests.controllerRequests)applyController(request);
    // EffectHitHandler applies the hit-specific slow request before doEffectVO.
    if (requests.slowTimeMs&&effects_.slow(1.5,requests.slowTimeMs)) {
        syncSourceSlow(1.5);
        slowTicks_ = requests.slowTimeMs * LogicHz / 1000;
        slowFrame_ = 0;
    }
    for(const auto& request:requests.contactEffects)playContact(request);
    counterSounds_ |= requests.counterSounds;
    walkSounds_ += requests.walkSounds;
    ghostFootSounds_ += requests.ghostFootSounds;
    if (requests.ground && requests.ground->sound) ++rushSounds_;
    ghostStepSounds_ += requests.ghostStepSound;
    effects_.play(requests);
}

void FightMatch::playContact(const ContactEffectRequest& request) {
    dispatchCombatEffect(effects_, request,
        [&](unsigned ms) { if(freeze_.request(ms,effectGameHz_)){
            effects_.freezeBlur(ms);if(sourcePhysics_)sourceControl_.pause();
        } },
        [&](double rate, unsigned ms) { if(effects_.slow(rate,ms)){syncSourceSlow(rate);slowTicks_ = ms * LogicHz / 1000; slowFrame_ = 0;} },
        [&](std::string_view name) { combatSounds_.push_back(name); });
}

void FightMatch::stepScene(bool animate) {
    if(freeze_.step(effectGameHz_,deliveryHz_)){
        effects_.freezeOver();if(sourcePhysics_)sourceControl_.resume();
    }
    const auto previousSlow=effects_.scene().blur.slowRate;
    effectAnimate_=effects_.effectAnimation(animate);
    // SlowHandler.render resumes global sprite/camera speed before selected
    // animation callbacks. A later endBisha cameraResume must remain last.
    if(sourcePhysics_&&previousSlow>0&&effects_.gameClocks()->clocks().slowRate()==0)syncSourceSlow(0);
    for(const auto& actor:actors_)actor.effectTarget();
    shadowSources_.clear();
    for (const auto& actor : actors_) if (actor.shadowing()) shadowSources_.push_back({actor.shadowLifetime().get(),
        &actor.asset(), &actor.pose(), actor.motion().position, actor.facing(), true, actor.shadowTint(), actor.shadowLifetime()});
    for (const auto& object : objects_) if (const auto* actor = std::get_if<FightAttacker>(&object); actor && actor->shadowing())
        shadowSources_.push_back({actor->shadowLifetime().get(), &actor->asset(), &actor->pose(), actor->position(), actor->facing(), true,
            actor->shadowTint(), actor->shadowLifetime()});
    if(sourcePhysics_)effects_.stepOrdered(effectAnimate_,shadowSources_,{this,
        [](void* self){static_cast<FightMatch*>(self)->stepSelectedAnimation();},
        [](void* self){static_cast<FightMatch*>(self)->stepSelectedRender();}});
    else effects_.step(effectAnimate_, shadowSources_);
    if(!sourcePhysics_&&previousSlow>0&&effects_.scene().blur.slowRate==0)syncSourceSlow(0);
    if (actors_[0].casting() || actors_[1].casting()) shade_ = 179;
    else shade_ -= std::min(shade_, 6U);
    if(sourcePhysics_){
        if(sourceCameraFocus_){updateSourceCameraPoint();camera_.track(sourceCameraLast_,sourceCameraLast_,true);}
        else if(focus_){const auto point=actors_[*focus_].motion().position;camera_.track(point,point,true);}
        else camera_.track(actors_[0].motion().position,actors_[1].motion().position);
        if(sourceHooks_.stage)sourceHooks_.stage(sourceHooks_.context);
        stepOptical();return;
    }
    auto focus = focus_;
    if (const auto cutin = cutinSide()) {
        const auto kind = actors_[*cutin].cutin().kind;
        if(kind==CutinKind::GhostStep){
            if(!sourcePhysics_){stepOptical();return;}
        }else if(kind!=CutinKind::Super)focus=cutin;
    }
    if (const auto* assist = assistCutin(); assist && assist->cutin() == 2) {
        camera_.track(assist->position(), assist->position(), true);
    } else if (focus) {
        const auto point = actors_[*focus].motion().position;
        camera_.track(point, point, true);
    } else {
        camera_.track(actors_[0].motion().position, actors_[1].motion().position);
    }
    if(sourcePhysics_&&sourceHooks_.stage)sourceHooks_.stage(sourceHooks_.context);
    stepOptical();
}

void FightMatch::applyController(const SourceControlRequest& request){
    const auto change=sourceControl_.apply(request);
    if(request.op==SourceControlOp::Bisha||request.op==SourceControlOp::SuperBisha||request.op==SourceControlOp::WanKai||request.op==SourceControlOp::Ghost||request.op==SourceControlOp::Replace||request.op==SourceControlOp::Explode)effects_.blackBack(true);
    else if(change.mapChanged&&change.mapVisible)effects_.blackBack(false);
    if(change.mapChanged)effects_.mapVisible(change.mapVisible);
    if(change.cameraFocus){sourceCameraFocus_=request.target;focus_.reset();updateSourceCameraPoint();camera_.focusOne();}
    if(change.cameraResume){sourceCameraFocus_.reset();focus_.reset();camera_.resumeFocus();}
    for(auto& actor:actors_)actor.syncSourceCounter(sourceControl_.counters());
}

void FightMatch::updateSourceCameraPoint(){
    for(const auto& actor:actors_)if(actor.effectTarget()==sourceCameraFocus_){sourceCameraLast_=actor.motion().position;return;}
    for(const auto& object:objects_)if(const auto* actor=std::get_if<FightAttacker>(&object);actor&&actor->controllerTarget()==sourceCameraFocus_){sourceCameraLast_=actor->position();return;}
}

void FightMatch::stepSelectedAnimation(){
    // The source iterates a mutable Vector forwards. Removing the current
    // target shifts its neighbour, which then skips this animation callback.
    for(unsigned i=0;i<sourceControl_.animations().size();++i){
        const auto target=sourceControl_.animations()[i];
        bool found=false;
        for(unsigned side=0;side<2;++side)if(actors_[side].effectTarget().get()==target.get()){
            std::array<std::uint32_t,ObjectLimit> names{};unsigned count=0;
            for(const auto& object:objects_)if(const auto* attacker=std::get_if<FightAttacker>(&object);
                attacker&&attacker->active()&&attacker->owner()==side&&attacker->key())names[count++]=attacker->key();
            actors_[side].stepSourceCutinAnimation(&actors_[1-side],true,sourceStage_,std::span(names).first(count));
            ++aiAnimations_[side];if(cpus_[side])cpus_[side]->update(*this,side);
            actors_[side].finishGhostAnimation(&actors_[1-side]);
            play(actors_[side].takeEffects());addObjects(actors_[side].takeSpawns(),side);
            found=true;break;
        }
        if(found)continue;
        for(unsigned object=0;object<objects_.size();++object){
            auto* attacker=std::get_if<FightAttacker>(&objects_[object]);
            if(!attacker||attacker->controllerTarget()!=target)continue;
            std::vector<ObjectSpawn> shots;
            advanceAttacker(*attacker,sourceStage_,true,camera_.fighterLimits(),effects_.motionRate(),shots);
            break;
        }
    }
}

void FightMatch::stepSelectedRender(){
    // Replace/explode timers belong to BlackBack.render, after FX animations.
    const auto counters=sourceControl_.renderCounters();
    FighterEffects requests;
    if(counters.replaceSmoke)requests.reactions.push_back({ReactionCue::ReplaceSmoke,counters.replaceOrigin});
    if(counters.explodeBurst){requests.reactions.push_back({ReactionCue::ExplodeBurst,counters.explodeOrigin});requests.counterSounds|=4;}
    play(std::move(requests));
    if(counters.change.mapChanged){effects_.mapVisible(counters.change.mapVisible);if(counters.change.mapVisible)effects_.blackBack(false);}
    for(auto& actor:actors_)actor.syncSourceCounter(sourceControl_.counters());
    for(unsigned i=0;i<sourceControl_.renders().size();++i){
        const auto target=sourceControl_.renders()[i];
        for(unsigned side=0;side<2;++side)if(actors_[side].effectTarget().get()==target.get()){
            auto& actor=actors_[side];const auto input=cpus_[side]?cpus_[side]->controls():sourceFrameControls_[side];
            std::array<std::uint32_t,ObjectLimit> names{};unsigned count=0;
            for(const auto& object:objects_)if(const auto* attacker=std::get_if<FightAttacker>(&object);
                attacker&&attacker->active()&&attacker->owner()==side&&attacker->key())names[count++]=attacker->key();
            actor.stepSourceCutinRender(sourceStage_,camera_.fighterLimits(),effects_.motionRate(),input,&actors_[1-side],std::span(names).first(count));
            play(actor.takeEffects());addObjects(actor.takeSpawns(),side);break;
        }
    }
}

void FightMatch::stepOptical(){
    if(!effects_.opticalState().view().active)return;
    opticalSprites_.clear();
    for(const auto& actor:actors_)opticalSprites_.push_back(opticalSprite(actor.opticalArea()));
    for(const auto& object:objects_){
        if(const auto* actor=std::get_if<FightAttacker>(&object))opticalSprites_.push_back(actor->opticalArea());
        else{const auto& bullet=std::get<FightProjectile>(object);opticalSprites_.push_back(opticalSprite(bullet.body(),!bullet.active()));}
    }
    effects_.opticalState().advance(camera_.view().origin,opticalSprites_);
}

void FightMatch::stopInput() {
    for (auto& actor : actors_) actor.clearInput();
}

void FightMatch::playAssistEffect(unsigned side, Point position) {
    const bool naruto = assisters_[side].faction == 1;
    const GroundCue cue = naruto ? GroundCue::AssistNaruto : GroundCue::AssistBleach;
    FighterEffects effects{};
    effects.ground = GroundCueView{cue, position};
    effects_.play(effects);
    assistSounds_ |= 1U << unsigned(naruto);
}

void FightMatch::addObjects(std::span<const ObjectSpawn> spawns, unsigned owner) {
    for (const auto& spawn : spawns) {
        if (spawn.op == FighterOp::RememberAttacker) {
            const auto key = spawn.asset->attacker(spawn.id).key;
            FightAttacker* selected = nullptr;
            for (auto& object : objects_) {
                auto* attacker = std::get_if<FightAttacker>(&object);
                if (!attacker || !attacker->active() || attacker->owner() != owner || (sourcePhysics_&&attacker->assister())) continue;
                attacker->clearReference(spawn.referenceSlot,owner);
                if (!selected && attacker->key() == key) selected = attacker;
            }
            if (selected) selected->setReference(spawn.referenceSlot,owner);
            continue;
        }
        if (spawn.op == FighterOp::ControlAttacker || spawn.op == FighterOp::ControlRemembered) {
            const auto& definition = spawn.asset->attacker(spawn.id);
            for (auto& object : objects_) {
                auto* attacker = std::get_if<FightAttacker>(&object);
                if (!attacker || !attacker->active() || attacker->owner() != owner || attacker->key() != definition.key || (sourcePhysics_&&attacker->assister())) continue;
                if (spawn.op == FighterOp::ControlRemembered && !attacker->referenced(spawn.referenceSlot,owner)) continue;
                attacker->applyControl(spawn);
                if(sourcePhysics_)syncSourceObjects();
                break;
            }
            continue;
        }
        if (objects_.size() >= ObjectLimit) continue;
        if (spawn.op == FighterOp::Fire) {
            objects_.emplace_back(std::in_place_type<FightProjectile>, *spawn.asset, owner, spawn);
        } else {
            objects_.emplace_back(std::in_place_type<FightAttacker>, *spawn.asset, owner, spawn,
                                  actors_[1 - owner].motion().position, nextObjectSeed());
        }
        if(sourcePhysics_){
            std::visit([&](auto& value){value.sourceHz(effectGameHz_);},objects_.back());
            const auto key=SourceScene::Object|sourceNextObject_++;sourceObjectKeys_.push_back(key);sourceScene_.add(key,owner,objects_.size()-1);
            if(auto* actor=std::get_if<FightAttacker>(&objects_.back()))bindSourcePaint(*actor,key);
        }
    }
}

std::uint32_t FightMatch::nextObjectSeed() {
    objectRandom_ ^= objectRandom_ << 13;
    objectRandom_ ^= objectRandom_ >> 17;
    objectRandom_ ^= objectRandom_ << 5;
    return objectRandom_;
}

void FightMatch::advanceAttacker(FightAttacker& attacker, const StageGeometry& stage, bool animate,
                                 std::optional<CameraLimits> limits, double rate, std::vector<ObjectSpawn>& shots) {
    if(sourcePhysics_)rate=effects_.motionRate();
    const unsigned owner = attacker.owner();
    const bool wasActive = attacker.active();
    play(attacker.step(stage, animate, limits, actors_[1 - owner], rate, shots, &actors_[owner], &actors_[owner]));
    if (wasActive && !attacker.active() && attacker.assister() && attacker.departureEffect()) {
        playAssistEffect(owner, attacker.position());
    }
    if(sourcePhysics_)syncSourceObjects();
    addObjects(shots, owner);
    shots.clear();
}

void FightMatch::step(const StageGeometry& stage, const std::array<FighterControls, 2>& controls, bool animate,
                      Contacts contacts) {
    stepFrame(stage, controls, animate, contacts);
    stepDetached(animate);
}

void FightMatch::stepDetached(bool animate) {
    // Detached ENTER_FRAME listeners follow the stage GameRender dispatch.
    for (auto& object : objects_) {
        if (auto* attacker = std::get_if<FightAttacker>(&object)) {
            const unsigned owner = attacker->owner();
            if (const auto control = attacker->applyFrameListeners(actors_[owner], actors_[1 - owner], &actors_[owner], sourcePhysics_?true:animate)) {
                addObjects(std::span(&*control, 1), owner);
            }
        }
    }
    // An assist or the other actor can renew a buff after its recipient's step.
    // Flush just these requests after all timeline listeners, in the same draw.
    for (auto& actor : actors_) for (const auto& request : actor.takeBuffEffects()) {
        playContact({request.burst, ContactEffectKind::Hurt, request.origin, 1, {}});
        effects_.buff(request);
    }
    if(sourcePhysics_){syncSourceObjects();pruneSourceObjects();}
}

void FightMatch::stepFrame(const StageGeometry& stage, const std::array<FighterControls, 2>& controls, bool animate,
                           Contacts contacts) {
    assistSounds_ = 0;
    pointsThisStep_ = 0;
    sourceFrameControls_=controls;
    if (!effects_.controllerClocks()&&slowTicks_ && --slowTicks_) {
        slowFrame_ = (slowFrame_ + 1) % 3;
        animate = slowFrame_ == 0;
    }
    stepScene(animate);
    const double rate = effects_.controllerClocks()?effects_.motionRate():(slowTicks_ ? 1.0 / 1.5 : 1.0);
    if(sourcePhysics_?!sourceControl_.running():freeze_.paused())return;
    const auto limits = camera_.fighterLimits();
    std::vector<ObjectSpawn> shots;
    if (const auto side = sourcePhysics_?std::optional<unsigned>{}:counterSide()) {
        const bool paused = actors_[*side].stepCounterPause();
        play(actors_[*side].takeEffects());
        if (paused) return;
    }
    if (!sourcePhysics_&&assistCutin()) {
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            if (auto* attacker = std::get_if<FightAttacker>(&objects_[i]); attacker && attacker->cutin() && attacker->active()) {
                advanceAttacker(*attacker, stage, effectAnimate_, limits, rate, shots);
            }
        }
        std::erase_if(objects_, [](const FightObject& object) {
            const auto* attacker = std::get_if<FightAttacker>(&object);
            return attacker && !attacker->active();
        });
        return;
    }
    std::array<std::uint32_t, ObjectLimit> attackerNames[2];
    unsigned nameCount[2]{};
    FightAttacker* summoned[2]{};
    for (auto& object : objects_) {
        if (auto* attacker = std::get_if<FightAttacker>(&object); attacker && attacker->active() && attacker->key()) {
            const auto owner = attacker->owner();
            attackerNames[owner][nameCount[owner]++] = attacker->key();
            if (attacker->assister()) summoned[owner] = attacker;
        }
    }
    const auto names = [&](unsigned side) { return std::span(attackerNames[side]).first(nameCount[side]); };
    if (const auto side = sourcePhysics_?std::optional<unsigned>{}:cutinSide()) {
        actors_[*side].stepCutin(&actors_[1 - *side], effectAnimate_, stage, limits, rate, names(*side));
        if (effectAnimate_) {
            ++aiAnimations_[*side];
            if (cpus_[*side]) cpus_[*side]->update(*this, *side);
            actors_[*side].finishGhostAnimation(&actors_[1 - *side]);
        }
        play(actors_[*side].takeEffects());
        return;
    }
    animate=effects_.actorAnimation(animate);
    if(sourcePhysics_&&sourceHooks_.beforeMain)sourceHooks_.beforeMain(sourceHooks_.context,animate);
    // GameCtrl finishes start/end/training/session work after MainLogic even
    // when a sprite paused the flat running flag during this callback.
    bool mainComplete=true;
    struct AfterMain {
        SourceFrameHooks hooks;bool animate;const bool& complete;
        ~AfterMain(){if(complete&&hooks.afterMain)hooks.afterMain(hooks.context,animate);}
    } after{sourcePhysics_?sourceHooks_:SourceFrameHooks{},animate,mainComplete};
    if(sourcePhysics_){mainComplete=stepSourceScene(stage,controls,animate,contacts,limits);return;}
    for (unsigned i = 0; i < 2; ++i) {
        const auto input = contacts != Contacts::Locked && cpus_[i] ? cpus_[i]->controls() : controls[i];
        const auto* assist = assisters_[i].asset;
        const bool call = input.assistHeld && assist && actors_[i].canCallAssist() &&
            (!sourcePhysics_||sourceActionsEnabled_)&&
            (summoned[i] || objects_.size() < ObjectLimit) && contacts != Contacts::Locked;
        if (call) {
            if (!assistHits_[i]) assistHits_[i] = makeRuntimeHitModel(*assist);
            const ObjectSpawn spawn{FighterOp::Summon, 0, actors_[i].motion().position, actors_[i].facing(), assist,
                1, {}, FinishClip, 0, false, assistHits_[i]};
            if (summoned[i]) {
                *summoned[i] = FightAttacker(*assist, i, spawn, {}, nextObjectSeed());
            } else {
                auto& object = objects_.emplace_back(std::in_place_type<FightAttacker>, *assist, i, spawn, Point{}, nextObjectSeed());
                summoned[i] = &std::get<FightAttacker>(object);
            }
            if(sourcePhysics_)summoned[i]->sourceHz(effectGameHz_);
            playAssistEffect(i, summoned[i]->position());
            actors_[i].spendAssist();
        }
        actors_[i].step(stage, input, animate, limits, &actors_[1 - i],
            sourcePhysics_?effects_.motionRate():rate, names(i), capturesTarget(1 - i));
        if (animate) {
            ++aiAnimations_[i];
            if (cpus_[i]) cpus_[i]->update(*this, i);
        }
        if(sourcePhysics_&&animate)actors_[i].finishGhostAnimation(&actors_[1-i]);
        if (call) actors_[i].recordAssist();
        if (animate) actors_[i].faceIdleTarget(actors_[1 - i]);
        play(actors_[i].takeEffects());
        addObjects(actors_[i].takeSpawns(), i);
        if (!sourcePhysics_&&actors_[i].counterPaused()) {
            stopInput();
            return;
        }
        if (!sourcePhysics_&&actors_[i].cutin().kind != CutinKind::None) {
            stopInput();
            return;
        }
    }
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        auto& object = objects_[i];
        if (auto* projectile = std::get_if<FightProjectile>(&object)) {
            if (projectile->step(stage, animate, sourcePhysics_?effects_.motionRate():rate)) effects_.play({2, 200, {}});
        } else {
            auto& attacker = std::get<FightAttacker>(object);
            // The Flash stage appends newly fired bullets to the same list it is updating.
            advanceAttacker(attacker, stage, animate, limits, rate, shots);
        }
    }
    std::erase_if(objects_, objectFinished);
    if ((!sourcePhysics_&&assistCutin())||(sourcePhysics_&&!sourceControl_.renderHit())) return;
    actors_[0].resolveBodyContact(actors_[1]);
    if (!animate || contacts == Contacts::Locked) return;
    const auto ended = [&] {
        if(sourcePhysics_)return false;
        return contacts == Contacts::Round && (actors_[0].health() <= 0 || actors_[1].health() <= 0);
    };

    // Determine both contacts before resolving either hit so simultaneous strikes trade.
    const auto first = actors_[0].strikeAgainst(actors_[1]);
    const auto second = actors_[1].strikeAgainst(actors_[0]);
    if (first) {
        const bool hurt=actors_[1].receiveStrike(*first,&actors_[0]);
        if(!sourcePhysics_&&hurt)pointsThisStep_+=hitPoints(*first,classicScore_);
        play(actors_[1].takeEffects());
        actors_[0].scoreStrike(*first,&actors_[1]);
        if (ended()) return;
    }
    if (second) {
        actors_[0].receiveStrike(*second, &actors_[1]);
        play(actors_[0].takeEffects());
        actors_[1].scoreStrike(*second,&actors_[0]);
        if (ended()) return;
    }
    // GameMainLogicCtrl compares each member of team one against team two in insertion order.
    for (auto& object : objects_) {
        if (ownerOf(object) == 1) std::visit([&](auto& member) { checkHit(member, actors_[0], actors_[1],classicScore_,sourcePhysics_,
            [&](FighterEffects effects){play(std::move(effects));}); }, object);
        if (ended()) return;
    }
    for (auto& object : objects_) {
        if (ownerOf(object) != 0) continue;
        pointsThisStep_ += std::visit([&](auto& member) { return checkHit(member, actors_[1], actors_[0],classicScore_,sourcePhysics_,
            [&](FighterEffects effects){play(std::move(effects));}); }, object);
        if (ended()) return;
        for (auto& enemy : objects_) {
            if (ownerOf(enemy) == 1) checkHit(object, enemy, actors_);
        }
    }
    for (auto& actor : actors_) play(actor.takeEffects());
}

} // namespace bvn
