// Adapted from GameMainLogicCtrl.checkBodyHit/checkHit and sprite hit methods.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fight_match.hpp"

namespace bvn {
struct SourceContactSprite {
    FighterMotion* fighter=nullptr;
    FightObject* object=nullptr;
    FighterMotion* owner=nullptr;
    unsigned ownerId=0;
    SourcePaint* paint=nullptr;
    SourcePaint::Key display=SourcePaint::None;
    Point position()const{return fighter?fighter->motion().position:std::visit([](const auto& v){return v.position();},*object);}
    WorldBox hurtBody()const{
        if(fighter)return fighter->hurtBody();
        if(const auto* bullet=std::get_if<FightProjectile>(object))return bullet->active()?bullet->body():WorldBox{};
        // FighterAttacker and Assister inherit BaseGameSprite's null body.
        return {};
    }
    std::optional<StrikeContact> strike(const SourceContactSprite& other)const{
        if(fighter){if(other.fighter)return fighter->strikeAgainst(*other.fighter);return fighter->strikeAgainst(other.position(),other.hurtBody());}
        return std::visit([&](auto& v){
            if(!v.active())return std::optional<StrikeContact>{};
            auto hit=v.strikeAgainst(other.position(),other.hurtBody());
            if constexpr(std::is_same_v<std::decay_t<decltype(v)>,FightAttacker>)
                if(hit)hit->recoil={&v,[](void* p,double x){static_cast<FightAttacker*>(p)->sourceRecoil(x);}};
            return hit;
        },*object);
    }
    void receive(const StrikeContact& hit,const SourceContactSprite& from)const{
        if(fighter){fighter->receiveStrike(hit,from.owner);return;}
        if(auto* bullet=std::get_if<FightProjectile>(object)){
            const auto* source=from.object?std::get_if<FightProjectile>(from.object):nullptr;
            bullet->receiveSourceHit(source?source->maxHealth():hit.power());
        }
    }
    void hit(const StrikeContact& contact,const SourceContactSprite& target)const{
        if(fighter){
            if(paint)paint->hit(display,target.display);
            // FighterMain.hit only gives Qi against another FighterMain.
            if(target.fighter)fighter->scoreStrike(contact,target.fighter);
            else fighter->noteStrike(contact);
            return;
        }
        std::visit([&](auto& v){
            if constexpr(std::is_same_v<std::decay_t<decltype(v)>,FightProjectile>){
                if(target.object&&std::holds_alternative<FightProjectile>(*target.object)){v.hitProjectile();return;}
                v.hitFighter(target.fighter&&target.fighter->motion().touchingSide);
                if(owner&&v.sourceMainOwner())owner->scoreObjectStrike(contact,.1,target.fighter);
            }else if(owner)owner->scoreObjectStrike(contact,v.asset().assister()?.15:.13,target.fighter);
        },*object);
    }
};
template<class Power,class Effects> void sourceSpriteContact(SourceContactSprite a,SourceContactSprite b,bool hits,Power&& power,Effects&& effects){
    if(!a.owner||!b.owner)return;
    const auto pa=a.position(),pb=b.position();
    if(a.fighter&&b.fighter&&std::abs(pa.x-pb.x)<=200&&std::abs(pa.y-pb.y)<=200)a.fighter->resolveBodyContact(*b.fighter);
    if(!hits||!inStrikeRange(a.position(),b.position()))return;
    // Both candidates are retained before either beHit/hit pair can change state.
    auto forward=a.strike(b),backward=b.strike(a);
    if(forward){*forward=power(*forward,a);b.receive(*forward,a);if(b.fighter)effects(b.fighter->takeEffects());a.hit(*forward,b);}
    if(backward){*backward=power(*backward,b);a.receive(*backward,b);if(a.fighter)effects(a.fighter->takeEffects());b.hit(*backward,a);}
}
}
