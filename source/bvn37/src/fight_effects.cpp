// Adapted from 5DPLAY Game Studio's EffectShakeHandler and ShineEffectView.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight_effects.hpp"
#include "fighter_motion.hpp"

#include <algorithm>
#include <cmath>
#ifdef __PSP__
#include <pspkernel.h>
#endif

namespace bvn {
namespace {
constexpr unsigned GroundCueFrames[]{8, 10, 7, 11, 10, 20, 22};
constexpr unsigned ReactionFrames[]{34, 17, 15, 14, 16};
static_assert(std::size(ReactionFrames) == static_cast<unsigned>(ReactionCue::Count));
static_assert(std::size(GroundCueFrames) == static_cast<unsigned>(GroundCue::Count));
}

void FightEffects::ShakeAxis::pulse(float requested) {
    if (requested == 0) return;
    if (!power) {
        direction = requested > 0 ? 1 : -1;
        power = int(std::abs(requested));
    } else power = int(power + std::abs(requested) * .5);
    power = std::min(power, 10);
}

void FightEffects::ShakeAxis::animate(float& offset) {
    const int total = hold + power;
    if (total <= 0) return;
    offset = float(total * direction);
    if (power > 0 && frame % 2 == 0) {
        power -= loss;
        if (power < loss) {
            power = 0; offset = 0; frame = 0; loss = 0;
            return;
        }
    }
    ++frame;
    direction = -direction;
}

bool FightEffects::emit(const CombatEffect& effect,Point origin,int facing,std::shared_ptr<const void> identity) {
    auto count=std::ranges::find_if(effectCounts_,[&](const auto& entry){
        return entry.effect==&effect&&entry.identity.get()==identity.get();
    });
    if(count==effectCounts_.end())count=effectCounts_.insert(effectCounts_.end(),{&effect,identity,0});
    if(++count->count>3)return false;
    ++emitted_;
    lastCombat_=startCombat(effect,origin,facing,std::move(identity));
    if(paint_&&lastCombat_)paint_->add(SourcePaint::Layer::Players,SourcePaint::key(SourcePaint::Kind::Combat,lastCombat_->paintId));
    updateCombatViews();
    return true;
}

std::shared_ptr<CombatEffectView> FightEffects::startCombat(const CombatEffect& effect,Point origin,int facing,
                                                         std::shared_ptr<const void> identity) {
    const auto* art=std::ranges::find(CombatArtData,effect.className,&CombatArt::className);
    if(art==std::end(CombatArtData))return {};
    // An identity with no external or active references cannot be requested
    // again. Discard only such inactive cache entries to avoid retaining dead
    // bullet hit models on a PSP; global effects keep their reusable views.
    std::erase_if(viewCache_,[](const auto& slot){return !slot.global&&slot.identity.expired()&&
        !slot.view->active&&slot.view.use_count()==1;});
    std::shared_ptr<CombatEffectView> view;
    for(auto& slot:viewCache_)if(slot.effect==&effect&&!slot.view->active&&
        slot.global==!identity&&(slot.global||slot.identity.lock().get()==identity.get())){view=slot.view;break;}
    if(!view){
        view=std::make_shared<CombatEffectView>();
        view->paintId=++paintSerial_;
        viewCache_.push_back({&effect,identity,!identity,view});
    }
    const int direction=view->rotation?1:facing;
    unsigned angle=view->rotation;
    if(effect.randomRotation){
        effectRandom_^=effectRandom_<<13;effectRandom_^=effectRandom_>>17;effectRandom_^=effectRandom_<<5;
        angle=unsigned((std::uint64_t(effectRandom_)*360)>>32);
    }
    view->effect=&effect;view->art=art;view->origin=origin;
    view->facing=effect.randomRotation?1:direction;view->offsetFacing=direction;
    view->rotation=angle;view->age=view->cursor=view->displayFrame=0;
    view->identity=std::move(identity);view->finished=false;view->active=true;
    view->background=false;
    view->buff.reset();
    activeCombat_.push_back(view);
    return view;
}

void FightEffects::updateCombatViews(){
    combat_.clear();backgrounds_.clear();combat_.reserve(activeCombat_.size());
    for(const auto& view:activeCombat_)if(view->background)backgrounds_.push_back(*view);else combat_.push_back(*view);
    // addChildAt(display,0) inserts each newer background below existing ones.
    std::ranges::reverse(backgrounds_);
}

void FightEffects::background(const CombatEffect& effect,double hold) {
    ++emitted_;
    lastCombat_=startCombat(effect,{0,0},1,{});
    if(!lastCombat_)return;
    lastCombat_->background=true;
    if(paint_)paint_->add(SourcePaint::Layer::Stage,SourcePaint::key(SourcePaint::Kind::Combat,lastCombat_->paintId),0);
    if(hold!=-1)lastCombat_->holdFrame=int(hold*AnimationHz);
    scene_.hideMap=true;
    updateCombatViews();
}

void FightEffects::setLastTarget(const std::shared_ptr<EffectTarget>& target){
    if(!lastCombat_||!target)return;
    auto& view=*lastCombat_;view.target=target;
    if((view.effect->special||view.effect->buff)&&view.effect->hasTargetTint){
        const auto& tint=view.effect->targetTint;target->tint={tint[0],tint[1],tint[2]};
    }
    if(view.effect->steel&&target->steel){
        target->tint={150,150,target->superSteel?0:150};
        if(target->superSteel){FighterEffects request;request.addShine(0xffff00,30);play(request);}
        // The original closure reads the reusable SteelHitEffect's current
        // target when called, rather than capturing the fighter by value.
        freezeCallbacks_.push_back(lastCombat_);
    }
    updateCombatViews();
}

void FightEffects::special(const CombatEffect& effect,const std::shared_ptr<EffectTarget>& target){
    if(!target)return;
    ++emitted_;
    lastCombat_=startCombat(effect,target->position,target->facing,{});
    setLastTarget(target);updateCombatViews();
    if(paint_&&lastCombat_)paint_->add(SourcePaint::Layer::Players,SourcePaint::key(SourcePaint::Kind::Combat,lastCombat_->paintId));
}

void FightEffects::buff(const BuffEffectRequest& request) {
    if (!request.aura || !request.target || !request.buff) return;
    ++emitted_;
    lastCombat_ = startCombat(*request.aura, request.origin, request.facing, {});
    if (lastCombat_) lastCombat_->buff = request.buff;
    setLastTarget(request.target);
    if(paint_&&lastCombat_)paint_->add(SourcePaint::Layer::Players,SourcePaint::key(SourcePaint::Kind::Combat,lastCombat_->paintId));
    updateCombatViews();
}

void FightEffects::freezeOver(){
    for(const auto& view:freezeCallbacks_)if(view->target)view->target->resumeColor();
    freezeCallbacks_.clear();
}

void FightEffects::play(FighterEffects requests) {
    const auto reaction = [&](const ReactionCueView& view) {
        auto empty = std::ranges::find(reactions_, ReactionCue::Count, &ReactionCueView::cue);
        if (empty == reactions_.end()) empty = std::ranges::max_element(reactions_, {}, &ReactionCueView::age);
        *empty = view;
        if(paint_)paint_->add(SourcePaint::Layer::Players,SourcePaint::key(SourcePaint::Kind::Reaction,unsigned(empty-reactions_.begin())));
    };
    if (requests.reaction) reaction(*requests.reaction);
    for (const auto& view : requests.reactions) reaction(view);
    if (requests.ground) {
        auto empty = std::ranges::find(ground_, GroundCue::Count, &GroundCueView::cue);
        if (empty == ground_.end()) empty = std::ranges::max_element(ground_, {}, &GroundCueView::frame);
        *empty = *requests.ground;
        if(paint_)paint_->add(SourcePaint::Layer::Players,SourcePaint::key(SourcePaint::Kind::Ground,unsigned(empty-ground_.begin())));
    }
    if (requests.shakeLoopX || requests.shakeLoopY) {
        shakeX_.hold = int(requests.shakeLoopX.value_or(0));
        shakeY_.hold = int(requests.shakeLoopY.value_or(0));
        // endShake resets the stage, but retains the pulse and direction state.
        if (!requests.startShake && !requests.shakeLoopX.value_or(0) && !requests.shakeLoopY.value_or(0))
            scene_.shiftX = scene_.shiftY = 0;
    }
    if ((requests.shakeRequested || requests.shakeX != 0 || requests.shakeY != 0) &&
        std::isfinite(requests.shakeX) && std::isfinite(requests.shakeY) &&
        std::abs(shakeX_.power) <= std::abs(requests.shakeX) &&
        std::abs(shakeY_.power) <= std::abs(requests.shakeY)) {
        shakeX_.pulse(requests.shakeX); shakeY_.pulse(requests.shakeY);
        const int requestedTime = int(requests.shakeTimeMs);
        const int time = requestedTime > 0 ? requestedTime : 500;
        const double frames = time * .001 * AnimationHz;
        shakeX_.loss = std::max(1, int(std::ceil(shakeX_.power / frames)));
        shakeY_.loss = std::max(1, int(std::ceil(shakeY_.power / frames)));
    }
    if(sourceProfile_) {
        const auto emit = [&](ShineRequest request) {
            if(!shineHz_)return;
            // EffectCtrl checks >3 before pushing, so four layers are allowed.
            if(shines_.size()>3)shines_.erase(shines_.begin());
            unsigned percent=request.percent;
            if(request.color<=0x7fffff)percent=percent*8/10;
            const unsigned gap=(gameHz_+shineHz_-1)/shineHz_-1;
            shines_.push_back({request.color,percent,gap,1000/shineHz_,gap+1});
        };
        if(!requests.shines.empty())for(const auto& request:requests.shines)emit(request);
        else if(requests.shine)emit({*requests.shine,*requests.shine==0xffffff?30U:20U});
        updateShineViews();
    } else if (requests.shine) {
        scene_.shineColor = *requests.shine;
        shineFrame_ = 0;
        shinePercent_ = *requests.shine == 0xffffff ? 30 : 20;
        scene_.shineAlpha = shinePercent_ * 255 / 100;
    }
}

void FightEffects::updateShineViews() {
    shineViews_.clear();
    for(const auto& shine:shines_)shineViews_.push_back({shine.color,shine.percent*255/100});
    scene_.shineAlpha=shineViews_.empty()?0:shineViews_.back().alpha;
    if(!shineViews_.empty())scene_.shineColor=shineViews_.back().color;
}

void FightEffects::step(bool animate,std::span<const ShadowSource> sources,std::optional<std::uint32_t> visualTimeMs) {
    stepFrame(animate,sources,visualTimeMs,{});
}

void FightEffects::stepOrdered(bool animate,std::span<const ShadowSource> sources,FrameCallbacks callbacks,
                              std::optional<std::uint32_t> visualTimeMs) {
    stepFrame(animate,sources,visualTimeMs,callbacks);
}

void FightEffects::stepFrame(bool animate,std::span<const ShadowSource> sources,
                            std::optional<std::uint32_t> visualTimeMs,FrameCallbacks callbacks) {
    const bool ordered=callbacks.animation||callbacks.render;
    effectCountPhase_+=gameHz_;
    const bool render=effectCountPhase_>=deliveryHz_;
    if(render){
        blur_.render();
        if(ordered)stepShines(visualTimeMs);
        effectCountPhase_-=deliveryHz_;effectCounts_.clear();
        for(unsigned i=0;i<activeCombat_.size();++i) {
            auto& view=*activeCombat_[i];
            if(view.effect->buff && view.buff) {
                if(view.buff->finished) {
                    if(view.effect->hasTargetTint && view.target) view.target->resumeColor();
                    if(paint_)paint_->remove(SourcePaint::key(SourcePaint::Kind::Combat,view.paintId));
                    view.active=false;view.identity.reset();activeCombat_.erase(activeCombat_.begin()+i);
                    continue;
                }
                if(view.target) view.origin=view.target->position;
            }
            if(!view.effect->special || view.finished || !view.target) continue;
            const int state=view.target->state;
            if(state==0||state==23||state==24){
                view.cursor=view.art->finish;view.finished=true;
                if(view.effect->hasTargetTint)view.target->resumeColor();
            }else view.origin=view.target->position;
        }
    }
    if (animate) {
        if(!ordered)blur_.animate();
        // EffectView starts at frame zero, repeats it on its first animation
        // callback, and removes before displaying the final cached frame.
        for(unsigned i=0;i<activeCombat_.size();++i){
            auto& view=*activeCombat_[i];
            bool remove=false;
            if(view.cursor==view.art->frames-1){
                if(view.effect->buff) view.cursor=0;
                else if(view.holdFrame!=-1)view.cursor=0;
                else remove=true;
            }
            if(!view.effect->buff&&view.holdFrame!=-1&&view.holdFrame--<=0)remove=true;
            if(remove){
                // Each original background removal restores visibility even
                // when another background remains. It is not a reference count.
                if(view.background)scene_.hideMap=false;
                if(paint_)paint_->remove(SourcePaint::key(SourcePaint::Kind::Combat,view.paintId));
                view.active=false;view.identity.reset();activeCombat_.erase(activeCombat_.begin()+i);
                continue;
            }
            if(view.art->loop&&view.cursor==view.art->loop)view.cursor=1;
            view.displayFrame=view.cursor++;++view.age;
        }
        for (const auto& source : sources) if (source.active &&
            std::ranges::find(shadows_, source.id, &ShadowEmitter::id) == shadows_.end()) shadows_.push_back({source.id, 0, {}, source.lifetime});
        for (auto it = shadows_.begin(); it != shadows_.end();) {
            const auto source = std::ranges::find(sources, it->id, &ShadowSource::id);
            const bool active = source != sources.end() && source->active;
            if (!active && it->stamps.empty()) { it = shadows_.erase(it); continue; }
            if (active && it->frame++ > 1) {
                it->stamps.push_back({source->asset, source->pose, source->position, source->facing, .8, ++shadowSerial_, source->tint});
                if(paint_)paint_->add(SourcePaint::Layer::Players,SourcePaint::key(SourcePaint::Kind::Shadow,unsigned(shadowSerial_)),0);
                it->frame = 0;
            }
            // ShadowEffectView removes in a forward loop: the shifted neighbor
            // skips this animation's alpha decrement when a bitmap is removed.
            for (unsigned i = 0; i < it->stamps.size(); ++i) {
                if ((it->stamps[i].alpha -= .1) <= 0) {if(paint_)paint_->remove(SourcePaint::key(SourcePaint::Kind::Shadow,unsigned(it->stamps[i].serial)));it->stamps.erase(it->stamps.begin() + i);}
            }
            ++it;
        }
        shadowViews_.clear();
        for (const auto& emitter : shadows_) shadowViews_.insert(shadowViews_.end(), emitter.stamps.begin(), emitter.stamps.end());
        std::ranges::sort(shadowViews_, std::greater{}, &ShadowStamp::serial);
        for (auto& effect : reactions_) {
            if (effect.cue != ReactionCue::Count && ++effect.age >= ReactionFrames[unsigned(effect.cue)]) {effect.cue = ReactionCue::Count;if(paint_)paint_->remove(SourcePaint::key(SourcePaint::Kind::Reaction,unsigned(&effect-reactions_.data())));}
        }
        for (auto& effect : ground_) {
            if (effect.cue != GroundCue::Count && ++effect.frame >= GroundCueFrames[static_cast<unsigned>(effect.cue)]) {
                effect.cue = GroundCue::Count;
                if(paint_)paint_->remove(SourcePaint::key(SourcePaint::Kind::Ground,unsigned(&effect-ground_.data())));
            }
        }
    }
    if(animate&&ordered){
        if(callbacks.animation)callbacks.animation(callbacks.context);
        shakeX_.animate(scene_.shiftX);shakeY_.animate(scene_.shiftY);
        if(callbacks.afterAnimation)callbacks.afterAnimation(callbacks.context);
        blur_.animate();
    }
    updateCombatViews();
    if(!ordered)stepShines(visualTimeMs);
    if (animate&&!ordered) {
        shakeX_.animate(scene_.shiftX);
        shakeY_.animate(scene_.shiftY);
    }
    if(callbacks.render)callbacks.render(callbacks.context);
}

void FightEffects::stepShines(std::optional<std::uint32_t> visualTimeMs) {
    if(sourceProfile_) {
        ++visualTicks_;
        if(!visualTimeMs) {
#ifdef __PSP__
            visualTimeMs=std::uint32_t(sceKernelGetSystemTimeWide()/1000);
#else
            visualTimeMs=std::uint32_t(visualTicks_*1000/deliveryHz_);
#endif
        }
        shineLogic_+=gameHz_;
        if(shineLogic_>=deliveryHz_) {
            shineLogic_-=deliveryHz_;
            for(unsigned i=0;i<shines_.size();++i) {
                auto& shine=shines_[i];
                if(shine.gap&&++shine.frame%shine.gap)continue;
                if(shine.startTimeMs) {
                    const auto elapsed=std::int32_t(*visualTimeMs-shine.startTimeMs);
                    const double rate=double(elapsed)/shine.frameTimeMs;
                    if(rate>1.2) {
                        const double loss=std::ceil(shine.loss*rate);
                        if(loss>shine.loss)shine.loss=unsigned(std::min(loss,double(shine.percent)));
                    }
                }
                shine.startTimeMs=*visualTimeMs;
                shine.percent-=std::min(shine.percent,shine.loss);
                // The original forward loop skips a shifted neighbor on removal.
                if(shine.percent<=5)shines_.erase(shines_.begin()+i);
            }
        }
        updateShineViews();
    } else if(scene_.shineAlpha&&++shineFrame_%3==0) {
        shinePercent_-=std::min(shinePercent_,4U);
        scene_.shineAlpha=shinePercent_>5?shinePercent_*255/100:0;
    }
}

} // namespace bvn
