#include "game.hpp"
#include "source_parts.hpp"
#include <algorithm>
#include <cmath>
namespace aow {
namespace {
constexpr double pi=3.14159265358979323846;
Unit* turretEnemy(Game& g,const Turret& t) {
 Unit* best=nullptr; double distance=2300;
 for(auto& u:g.units) {
  double dx=(u.x-t.x)*(t.side==1?1:-1);
  if(dx>0 && dx<distance && u.hp>0 && u.side!=t.side) { best=&u; distance=dx; }
 }
 return best;
}
}
std::string turretClip(int id) {
 return id>=1 && id<=17?turretParts[id-1].key:"";
}
void Game::createTurret(int id,int side,int spot) {
 if(id<1 || id>17 || static_cast<std::size_t>(id)>turretDefs.size() || (side!=1 && side!=2)) return;
 const auto& d=turretDefs[id-1];
 Turret t; t.id=id; t.side=side; t.spot=spot;t.animId=nextAnim++;
 t.x=side==1?-624:620; t.y=-146-47*(spot-1);
 t.rotation=side==1?0:180;
 t.range=std::floor(d.range*(1+bonus("Turret Range",side))+0.5);
 play(t.animation,turretClip(id),"idle",true);
 turrets.push_back(t);
}
void Game::animateTurrets(double dt) {
 for(auto& t:turrets) {
  if(!advance(t.animation,dt)) continue;
  if(t.animation.label=="shoot") {
   turretHit(t);
   play(t.animation,turretClip(t.id),"shootLaunch",false);
  } else if(t.animation.label=="shootLaunch") {
   t.shooting=false; t.state="idle";
  }
 }
}
void Game::updateTurrets(double dt) {
 (void)dt;
 turrets.erase(std::remove_if(turrets.begin(),turrets.end(),[](const Turret& t){return !t.alive && (t.animation.loop || t.animation.stopped);}),turrets.end());
 for(auto& t:turrets) {
  if(!t.alive)continue;
  Unit* target=turretEnemy(*this,t);
  bool inRange=target && std::abs(target->x-t.x)<t.range;
  if(inRange) t.rotation=std::atan2(-target->h/2-t.y,target->x-t.x)*180/pi;
  const std::string state=(inRange && !t.shooting)?"shoot":"idle";
  if(t.state!=state) {
   play(t.animation,turretClip(t.id),state,state=="idle");
   t.state=state;
  }
 }
}
void Game::turretHit(Turret& t) {
 const auto& d=turretDefs[t.id-1];
 if(d.shoot==1) {
  const auto* c=clip(turretClip(t.id));
  const auto& part=turretParts[t.id-1];
  const int frame=t.animation.frame;
  auto launch=[&](const std::vector<PointFrame>& points,bool required) {
   if(frame<0 || static_cast<std::size_t>(frame)>=points.size() || !points[frame].present) {
    if(required) error="Missing projectile bp: "+turretClip(t.id)+" frame "+std::to_string(frame);
    return;
   }
   const auto& bp=points[frame];
   const double theta=t.rotation*pi/180;
   const float a=float(part.sx*std::cos(theta)),b=float(part.sx*std::sin(theta));
   const float cc=float(-part.sy*(t.side==2?-1:1)*std::sin(theta)),dd=float(part.sy*(t.side==2?-1:1)*std::cos(theta));
   const float xx=float(bp.x*20),yy=float(bp.y*20);
   const double lx=std::llround(part.x*20)+std::nearbyint(a*xx+cc*yy);
   const double ly=std::llround(part.y*20)+std::nearbyint(b*xx+dd*yy);
   const double wx=(std::llround(t.x*20)+std::nearbyint(float(d.scale)*float(lx)))/20.0;
   const double wy=(std::llround(t.y*20)+std::nearbyint(float(d.scale)*float(ly)))/20.0;
   createProjectile(d.projectile,t.side,wx,wy,bp.rotation-t.rotation*(t.side==1?1:-1)+(t.side==1?0:180),bp.velocity,d.lo,d.hi,"Turret");
  };
  if(c) { launch(c->points,true); launch(c->points2,false); }
  else error="Missing cannon clip: "+turretClip(t.id);
 } else {
  Unit* target=turretEnemy(*this,t);
  if(target) {
   int amount=damage("Turret",unitDefs[target->id-1].type,d.lo,d.hi,t.side);
   target->hp-=amount;
   createEffect("damage",target->side,target->x,target->y-target->h);
   if(!effects.empty()) effects.back().value=amount;
  }
 }
 emitSound(d.sounds,t.x);
}
}
