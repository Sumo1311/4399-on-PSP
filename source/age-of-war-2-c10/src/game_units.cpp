#include "game.hpp"
#include <algorithm>
#include <cmath>
namespace aow {
namespace {
double rounded(double v) { return std::floor(v+0.5); }
std::string unitKey(const Unit& u) { return "u"+std::to_string(u.id); }
void unitPlay(Unit& u,const std::string& label,bool loop=false) {
 play(u.animation,unitKey(u),label,loop);
}
}
void Game::createUnit(int id,int side) {
 if(id<1 || static_cast<std::size_t>(id)>unitDefs.size() || (side!=1 && side!=2)) return;
 const auto& d=unitDefs[id-1];
 Unit u; u.id=id; u.side=side; u.serial=nextSerial++;u.animId=nextAnim++;
 u.x=side==1?-580:580;
 u.speed=u.originalSpeed=d.speed/3.2; u.w=d.w; u.h=d.h;
 std::string type=d.type,armor;
 if(type=="Infantry" || type=="Anti-Armor" || type=="Armored") armor=type+" Armor";
 u.hp=rounded(int(d.health)*(1+bonus(armor,side)));
 if(side==2) u.hp=rounded(difficulty*u.hp);
 u.maxHP=u.hp;
 u.range=rounded(d.range*(1+bonus(type=="Support"?"Support Range":"",side)));
 u.reward=rounded(int(d.cost)*(1+bonus("Income",side)));
 unitPlay(u,"idle",true); units.push_back(u);
}
Unit* Game::nearest(double x,int side,bool enemyOnly,int ignoreSerial) {
 Unit* best=nullptr; double distance=1200;
 for(auto& u:units) {
  double delta=(u.x-x)*(side==1?1:-1);
  if(u.serial!=ignoreSerial && u.hp>0 && (!enemyOnly || u.side!=side) && delta>0 && delta<distance) {
   best=&u; distance=delta;
  }
 }
 return best;
}
void Game::animateUnits(double dt) {
 for(auto& u:units) if(advance(u.animation,dt)) unitAnimEnd(u);
}
void Game::unitAnimEnd(Unit& u) {
 const std::string label=u.animation.label;
 if(label=="walkStart") unitPlay(u,"walkCycle",true);
 else if(label=="attackMelee" || label=="attackMelee2") {
  u.onMelee=true; meleeHit(u); unitPlay(u,label+"Hit");
 } else if(label=="attackMeleeHit" || label=="attackMelee2Hit") {
  u.onMelee=false;
  double roll=random(); // checkMelee consumes a draw even without the alternate label.
  unitPlay(u,hasLabel(unitKey(u),"attackMelee2") && roll>=0.5?"attackMelee2":"attackMelee");
 } else if(label=="attackRanged") {
  rangedHit(u); unitPlay(u,"attackRangedHit");
 } else if(label=="attackRangedHit") unitPlay(u,"attackRanged");
 else if(label=="walkAttack") { rangedHit(u); unitPlay(u,"walkAttackHit1"); }
 else if(label=="walkAttackHit1") {
  if(hasLabel(unitKey(u),"walkAttackHit2")) { rangedHit(u); unitPlay(u,"walkAttackHit2"); }
  else unitPlay(u,"walkAttack");
 } else if(label=="walkAttackHit2") unitPlay(u,"walkAttack");
 else if(label=="die") u.alive=false;
}
void Game::checkUnitAnim(Unit& u) {
 std::string state="idle"; bool walking=u.canWalk;
 if(u.onMelee) { walking=false; state="attackMelee"; u.unitRange=false; }
 if(u.wait>0) walking=false;
 if(u.hp<=0) state="die";
 else if(!walking && (u.unitMelee || u.baseMelee)) state="attackMelee";
 else if(!walking && (u.unitRange || u.baseRanged)) state="attackRanged";
 else if(walking && (u.baseRanged || u.unitRange)) state="walkAttack";
 else if(walking) state="walk";
 if(u.state==state) return;
 if(state=="attackMelee" || state=="attackRanged") u.onMelee=false;
 unitPlay(u,state=="walk"?"walkStart":state,state=="idle");
 u.state=state; // Missing labels must not prevent the logical state transition.
}
void Game::meleeHit(Unit& u) {
 const auto& d=unitDefs[u.id-1];
 if(u.baseMelee) {
  emitSound(d.meleeSounds,u.x);
  damageBase(u.side,damage(d.type,"base",d.lo,d.hi,u.side));
  return;
 }
 Unit* target=nearest(u.x,u.side,true,u.serial);
 if(!target) return;
 int amount=damage(d.type,unitDefs[target->id-1].type,d.lo,d.hi,u.side);
 target->hp-=amount;
 const double x=target->x,y=target->y-target->h; const int side=target->side;
 if(showDamage) {
  createEffect("damage",side,x,y);
  if(!effects.empty()) effects.back().value=amount;
 }
 emitSound(d.meleeSounds,u.x);
}
void Game::rangedHit(Unit& u) {
 const auto& d=unitDefs[u.id-1];
 if(d.shoot==1) {
  const auto* c=clip(unitKey(u));
  if(c && u.animation.frame>=0 && static_cast<std::size_t>(u.animation.frame)<c->points.size() && c->points[u.animation.frame].present) {
   const auto bp=c->points[u.animation.frame];
   // Ruffle 0.7.1 uses f32 display matrices and ties-to-even twip transforms;
   // property setters themselves truncate pixels to twips (displayPixel).
   double bx=(std::llround(u.x*20)+std::nearbyint(float(bp.x*20)*float(d.scale*(u.side==1?-1:1))))/20.0;
   double by=(std::llround(u.y*20)+std::nearbyint(float(bp.y*20)*float(d.scale)))/20.0;
   createProjectile(d.projectile,u.side,bx,by,bp.rotation+180,bp.velocity,d.lo,d.hi,d.type);
  } else error="Missing projectile bp: "+unitKey(u)+" frame "+std::to_string(u.animation.frame);
 } else {
  Unit* target=nearest(u.x,u.side,true,u.serial);
  if(target) {
   int amount=damage(d.type,unitDefs[target->id-1].type,d.lo,d.hi,u.side);
   target->hp-=amount;
   const double x=target->x,y=target->y-target->h; const int side=target->side;
   if(showDamage) {
    createEffect("damage",side,x,y);
    if(!effects.empty()) effects.back().value=amount;
   }
  } else if(u.baseRanged) damageBase(u.side,damage(d.type,"base",d.lo,d.hi,u.side));
 }
 emitSound(d.rangedSounds,u.x);
}
void Game::updateUnits(double dt) {
 units.erase(std::remove_if(units.begin(),units.end(),[](const Unit& u){return !u.alive;}),units.end());
 for(auto& u:units) {
  if(u.hp>0) {
   if(u.wait>0) u.wait-=dt;
   Unit* next=nearest(u.x,u.side,false,u.serial);
   u.canWalk=false; u.baseMelee=false; u.baseRanged=false; u.unitMelee=false; u.unitRange=false;
   if((u.side==1 && u.x+u.w>580) || (u.side==2 && u.x-u.w< -580)) u.baseMelee=true;
   else u.canWalk=true;
   if((u.side==1 && u.x+u.w+u.range>580) || (u.side==2 && u.x-u.w-u.range< -580)) u.baseRanged=true;
   if(!next) {
    if(u.speed!=u.originalSpeed) u.speed=u.originalSpeed;
   } else {
    double distance=std::abs(next->x-u.x);
    if(distance<u.w+next->w) {
     if(u.side!=next->side) { u.unitMelee=true; u.canWalk=false; }
     else if(next->stopped) u.canWalk=false;
     else {
      if(next->speed<u.speed) u.speed=next->speed;
      u.wait=0.2; u.canWalk=false;
     }
    } else u.canWalk=true;
    if(u.range>0) {
     Unit* enemy=nearest(u.x,u.side,true,u.serial);
     if(enemy && std::abs(enemy->x-u.x)<u.range) u.unitRange=true;
    }
   }
   if(u.onMelee) u.canWalk=false;
   if(u.canWalk && u.wait<=0) u.x=displayPixel(u.x+u.speed*dt*100*(u.side==1?1:-1));
   u.stopped=!u.canWalk;
  } else if(!u.dead) {
   u.dead=true;
   pop[u.side-1]-=int(unitDefs[u.id-1].pop);
   if(u.side==1) xp+=int(rounded(u.reward/2));
   else { money+=int(u.reward); xp+=int(u.reward); }
   emitSound(unitDefs[u.id-1].deathSounds,u.x);
  }
  checkUnitAnim(u);
 }
}
}
