#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
namespace aow {
namespace {
constexpr double pi=3.14159265358979323846;
const ProjectileDef* projectileDefinition(const std::string& name) {
 for(const auto& d:projectileDefs) if(name==d.name) return &d;
 return nullptr;
}
}
void Game::createProjectile(const std::string& name,int side,double x,double y,double rotation,double velocity,double lo,double hi,const std::string& type) {
 const auto* d=projectileDefinition(name);
 if(!d) { error="BULLET CANNOT FIND XML: "+name; return; }
 Projectile p; p.name=name; p.side=side; p.x=displayPixel(x); p.y=displayPixel(y); p.lo=lo; p.hi=hi; p.type=type;
 p.scale=d->scale; p.mass=d->mass; p.explosion=d->explosion;
 const std::string mode=d->rotation;
 if(mode=="random") p.rotSpeed=(1-random()*2)*10;
 else if(mode=="follow") { p.follow=true; p.rotation=rotation*(side==1?-1:1); }
 else p.rotSpeed=std::strtod(d->rotation,nullptr);
 p.xs=std::cos(rotation*pi/180)*velocity*3;
 p.ys=std::sin(rotation*pi/180)*velocity*3;
 projectiles.push_back(p);
}
void Game::updateProjectiles(double dt) {
 projectiles.erase(std::remove_if(projectiles.begin(),projectiles.end(),[](const Projectile& p){return !p.alive;}),projectiles.end());
 for(auto& p:projectiles) {
  const double multi=p.side==1?-1:1;
  p.x=displayPixel(p.x+p.xs*dt*100*multi); p.y=displayPixel(p.y+p.ys*dt*100);
  if(p.follow) p.rotation=std::atan2(p.ys*multi,p.xs)*180/pi;
  else p.rotation+=p.rotSpeed*dt*100;
  p.ys+=p.mass*dt/4;
  Unit* target=nullptr;
  for(auto& u:units) {
   if(std::abs(u.x-p.x)<u.w*0.6 && std::abs(u.y-p.y)<u.h && u.hp>0 && u.side!=p.side) target=&u;
  }
  int baseDamage=0; bool baseHit=false;
  if(target) {
   int amount=damage(p.type,unitDefs[target->id-1].type,p.lo,p.hi,p.side);
   target->hp-=amount;
   if(showDamage) {
    createEffect("damage",target->side,target->x,target->y-target->h);
    if(!effects.empty()) effects.back().value=amount;
   }
   p.alive=false;
  } else if((p.side==1 && p.x>580) || (p.side==2 && p.x< -580)) {
   baseDamage=damage(p.type,"base",p.lo,p.hi,p.side); baseHit=true; p.alive=false;
  } else if(p.y>0) p.alive=false;
  if(!p.alive) {
   const auto* d=projectileDefinition(p.name);
   if(d) emitSound(d->sounds,p.x);
   if(p.explosion!=0) createEffect("explosion",p.side,p.x,p.y,p.explosion);
   if(baseHit) damageBase(p.side,baseDamage);
  }
 }
}
}
