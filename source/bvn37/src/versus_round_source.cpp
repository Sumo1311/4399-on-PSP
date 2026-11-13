// Adapted from GameCtrl.render, GameStartCtrl, GameEndCtrl and FightUI.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "versus_round.hpp"

namespace bvn {
void VersusRound::SourceView::focus(int side){owner.match_.focus(side<0?std::nullopt:std::optional<unsigned>(side));}
void VersusRound::SourceView::intro(unsigned side){owner.introSide_=side;owner.match_.actor(side).present(FighterCommand::Intro);owner.setPhase(RoundPhase::IntroPose);}
void VersusRound::SourceView::idle(){for(unsigned side=0;side<2;++side)owner.match_.actor(side).cancelIntro();}
void VersusRound::SourceView::transitionOut(bool notify){owner.sourceTransition_.out(notify);owner.sourceCallbacks_.add(SourceRenderCallbacks::Group::Main,TransitionCallback);}
void VersusRound::SourceView::quickTransition(){owner.sourceQuick_.begin(owner.sourceUiWallMs_);owner.setPhase(RoundPhase::IntroSkip);}
void VersusRound::SourceView::transitionIn(){owner.sourceTransition_.in();owner.sourceCallbacks_.add(SourceRenderCallbacks::Group::Main,TransitionCallback);}
void VersusRound::SourceView::startBanner(){owner.sourceHud_.show();owner.sourceBanner_.start(owner.round_);owner.setPhase(RoundPhase::Ready);}
void VersusRound::SourceView::clearBanner(){owner.sourceBanner_.clear();}
void VersusRound::SourceView::win(){owner.showWinner();}
void VersusRound::SourceView::endBanner(){
    auto& game=owner;
    game.sourceRenderEnd_=true;game.sourcePlayOver_=game.sourceSlowStarted_=false;game.winnerDelay_=0;
    game.sourceEndPending_=false;
    if(game.phase_==RoundPhase::TimeOver){game.sourceHud_.hideQi();game.sourceBanner_.timeOver();return;}
    game.match_.focus(1-game.winner_);game.match_.flashKnockout(1-game.winner_);
    game.match_.manualPause();game.setPhase(RoundPhase::KnockoutFlash);
}

void VersusRound::sourceStartRound(bool next){
    SourceView view{*this};sourceBanner_.clear();sourceRenderEnd_=sourcePlayOver_=false;
    if(next)sourceStart_.nextRound(deliveryHz(),view);
    else sourceStart_.versus(deliveryHz(),introMask_==3?-1:introMask_==1?1:2,view);
}

void VersusRound::pausedDisplayFrame(){
    if(!sourceExplicitUiClock_){sourceUiWallMs_=++sourceDisplayFrames_*1000./60;}
    if(rules_.sourceQuality&&delivery_.advance())sourceRootCallback({},false,true);
}
void VersusRound::stepOrderedSource(const std::array<FighterControls,2>& controls,bool skip){sourceRootCallback(controls,skip,false);}
void VersusRound::sourceRootCallback(const std::array<FighterControls,2>& controls,bool skip,bool uiPaused){
    ++sourceRootFrames_;sourceMainDispatched_=false;sourceQuick_.render(sourceUiWallMs_);sourceHud_.animateNested();match_.advanceComboUi();
    sourceCallbacks_.render([&](unsigned id){
        if(id==GameCallback){if(!uiPaused){sourceMainDispatched_=true;sourceGameCallback(controls,skip);}return;}
        if(id!=TransitionCallback)return;
        switch(sourceTransition_.render(deliveryHz())){
        case SourceTransitionTimeline::Callback::Start:sourceStart_.transitionComplete();break;
        case SourceTransitionTimeline::Callback::End:sourceEnd_.transitionComplete();break;
        default:break;
        }
        if(!sourceTransition_.rendering())sourceCallbacks_.remove(SourceRenderCallbacks::Group::Main,TransitionCallback);
    });
    match_.stepDetached(true);
}
void VersusRound::sourceGameCallback(const std::array<FighterControls,2>& controls,bool skip){
    ++ticks_;++phaseTicks_;sourceSkip_=skip;
    SourceView view{*this};
    if(skip){if(sourceStart_.active())sourceStart_.skip(view);else if(sourceEnd_.active())sourceEnd_.skip();}
    match_.sourceFrameHooks({this,
        [](void* self){static_cast<VersusRound*>(self)->sourceStageUi();},
        [](void* self,bool animate){static_cast<VersusRound*>(self)->sourceBeforeMain(animate);},
        [](void* self,bool animate){static_cast<VersusRound*>(self)->sourceAfterMain(animate);},
        [](void* self,unsigned loser){static_cast<VersusRound*>(self)->sourceDeath(loser);}});
    match_.stepMainFrame(stage_.geometry(),controls,training()?Contacts::Training:Contacts::Round);
}

void VersusRound::sourceBeforeMain(bool animate){
    if(!animate||training()||sourceStart_.active()||sourceEnd_.active()||secondsPerRound_==-1)return;
    if(++clockFrames_>AnimationHz){
        clockFrames_=0;
        if(--seconds_<=0){
            const double first=match_.actor(0).health(),second=match_.actor(1).health();
            endRound(first==second?-1:first>second?0:1,true);
        }
    }
}

void VersusRound::sourceDeath(unsigned loser){
    if(!training()&&phase_==RoundPhase::Playing)endRound(1-loser,false);
}

void VersusRound::sourceStageUi(){
    sourceUiSeconds_=seconds_;
    std::array<SourceHudActor,2> uiActors;
    for(unsigned i=0;i<2;++i){const auto& actor=match_.actor(i);uiActors[i]={actor.health(),actor.maxHealth(),actor.qi(),actor.energy(),actor.maxEnergy(),double(actor.assistCharge())/AssistChargeMax,actor.aiState(),actor.overloaded()};}
    sourceHud_.render(uiActors,match_.camera().zoom,deliveryHz());
    // FightUI.renderEnd precedes its animation/COMPLETE dispatch.
    if(sourceRenderEnd_&&sourcePlayOver_){
        if(winnerDelay_){
            if(!--winnerDelay_){
                if(winner_>=0){match_.focus(winner_);sourceBanner_.winner();sourceEnd_.uiComplete();}
                sourceRenderEnd_=false;
            }
        }else if(winner_>=0){
            const auto& loser=match_.actor(1-winner_);
            if(loser.aiState()==AiState::Flying&&!sourceSlowStarted_){sourceSlowStarted_=true;sourceFlyTick_=ticks_;}
            if(loser.deathComplete()){
                winnerDelay_=sourceSlowStarted_&&ticks_-sourceFlyTick_>=2*deliveryHz()?deliveryHz():2*deliveryHz();
                match_.resumeKnockoutSpeed();setPhase(RoundPhase::WinnerDelay);
            }
        }
    }
    // flash.utils.setTimeout(500) is still a nominal-time adapter here. It is
    // not a KyoTimeout counter; wall-clock/event-loop dispatch remains open.
    if(phase_==RoundPhase::KnockoutFlash&&phaseTicks_>=deliveryHz()/2){
        sourceHud_.hideQi();sourceBanner_.knockout();setPhase(RoundPhase::Knockout);
    }
    if(!sourceUiClock_.advance())return;
    const auto kind=sourceBanner_.kind();
    if(!sourceBanner_.renderAnimate())return;
    switch(kind){
    case SourceBannerTimeline::Kind::Start:sourceStart_.uiComplete();break;
    case SourceBannerTimeline::Kind::Knockout:
        match_.resumeKnockout();sourcePlayOver_=true;setPhase(RoundPhase::KnockoutFall);break;
    case SourceBannerTimeline::Kind::TimeOver:
        if(winner_<0){sourceBanner_.draw();setPhase(RoundPhase::Draw);}
        else{sourcePlayOver_=true;winnerDelay_=deliveryHz();setPhase(RoundPhase::WinnerDelay);}
        break;
    case SourceBannerTimeline::Kind::Draw:sourceEnd_.uiComplete();break;
    default:break;
    }
}

void VersusRound::sourceAfterMain(bool){
    ++sourceMainFrames_;++sourceMainPhaseTicks_;
    SourceView view{*this};
    if(sourceStart_.active()){
        sourceGates(false);
        const bool intro=match_.actor(0).performing(FighterCommand::Intro)||match_.actor(1).performing(FighterCommand::Intro);
        if(sourceStart_.render(intro,view)){
            sourceStart_.finish();sourceGates(true);setPhase(RoundPhase::Playing);
            if(sourceDeferredPause_){sourceDeferredPause_=false;sourcePauseReady_=true;match_.manualPause();}
        }else if(phase_!=RoundPhase::Ready&&phase_!=RoundPhase::IntroSkip){
            const auto step=sourceStart_.step();
            if(step==1||step==3)setPhase(RoundPhase::IntroFocus);
            else if(step>=4)setPhase(RoundPhase::IntroPan);
        }
    }
    if(sourceEnd_.active()){
        const auto state=winner_<0?AiState::Normal:match_.actor(winner_).aiState();
        const bool allowWin=state!=AiState::Super&&state!=AiState::Ultimate&&state!=AiState::Transform;
        if(sourceEnd_.render(allowWin,view))nextRound();
    }
    if(training())for(unsigned side=0;side<2;++side){auto& actor=match_.actor(side);actor.updateTraining();history_[side].step(actor,deliveryHz());}
}
} // namespace bvn
