// Adapted from 5DPLAY Game Studio's MusouCtrl and EnemyFighterAICtrl.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "musou_battle.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace bvn {
namespace {

double randomUnit(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return double(state) / 4294967296.0;
}

unsigned ownerOf(const FightObject& object) {
    return std::visit([](const auto& value) { return value.owner(); }, object);
}

bool inRange(const FighterMotion& actor, FighterCommand action, const FighterMotion& target) {
    if (!actor.hasAction(action)) return false;
    const auto range = worldBox(actor.asset().aiRange(action), actor.motion().position, actor.facing());
    const auto body = target.body();
    return !range.empty() && !body.empty() && range.overlaps(body);
}

} // namespace

unsigned MusouMember::levelUpExp(unsigned level) {
    unsigned result = 0;
    for (unsigned n = 1; n <= level; ++n) result += unsigned(3.8 * n * n - 1.2 * n + 48.6);
    return result;
}

bool MusouMember::earn(unsigned amount) {
    const unsigned cap = levelUpExp(level);
    experience = std::min(experience, std::numeric_limits<unsigned>::max() - amount) + amount;
    if (experience < cap) return false;
    if (level == 80) {
        experience = cap;
        return false;
    }
    experience -= cap;
    ++level;
    return true;
}

void MusouEnemyBrain::animate(const FighterMotion& actor,const FighterMotion& target,std::uint32_t& random,bool source) {
    const auto here=actor.motion().position,there=target.motion().position;
    if (followTimer_-- <= 0) {
        followTimer_ = 2 * AnimationHz;
        following_ = randomUnit(random) < 0.5;
        if (following_) distance_ = randomUnit(random) > 0.6 ? unsigned(10 + randomUnit(random) * 20)
                                                           : unsigned(50 + randomUnit(random) * 100);
    }
    if (following_) {
        const double aim = there.x + (here.x > there.x ? double(distance_) : -double(distance_));
        direction_ = here.x < aim - 20 ? 1 : here.x > aim + 20 ? -1 : 0;
        jump_ = std::abs(here.y - there.y) < 30 ? 0 : here.y > there.y ? 1 : -1;
        if (!direction_ && !jump_) following_ = false;
    }
    const auto state=actor.aiState();
    const bool attacking=source?(state==AiState::Attack||state==AiState::Skill||state==AiState::Super||state==AiState::Ultimate):actor.acting();
    if (attacking) {
        attack_ = randomUnit(random) < 0.05;
        special_ = false;
    } else if (attackTimer_++ >= 3 * AnimationHz) {
        attackTimer_ = 0;
        attack_ = randomUnit(random) < 0.2;
        special_ = randomUnit(random) < 0.1;
    }
}

FighterControls MusouEnemyBrain::step(const FighterMotion& actor,const FighterMotion& target,
                                      bool animateFrame,std::uint32_t& random) {
    if(animateFrame)animate(actor,target,random);
    FighterControls controls{.horizontal=direction_};
    if (attack_ && inRange(actor, FighterCommand::Attack, target)) controls.attackPressed = true;
    else if (special_) {
        for (const auto command : {FighterCommand::SkillDown, FighterCommand::SkillUp, FighterCommand::Special,
                                   FighterCommand::SpecialDown, FighterCommand::SpecialUp}) {
            if (!inRange(actor, command, target)) continue;
            controls.attackPressed = command == FighterCommand::SkillDown || command == FighterCommand::SkillUp;
            controls.specialPressed = !controls.attackPressed;
            controls.vertical = command == FighterCommand::SkillDown || command == FighterCommand::SpecialDown ? -1 :
                                command == FighterCommand::SkillUp || command == FighterCommand::SpecialUp ? 1 : 0;
            return controls;
        }
    }
    if (!controls.attackPressed && jump_) {
        controls.jumpPressed = true;
        controls.vertical = jump_ < 0 ? -1 : 0;
    }
    return controls;
}

FighterControls MusouEnemyBrain::sourceControls(const FighterMotion& actor,const FighterMotion& target)const{
    FighterControls input{.horizontal=direction_,.aiControlled=true};
    const auto add=[&](AiIntent flag,bool value){if(value)input.aiIntents|=1U<<unsigned(flag);};
    add(AiIntent::Left,direction_<0);add(AiIntent::Right,direction_>0);
    add(AiIntent::Jump,jump_>0);add(AiIntent::JumpDown,jump_<0);
    add(AiIntent::Attack,attack_&&inRange(actor,FighterCommand::Attack,target));
    add(AiIntent::AirAttack,attack_&&inRange(actor,FighterCommand::AirAttack,target));
    for(const auto [flag,command]:{std::pair{AiIntent::SkillDown,FighterCommand::SkillDown},
        {AiIntent::SkillUp,FighterCommand::SkillUp},{AiIntent::Special,FighterCommand::Special},
        {AiIntent::SpecialDown,FighterCommand::SpecialDown},{AiIntent::SpecialUp,FighterCommand::SpecialUp},
        {AiIntent::AirSpecial,FighterCommand::AirSpecial}})add(flag,special_&&inRange(actor,command,target));
    return input;
}

bool MusouBattle::start(const MusouCampaign& campaign, unsigned mission, const StageAsset& stage,
                        const std::array<MusouFighterBank, 3>& team, std::span<const MusouFighterBank> opponents,
                        MusouProgress& progress, std::array<MusouMember, 3>& members, std::uint32_t seed) {
    if (mission >= campaign.missions().size() || !stage.spawn(0)) return false;
    const auto valid = [](const MusouFighterBank& bank) {
        return bank.firstForm < bank.forms.size() && bank.forms[bank.firstForm].has(MoveClip::Idle);
    };
    if (!std::ranges::all_of(team, valid)) return false;
    const auto& entry = campaign.missions()[mission];
    std::vector<const MusouFighterBank*> banks(campaign.enemies().size());
    for (const auto& wave : campaign.waves().subspan(entry.firstWave, entry.waveCount)) {
        for (unsigned i = wave.firstEnemy; i < unsigned(wave.firstRepeat) + wave.repeatCount; ++i) {
            const auto& enemy = campaign.enemies()[i];
            const auto found = std::ranges::find(opponents, std::string_view(enemy.fighter), &MusouFighterBank::name);
            if (found == opponents.end() || !valid(*found)) return false;
            banks[i] = &*found;
        }
    }
    *this = MusouBattle{};
    campaign_ = &campaign;
    stage_ = &stage;
    effects_.opticalState().initialize(stage.optical().children());
    if(!stage.optical().children().empty())opticalSprites_.reserve(FightMatch::ObjectLimit+65);
    progress_ = &progress;
    members_ = &members;
    banks_ = std::move(banks);
    random_ = seed ? seed : 1;
    enemyLevel_ = entry.enemyLevel;
    objects_.reserve(FightMatch::ObjectLimit);
    party_.reserve(team.size());
    for (const auto& bank : team) party_.emplace_back(bank.forms, bank.firstForm);
    for (unsigned i = 0; i < party_.size(); ++i) {
        party_[i].setFaction(team[i].faction);
        updateMember(i, false);
        party_[i].reset(*stage.spawn(0), 1);
        party_[i].setQi(party_[i].maxQi());
    }
    camera_.reset(stage);
    return wave_.start(campaign, mission);
}

void MusouBattle::updateMember(unsigned slot, bool refill) {
    const auto level = (*members_)[slot].level;
    auto& actor = party_[slot];
    actor.setRoundStats(1000 + 150 * level, 1);
    actor.setGaugeLimits(std::min(300U, 100 + 20 * level), 50 + 10 * level);
    if (refill) {
        actor.setHealth(actor.maxHealth());
        actor.setQi(actor.maxQi());
        actor.setEnergy(actor.maxEnergy());
    }
}

void MusouBattle::changeMember() {
    if (switchTicks_ || !player().alive()) return;
    for (unsigned offset = 1; offset < party_.size(); ++offset) {
        const unsigned next = (leader_ + offset) % party_.size();
        if (!party_[next].alive()) continue;
        party_[next].place(player().motion().position);
        party_[next].setFacing(player().facing());
        party_[next].setEnergy(party_[next].maxEnergy());
        party_[next].clearInput();
        player().clearInput();
        if(sourcePhysics_)sourceScene_.remove(leader_,false);
        leader_ = next;
        if(sourcePhysics_)sourceScene_.add(leader_,0);
        target_ = 0;
        if(sourcePhysics_)resetSourceCamera(true);
        switchTicks_ = AnimationHz;
        if (const auto* effect = combatEffect("team_change"))
            playContact({effect, ContactEffectKind::Hurt, player().motion().position, 1, {}});
        return;
    }
}

void MusouBattle::recoverMembers() {
    if (healTicks_) {
        --healTicks_;
        return;
    }
    healTicks_ = AnimationHz;
    for (unsigned i = 0; i < party_.size(); ++i) {
        auto& actor = party_[i];
        if (i == leader_ || !actor.alive()) continue;
        actor.setHealth(actor.health() + actor.maxHealth() * 0.01);
        actor.setQi(actor.qi() + actor.maxQi() * 0.02);
        actor.setEnergy(actor.energy() + actor.maxEnergy() * 0.08);
    }
}

void MusouBattle::populateStates() {
    states_.clear();
    for (const auto& enemy : enemies_) states_.push_back({enemy.spawn, enemy.motion.alive(), !enemy.fade});
}

void MusouBattle::spawn(MusouSpawn spawn) {
    const auto& definition = campaign_->enemies()[spawn.enemy];
    const auto& bank = *banks_[spawn.enemy];
    auto& entry = enemies_.emplace_back(MusouOpponent{nextId_++, spawn, FighterMotion(bank.forms, bank.firstForm),
                                                       {}, definition.boss(), 0, &bank, {}, false});
    const auto position = player().motion().position;
    const double dx = 100 + randomUnit(random_) * 100;
    const double dy = 50 + randomUnit(random_) * 50;
    const int side = randomUnit(random_) > 0.5 ? 1 : -1;
    const int up = randomUnit(random_) > 0.5 ? 1 : -1;
    const double hp = definition.hp ? definition.hp :
        bank.baseHp * (1 + (enemyLevel_ - 1) * 0.08);
    entry.motion.setRoundStats(hp, 1);
    entry.motion.setGameHz(gameHz_);if(sourcePhysics_){entry.motion.sourceHz(gameHz_);entry.motion.orderedSourceControl();}
    entry.motion.setFaction(bank.faction);
    entry.motion.reset({position.x + side * dx, position.y + up * dy}, -side);
    entry.motion.setMusouEnemyEffects(!entry.boss);entry.motion.hurtRandomState(random_);if(sourcePhysics_)bindSourceHook(entry.motion,entry.id);
    if(sourcePhysics_)sourceScene_.add(entry.id,1);
    if (entry.boss) {
        if(sourcePhysics_)entry.bossAI.emplace(random_^(entry.id*0x9e3779b9u),2,gameHz_);
        else entry.bossAI.emplace(random_ ^ (entry.id * 0x9e3779b9u));
        bossCue_ = {MusouBossCue::Kind::Entrance, entry.id};
        target_ = entry.id;
        player().clearInput();
        if(sourcePhysics_){sourcePresentation_.entrance();sourceCameraFocus_=entry.motion.effectTarget();updateSourceCameraPoint();camera_.focusOne();}
    }
    FighterEffects effect{};
    // enemyBirthEffect chooses the comic's assist cache and explicitly suppresses sound.
    effect.ground = GroundCueView{bank.faction == 0 ? GroundCue::AssistBleach : GroundCue::AssistNaruto,
                                 entry.motion.motion().position, 0, -side};
    play(effect);
}

FighterMotion* MusouBattle::owner(unsigned id) {
    if (id < party_.size()) return &party_[id];
    const auto entry = std::ranges::find(enemies_, id, &MusouOpponent::id);
    if(entry!=enemies_.end())return &entry->motion;
    if(sourcePhysics_){const auto retained=std::ranges::find(retainedEnemies_,id,&MusouOpponent::id);
        if(retained!=retainedEnemies_.end())return &retained->motion;}
    return nullptr;
}

MusouOpponent* MusouBattle::chooseTarget() {
    MusouOpponent* selected = nullptr;
    unsigned order = std::numeric_limits<unsigned>::max();
    for (auto& enemy : enemies_) {
        if (!enemy.motion.alive()) continue;
        if (enemy.id == target_) return &enemy;
        const unsigned rank = enemy.motion.body().empty() ? 5 : enemy.boss ? 0 : 1;
        if (rank < order) {
            order = rank;
            selected = &enemy;
        }
    }
    target_ = selected ? selected->id : 0;
    return selected;
}

void MusouBattle::setSourceProfile(unsigned hz,unsigned shineHz){
    if(!sourcePhysics_)initializeSourceScene();
    hz=hz==30?30:60;sourcePhysics_=true;deliveryHz_=hz;setGameHz(hz,shineHz);
    effects_.deliveryHz(hz);effects_.enableGameClocks(hz);delivery_.initialize(hz);
    sourceControl_.initialize(hz);sourceCameraFocus_.reset();sourceCameraLast_={};
    sourceUiPhase_.rate(hz,30,true);sourcePresentation_.initialize(hz);
    sourceCallbacks_={};sourceCallbacks_.add(SourceRenderCallbacks::Group::Main,1);
    sourceTransition_={};sourceReturnClosing_=sourceLeaveReady_=false;
    wave_.sourceResultEvents();sourceResultRevision_=sourceReturnSerial_=0;
    for(auto& actor:party_){actor.allowLoseHp(false);actor.sourceActionEnable(false);}
    for(unsigned i=0;i<party_.size();++i){auto& actor=party_[i];actor.sourceHz(hz);actor.orderedSourceControl();actor.hurtRandomState(random_);bindSourceHook(actor,i);}
    for(auto& enemy:enemies_){enemy.motion.sourceHz(hz);enemy.motion.orderedSourceControl();enemy.motion.hurtRandomState(random_);bindSourceHook(enemy.motion,enemy.id);}
    for(auto& object:objects_)std::visit([&](auto& value){value.sourceHz(hz);},object);
    camera_.sourceHz(hz);camera_.reset(*stage_);resetSourceCamera(true);
}

void MusouBattle::syncSourceSlow(double rate){
    if(!sourcePhysics_)return;
    const double speed=rate>0?1/rate:1;camera_.slow(rate);
    for(auto& actor:party_)actor.speedRate(speed);
    for(auto& enemy:enemies_)enemy.motion.speedRate(speed);
    for(auto& enemy:retainedEnemies_)enemy.motion.speedRate(speed);
    for(auto& object:objects_)if(auto* actor=std::get_if<FightAttacker>(&object))actor->speedRate(speed);
}

void MusouBattle::play(FighterEffects effect) {
    if(sourcePhysics_)for(const auto& request:effect.controllerRequests)applyController(request);
    if (effect.slowTimeMs&&effects_.slow(1.5,effect.slowTimeMs)) {
        syncSourceSlow(1.5);
        slowTicks_ = effect.slowTimeMs * LogicHz / 1000;
        slowPeriod_ = 3;
    }
    for (const auto& request : effect.contactEffects) playContact(request);
    counterSounds_ |= effect.counterSounds;
    walkSounds_ += effect.walkSounds;
    ghostFootSounds_ += effect.ghostFootSounds;
    if (effect.ground && effect.ground->sound) ++rushSounds_;
    ghostStepSounds_ += effect.ghostStepSound;
    effects_.play(effect);
}

void MusouBattle::playContact(const ContactEffectRequest& request) {
    dispatchCombatEffect(effects_, request,
        [&](unsigned ms) { if(freeze_.request(ms,gameHz_)){
            effects_.freezeBlur(ms);if(sourcePhysics_)sourceControl_.pause();
        } },
        [&](double rate, unsigned ms) {
            if(effects_.slow(rate,ms)){
                syncSourceSlow(rate);
                slowTicks_ = ms * LogicHz / 1000;
                slowPeriod_ = std::max(1U, unsigned(std::ceil(rate * LogicHz / AnimationHz)));
            }
        },
        [&](std::string_view name) { combatSounds_.push_back(name); });
}

unsigned MusouBattle::takeRushSounds() {
    return std::exchange(rushSounds_, 0);
}

unsigned MusouBattle::takeGhostStepSounds() {
    return std::exchange(ghostStepSounds_, 0);
}

void MusouBattle::addObjects(std::span<const ObjectSpawn> spawns, unsigned id, Point target) {
    if(sourcePhysics_)syncSourceObjects();
    for (const auto& spawn : spawns) {
        if (spawn.op == FighterOp::RememberAttacker || spawn.op == FighterOp::ControlAttacker ||
            spawn.op == FighterOp::ControlRemembered) {
            const auto& definition = spawn.asset->attacker(spawn.id);
            FightAttacker* remembered = nullptr;
            for (auto& value : objects_) {
                auto* attacker = std::get_if<FightAttacker>(&value);
                if (!attacker || !attacker->active()) continue;
                if(sourcePhysics_){
                    if(attacker->assister()||(attacker->owner()<party_.size())!=(id<party_.size()))continue;
                }else if(attacker->owner()!=id)continue;
                if (spawn.op == FighterOp::RememberAttacker) attacker->clearReference(spawn.referenceSlot,id);
                if (attacker->key() != definition.key) continue;
                if (spawn.op == FighterOp::RememberAttacker) {
                    if (!remembered) remembered = attacker;
                    continue;
                }
                if (spawn.op == FighterOp::ControlRemembered && !attacker->referenced(spawn.referenceSlot,id)) continue;
                attacker->applyControl(spawn);
                if(sourcePhysics_)syncSourceObjects();
                break;
            }
            if (remembered) remembered->setReference(spawn.referenceSlot,id);
        } else if (objects_.size() < FightMatch::ObjectLimit) {
            if (spawn.op == FighterOp::Fire) objects_.emplace_back(std::in_place_type<FightProjectile>, *spawn.asset, id, spawn);
            else objects_.emplace_back(std::in_place_type<FightAttacker>, *spawn.asset, id, spawn, target);
            if(sourcePhysics_){
                std::visit([&](auto& value){value.sourceHz(gameHz_);},objects_.back());
                const auto key=SourceScene::Object|sourceNextObject_++;sourceObjectKeys_.push_back(key);sourceScene_.add(key,id<party_.size()?0:1,objects_.size()-1);
                if(auto* actor=std::get_if<FightAttacker>(&objects_.back()))bindSourcePaint(*actor,key);
            }
        }
    }
}

void MusouBattle::collectSpawns(FighterMotion& actor, unsigned id, Point target) {
    addObjects(actor.takeSpawns(), id, target);
    play(actor.takeEffects());
}

void MusouBattle::advanceObjects(bool animate, double rate, FighterMotion* primary) {
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        if(sourcePhysics_)rate=effects_.motionRate();
        auto& value = objects_[i];
        if (auto* projectile = std::get_if<FightProjectile>(&value)) {
            if (projectile->step(stage_->geometry(), animate, rate)) effects_.play({2, 200, {}});
        } else {
            auto& attacker = std::get<FightAttacker>(value);
            const unsigned id = attacker.owner();
            auto* target = id < party_.size() ? primary : &player();
            auto* source = owner(id);
            if (!target || !source) continue;
            play(attacker.step(stage_->geometry(), animate, std::nullopt, *target, rate, spawnedObjects_, source,
                               id < party_.size() ? source : primary));
            addObjects(spawnedObjects_, id, target->motion().position);
            spawnedObjects_.clear();
        }
    }
    std::erase_if(objects_, objectFinished);
}

StrikeContact MusouBattle::powered(StrikeContact contact, unsigned id) const {
    const bool playerTeam = id < party_.size();
    const unsigned level = playerTeam ? (*members_)[id].level : enemyLevel_;
    const unsigned bonus = contact.strike.bisha() ? (playerTeam ? 17 : 20) :
                           contact.strike.skill() ? (playerTeam ? 15 : 10) : (playerTeam ? 10 : 5);
    contact.customPower = contact.power() + bonus * level;
    return contact;
}

void MusouBattle::contacts(bool hits) {
    if(sourcePhysics_&&!sourceControl_.renderHit())return;
    for (auto& enemy : enemies_) {
        if (!enemy.motion.alive() || !player().alive()) continue;
        player().resolveBodyContact(enemy.motion);
        if(!hits)continue;
        const auto hit = player().strikeAgainst(enemy.motion);
        const auto counter = enemy.motion.strikeAgainst(player());
        if (hit) {
            const auto strike = powered(*hit, leader_);
            enemy.motion.receiveStrike(strike, &player());
            play(enemy.motion.takeEffects());
            player().scoreStrike(strike,&enemy.motion);
        }
        if (counter) {
            const auto strike = powered(*counter, enemy.id);
            player().receiveStrike(strike, &enemy.motion);
            play(player().takeEffects());
            enemy.motion.scoreStrike(strike,&player());
        }
    }
}

void MusouBattle::contactObjects() {
    if(sourcePhysics_&&!sourceControl_.renderHit())return;
    for (auto& value : objects_) {
        std::visit([&](auto& attacker) {
            using T = std::decay_t<decltype(attacker)>;
            const unsigned id = attacker.owner();
            auto* source = owner(id);
            if (!source) return;
            const auto contact = [&](FighterMotion& target) {
                if (!attacker.active() || !target.alive() ||
                    !inStrikeRange(attacker.position(),target.motion().position)) return;
                if (const auto hit = attacker.strikeAgainst(target.motion().position, target.hurtBody())) {
                    auto strike = powered(*hit, id);
                    if constexpr (std::is_same_v<T,FightAttacker>)strike.recoil={&attacker,[](void* p,double x){static_cast<FightAttacker*>(p)->sourceRecoil(x);}};
                    target.receiveStrike(strike, source);
                    play(target.takeEffects());
                    if constexpr (std::is_same_v<T, FightProjectile>) attacker.hitFighter(target.motion().touchingSide);
                    if constexpr(std::is_same_v<T,FightProjectile>){
                        if(!sourcePhysics_||attacker.sourceMainOwner())source->scoreObjectStrike(strike,.1,&target);
                    }else source->scoreObjectStrike(strike,attacker.asset().assister()?.15:.13,&target);
                }
                if constexpr (std::is_same_v<T, FightProjectile>) {
                    if (const auto strike = target.strikeAgainst(attacker.position(), attacker.body()))
                        attacker.receiveHit(strike->damage());
                }
            };
            if (id < party_.size()) {
                for (auto& enemy : enemies_) contact(enemy.motion);
            } else contact(player());
        }, value);
    }
}

void MusouBattle::reward(const MusouOpponent& enemy) {
    ++kills_;
    const auto& definition = campaign_->enemies()[enemy.spawn.enemy];
    const unsigned experience = definition.experience + enemyLevel_ * (enemy.boss ? 10 : 2);
    progress_->earn(definition.money + enemyLevel_ * (enemy.boss ? 5 : 1));
    for (unsigned i = 0; i < party_.size(); ++i) {
        if ((*members_)[i].earn(experience * (i ? 1 : 2)) && i == leader_) updateMember(i, true);
    }
}

void MusouBattle::updateEnemies() {
    for (auto entry = enemies_.begin(); entry != enemies_.end();) {
        if (entry->boss && entry->motion.deathComplete()) {
            const auto id = entry->id;
            reward(*entry);
            entry = retireEnemy(entry);
            if(!sourcePhysics_)std::erase_if(objects_, [id](const auto& value) { return ownerOf(value) == id; });
            if(sourcePhysics_)resetSourceCamera(true);
            populateStates();
            wave_.enemyRemoved(states_, true);
            if(sourcePhysics_)syncSourceResult();
            if (wave_.result() == MusouResult::Won) {
                for (auto& enemy : enemies_) enemy.motion.die();
                return;
            }
            continue;
        }
        if (!entry->fade) {
            if (entry->motion.deathComplete()) {
                reward(*entry);
                entry->fade = 21;
            }
            ++entry;
        } else ++entry;
    }
}

void MusouBattle::fadeEnemies() {
    // EffectCtrl owns the 0.05 alpha fade, so it continues during a hit freeze.
    for (auto entry = enemies_.begin(); entry != enemies_.end();) {
        const bool complete=sourcePhysics_?entry->sourceFade.render(entry->motion):(entry->fade&&!--entry->fade);
        if (!complete) ++entry;
        else {
            const auto id = entry->id;
            entry = retireEnemy(entry);
            if(!sourcePhysics_)std::erase_if(objects_, [id](const auto& value) { return ownerOf(value) == id; });
            populateStates();
            wave_.enemyRemoved(states_);
            if(sourcePhysics_)syncSourceResult();
        }
    }
}

const MusouOpponent* MusouBattle::boss() const {
    if (bossCue_.actor) {
        const auto entry = std::ranges::find(enemies_, bossCue_.actor, &MusouOpponent::id);
        if (entry != enemies_.end()) return &*entry;
    }
    const auto entry = std::ranges::find_if(enemies_, [](const auto& enemy) { return enemy.boss&&!enemy.deadHandled; });
    return entry == enemies_.end() ? nullptr : &*entry;
}

void MusouBattle::stepBossCue(bool animate) {
    auto* actor = owner(bossCue_.actor);
    if (!actor) {
        if (bossCue_.kind == MusouBossCue::Kind::Knockout) freeze_.resume();
        bossCue_ = {};
        return;
    }
    const auto focus = actor->motion().position;
    camera_.track(focus, focus, true);
    ++bossCue_.ticks;
    if (bossCue_.kind == MusouBossCue::Kind::Entrance) {
        const auto geometry = stage_->geometry();
        const double rate=sourcePhysics_?effects_.motionRate():1;
        player().step(geometry, {}, animate,std::nullopt,nullptr,rate);
        for (auto& enemy : enemies_) enemy.motion.step(geometry, {}, animate, std::nullopt, &player(),rate);
        if (!bossCue_.spoke && !actor->motion().airborne) {
            actor->present(FighterCommand::Intro);
            bossCue_.spoke = true;
        }
        if (bossCue_.ticks < MusouBossCue::entranceTicks(deliveryHz_) || !bossCue_.spoke ||
            actor->performing(FighterCommand::Intro)) return;
    } else if (bossCue_.ticks < MusouBossCue::knockoutTicks(deliveryHz_)) {
        return;
    }
    actor->clearInput();
    player().clearInput();
    if (bossCue_.kind == MusouBossCue::Kind::Knockout) freeze_.resume();
    bossCue_ = {};
}

bool MusouBattle::beginBossKo() {
    const auto entry = std::ranges::find_if(enemies_, [this](const auto& enemy) {
        return enemy.boss && !enemy.bossKoShown && (sourcePhysics_?enemy.motion.dieEvents()>0:!enemy.motion.alive());
    });
    if (entry == enemies_.end()) return false;
    entry->bossKoShown = true;
    bossCue_ = {MusouBossCue::Kind::Knockout, entry->id};
    bossCue_.super=player().aiState()==AiState::Super||player().aiState()==AiState::Ultimate;
    if (const auto* effect = combatEffect("hit_end"))
        playContact({effect, ContactEffectKind::Hurt, entry->motion.motion().position, 1, {}});
    freeze_.disable();
    if(sourcePhysics_){sourceControl_.pause();sourcePresentation_.knockout();sourceCameraFocus_=entry->motion.effectTarget();updateSourceCameraPoint();camera_.focusOne();}
    FighterEffects end{};
    end.addShine(0xffffff, 50);
    end.shakeRequested = true;
    end.shakeX = 5;
    // The original third argument is an int: shake(5, 0, 1.5) becomes 1 ms.
    end.shakeTimeMs = 1;
    effects_.play(end);
    if(bossCue_.super)if(const auto* effect=combatEffect("kobg"))effects_.background(*effect,2);
    effects_.slow(2,5000);syncSourceSlow(2);
    slowTicks_ = 5 * LogicHz;
    slowPeriod_ = 4;
    return true;
}

bool MusouBattle::hidesEnemy(unsigned id) const {
    return id == target_ && (player().hidesTarget() || std::ranges::any_of(objects_, [&](const auto& value) {
        const auto* attacker = std::get_if<FightAttacker>(&value);
        return attacker && attacker->owner() == leader_ && attacker->holdsTarget();
    }));
}

bool MusouBattle::hidesPlayer() const {
    return std::ranges::any_of(enemies_, [](const auto& enemy) { return enemy.motion.hidesTarget(); }) ||
        std::ranges::any_of(objects_, [&](const auto& value) {
            const auto* attacker = std::get_if<FightAttacker>(&value);
            return attacker && attacker->owner() >= party_.size() && attacker->holdsTarget();
        });
}

SceneEffects MusouBattle::scene() const {
    auto view = effects_.scene();
    const auto* leader = &player();
    const auto* caster = leader;
    if (!leader->hidesMap() && leader->cutin().kind == CutinKind::None) {
        const auto found = std::ranges::find_if(enemies_, [](const auto& enemy) {
            return enemy.motion.hidesMap() || enemy.motion.cutin().kind != CutinKind::None || enemy.motion.casting();
        });
        if (found != enemies_.end()) caster = &found->motion;
    }
    const auto cutin = caster->cutin();
    view.fillBackdrop = caster->hidesMap() || cutin.kind != CutinKind::None || leader->counterPaused() ||
        std::ranges::any_of(enemies_, [](const auto& enemy) { return enemy.motion.counterPaused(); });
    if(!sourcePhysics_)view.hideMap = view.hideMap || view.fillBackdrop;
    view.backdropColor = caster->backdropColor();
    view.shadeAlpha = leader->casting() || caster->casting() ? 179 : 0;
    if (cutin.asset && cutin.asset->has(cutin.face)) {
        view.portraitAsset = cutin.asset;
        view.portrait = &cutin.asset->frame(cutin.face, 0).pose;
        view.portraitFrame = cutin.frame;
        view.portraitRight = caster != leader && caster->motion().position.x > leader->motion().position.x;
    }
    return view;
}

void MusouBattle::stepOptical(){
    if(!effects_.opticalState().view().active)return;
    opticalSprites_.clear();opticalSprites_.push_back(opticalSprite(player().opticalArea()));
    for(const auto& enemy:enemies_)opticalSprites_.push_back(opticalSprite(enemy.motion.opticalArea()));
    for(const auto& object:objects_){
        if(const auto* actor=std::get_if<FightAttacker>(&object))opticalSprites_.push_back(actor->opticalArea());
        else{const auto& bullet=std::get<FightProjectile>(object);opticalSprites_.push_back(opticalSprite(bullet.body(),!bullet.active()));}
    }
    effects_.opticalState().advance(camera_.view().origin,opticalSprites_);
}

void MusouBattle::stepFrame(FighterControls controls, bool switchPressed, bool& animate) {
    if(sourcePhysics_){stepOrderedFrame(controls,switchPressed,animate);return;}
    ++ticks_;
    animate = ticks_ % 2 == 0;
    const double suppliedRate=slowTicks_?2.0/slowPeriod_:1.0;
    if (!effects_.controllerClocks()&&slowTicks_) {
        --slowTicks_;
        animate = ticks_ % slowPeriod_ == 0;
    }
    if (freeze_.step(gameHz_,deliveryHz_)) effects_.freezeOver();
    const double previousSlow=effects_.scene().blur.slowRate;
    const bool effectAnimate=effects_.effectAnimation(animate);
    for (const auto& actor : party_) actor.effectTarget();
    for (const auto& enemy : enemies_) enemy.motion.effectTarget();
    shadowSources_.clear();
    const auto addShadow = [&](const FighterMotion& actor) {
        if (actor.shadowing()) shadowSources_.push_back({actor.shadowLifetime().get(), &actor.asset(), &actor.pose(),
            actor.motion().position, actor.facing(), true, actor.shadowTint(), actor.shadowLifetime()});
    };
    addShadow(player());
    for (const auto& enemy : enemies_) addShadow(enemy.motion);
    for (const auto& object : objects_) if (const auto* actor = std::get_if<FightAttacker>(&object); actor && actor->shadowing())
        shadowSources_.push_back({actor->shadowLifetime().get(), &actor->asset(), &actor->pose(), actor->position(), actor->facing(), true,
            actor->shadowTint(), actor->shadowLifetime()});
    effects_.step(effectAnimate, shadowSources_);
    if(previousSlow>0&&effects_.scene().blur.slowRate==0)syncSourceSlow(0);
    const double rate=effects_.controllerClocks()?effects_.motionRate():suppliedRate;
    if (effectAnimate) fadeEnemies();
    if (wave_.result() != MusouResult::Playing) {stepOptical();return;}
    if (freeze_.paused()) {stepOptical();return;}
    FighterMotion* counter = player().counterPaused() ? &player() : nullptr;
    if (!counter) for (auto& enemy : enemies_) if (enemy.motion.counterPaused()) { counter = &enemy.motion; break; }
    if (counter) {
        const bool paused = counter->stepCounterPause();
        play(counter->takeEffects());
        if (paused) {stepOptical();return;}
    }
    if (bossCue_.kind != MusouBossCue::Kind::None) {
        animate=bossCue_.kind==MusouBossCue::Kind::Entrance?effects_.actorAnimation(animate):effectAnimate;
        stepBossCue(animate);
        stepOptical();
        return;
    }
    if (selfDieShown_) {
        const auto point = player().motion().position;
        camera_.track(point, point, true);
    } else if (const auto* current = boss()) {
        const auto point = current->motion.motion().position;
        if (current->bossKoShown) camera_.track(point, point, true);
        else camera_.track(player().motion().position, point);
    } else camera_.trackSolo(player().motion().position);
    stepOptical();
    if (!player().alive()) {
        animate=effects_.actorAnimation(animate);
        player().step(stage_->geometry(), {}, animate,std::nullopt,nullptr,sourcePhysics_?effects_.motionRate():1);
        if (player().deathComplete()) wave_.playerDefeated();
        return;
    }
    if (switchPressed && player().cutin().kind == CutinKind::None) changeMember();
    auto* target = chooseTarget();
    auto* primary = target ? &target->motion : nullptr;
    std::array<std::uint32_t, FightMatch::ObjectLimit> activeNames{};
    unsigned names = 0;
    for (const auto& object : objects_) {
        const auto* attacker = std::get_if<FightAttacker>(&object);
        if (attacker && attacker->active() && attacker->owner() == leader_ && attacker->key()) activeNames[names++] = attacker->key();
    }
    const auto attacks = std::span(activeNames).first(names);
    if (player().cutin().kind != CutinKind::None) {
        player().stepCutin(primary, effectAnimate, stage_->geometry(), std::nullopt, rate, attacks);
        if (effectAnimate) player().finishGhostAnimation(primary);
        play(player().takeEffects());
        return;
    }
    for (auto& enemy : enemies_) {
        if (enemy.motion.cutin().kind == CutinKind::None) continue;
        enemy.motion.stepCutin(&player(), effectAnimate, stage_->geometry(), std::nullopt, rate);
        if (effectAnimate) enemy.motion.finishGhostAnimation(&player());
        play(enemy.motion.takeEffects());
        return;
    }
    animate=effects_.actorAnimation(animate);
    if (animate) {
        if (switchTicks_) --switchTicks_;
        populateStates();
        if (const auto entry = wave_.step(states_)) spawn(*entry);
        if (bossCue_.kind != MusouBossCue::Kind::None) return;
        recoverMembers();
    }
    player().step(stage_->geometry(), controls, animate, std::nullopt, primary,
        sourcePhysics_?effects_.motionRate():rate, attacks, hidesPlayer());
    if (animate && primary) player().faceIdleTarget(*primary);
    collectSpawns(player(), leader_, primary ? primary->motion().position : player().motion().position);
    if (player().counterPaused() || player().cutin().kind != CutinKind::None) return;
    for (auto& enemy : enemies_) {
        FighterControls input;
        if (enemy.motion.alive()) {
            input = enemy.bossAI ? enemy.bossAI->stepBoss(enemy.motion, player(), objects_, party_.size(), animate)
                                 : enemy.brain.step(enemy.motion, player(), animate, random_);
        }
        enemy.motion.step(stage_->geometry(), input, animate, std::nullopt, &player(),
            sourcePhysics_?effects_.motionRate():rate, {}, hidesEnemy(enemy.id));
        if (animate) enemy.motion.faceIdleTarget(player());
        collectSpawns(enemy.motion, enemy.id, player().motion().position);
        if (enemy.motion.counterPaused() || enemy.motion.cutin().kind != CutinKind::None) return;
    }
    advanceObjects(animate, rate, primary);
    if (animate) {
        contacts();
        contactObjects();
        if (!player().alive()) {
            selfDieShown_ = true;
            if (const auto* end = combatEffect("hit_end"))
                playContact({end, ContactEffectKind::Hurt, player().motion().position, 1, {}});
            FighterEffects effect{};
            effect.addShine(0xff0000,80);
            play(std::move(effect));
            slowTicks_ = 5 * LogicHz;
            slowPeriod_ = 4;
            effects_.slow(2,5000);syncSourceSlow(2);
        }
        else if (!beginBossKo()) updateEnemies();
    }
}

bool MusouBattle::step(FighterControls controls, bool switchPressed) {
    if(!sourcePhysics_&&wave_.result()!=MusouResult::Playing)return false;
    if(!sourcePhysics_){stepSource(controls,switchPressed);return true;}
    queuedControls_.submit(controls);queuedSwitch_|=switchPressed;
    if(!delivery_.advance())return false;
    sourceRoot(queuedControls_.take(),std::exchange(queuedSwitch_,false),false);
    return true;
}

void MusouBattle::stepSource(FighterControls controls, bool switchPressed) {
    if(sourcePhysics_&&controls.sourceHuman)controls.assistHeld=
        (controls.heldKeys&(1U<<unsigned(HumanKey::Assist)))&&
        (controls.heldKeys&(1U<<unsigned(HumanKey::Dash)));
    if (!sourcePhysics_&&wave_.result() != MusouResult::Playing) return;
    bool animate = false;
    stepFrame(controls, switchPressed, animate);
    stepDetached(animate);
}

void MusouBattle::stepDetached(bool animate){
    // Authored ENTER_FRAME listeners also run when GameCtrl's main loop is paused.
    auto* target = chooseTarget();
    auto* primary = target ? &target->motion : nullptr;
    for (auto& value : objects_) {
        if (auto* attacker = std::get_if<FightAttacker>(&value)) {
            const auto id = attacker->owner();
            auto* opponent = id < party_.size() ? primary : &player();
            if (auto* source = owner(id); source && opponent) {
                if (const auto control = attacker->applyFrameListeners(*source, *opponent,
                        id < party_.size() ? source : primary, sourcePhysics_?true:animate)) {
                    addObjects(std::span(&*control, 1), id, opponent->motion().position);
                }
            }
        }
    }
    const auto buffs = [&](FighterMotion& actor) {
        for (const auto& request : actor.takeBuffEffects()) {
            playContact({request.burst, ContactEffectKind::Hurt, request.origin, 1, {}});
            effects_.buff(request);
        }
    };
    for (auto& actor : party_) buffs(actor);
    for (auto& enemy : enemies_) buffs(enemy.motion);
    if(sourcePhysics_){syncSourceObjects();pruneSourceObjects();pruneRetainedEnemies();}
}

} // namespace bvn
