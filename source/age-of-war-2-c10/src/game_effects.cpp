#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace aow {
static Unit* effectTarget(Game& g,const Effect& e) {
 Unit* result=nullptr;
 for(auto& u:g.units) if(u.side!=e.side && u.hp>0 && std::abs(u.x-e.x)<u.w && std::abs(u.y-e.y)<u.h) result=&u;
 return result; // Original selects the LAST matching unit, not the nearest.
}
void Game::createSpecial(int side,int era) { if(era>=1 && era<=7) createEffect("special"+std::to_string(era),side,0,0); }
void Game::createEffect(const std::string& kind,int side,double x,double y,double scale) {
 Effect e;e.kind=kind;e.side=side;e.x=displayPixel(x);e.y=displayPixel(y);e.scale=scale;
 if(kind.size()==8 && kind.substr(0,7)=="special") {
  e.age=kind[7]-'0';e.time=10;
  if(e.age>=6) {
   e.time=std::numeric_limits<double>::quiet_NaN(); // AS3 uninitialized Number.
   e.x=-1080*(side==1?1:-1);e.y=e.age==6?-250:0;
  }
 } else if(kind=="meteor" || kind=="fireball" || kind=="arrow" || kind=="canonball" || kind=="planebomb") {
  double rot=kind=="planebomb"?90:80+random()*20;
  double velocity=kind=="canonball"?3:(kind=="planebomb"?1:1.4);
  e.xs=std::cos(rot*3.141592653589793/180)*velocity*3;
  e.ys=std::sin(rot*3.141592653589793/180)*velocity*3;
  e.rotation=(kind=="meteor" || kind=="planebomb")?0:rot;
  e.mass=rot; // Trail emission uses original travel angle.
  if(kind=="fireball") play(e.animation,"special3","anim",true);
 } else if(kind=="lightning" || kind=="laserbeam") {
  e.count=int(std::floor(random()*7+0.5));e.time=0.5;
  int sound=int(std::floor(random()*2+0.5));
  sounds.push_back({sound==0?"lightning":sound==1?"lightning2":"lightning3",x});
  if(Unit* u=effectTarget(*this,e)) {
   int amount=kind=="lightning"?500:1500;u->hp-=amount;
   if(showDamage) {createEffect("damage",u->side,u->x,u->y-u->h);effects.back().value=amount;}
  }
 } else if(kind=="explosion" || kind=="firetrail") {
  play(e.animation,kind,"anim",false);
  if(kind=="explosion") sounds.push_back({"explosion6",x}); // No random selection in source.
 } else if(kind=="damage") e.time=2;
 if(!e.animation.key.empty())e.animId=nextAnim++;
 effects.push_back(e);
}
void Game::updateEffects(double dt) {
 // Manager captures its initial length; children created while rendering wait
 // for the next manager pass. Dead objects are removed after that same pass.
 const std::size_t n=effects.size();
 std::vector<std::size_t> remove;
 for(std::size_t i=0;i<n;++i) {
  Effect e=effects[i]; // Appending effects can reallocate; never retain a reference.
  if(!e.alive) {remove.push_back(i);continue;}
  const auto kind=e.kind;
  if(kind.size()==8 && kind.substr(0,7)=="special") {
   e.delay+=dt;
   if(e.age<=5) {
    e.time-=dt;
    double interval=e.age==2 || e.age==3?0.3:(e.age==4?0.05:0.2);
    if(e.delay>=interval) {
     static const char* children[]={"meteor","lightning","fireball","arrow","canonball"};
     createEffect(children[e.age-1],e.side,random()*1160-580,e.age==2?0:-400);
     e.delay=0;
    }
    if(e.time<=0)e.alive=false;
   } else {
    int multi=e.side==1?1:-1;
    e.x=displayPixel(e.x+dt*(e.age==6?300:450)*multi);
    double interval=e.age==6?0.2:0.1;
    if(e.delay>=interval) {
     if(e.x>-580 && e.x<580) createEffect(e.age==6?"planebomb":"laserbeam",e.side,e.x,e.y);
     e.delay=0;
    }
    if(e.x*multi>1080)e.alive=false;
   }
  } else if(kind=="meteor" || kind=="fireball" || kind=="arrow" || kind=="canonball" || kind=="planebomb") {
   if(kind=="arrow" && e.count) { e.life+=dt;if(e.life>=4)e.alive=false; }
   else {
    e.delay+=dt;
    if(e.delay>=0.05) {
     e.delay=0;
     if(kind=="meteor" || kind=="fireball") {
      createEffect("firetrail",e.side,e.x,e.y,0.5);
      effects.back().rotation=e.mass+245+random()*50;
     }
    }
    if(kind=="planebomb")e.rotation+=dt*100*(e.side==1?1:-1);
    e.x=displayPixel(e.x+e.xs*dt*100);e.y=displayPixel(e.y+e.ys*dt*100);
    Unit* u=effectTarget(*this,e);
    if(u) {
     int amount=kind=="meteor"?200:kind=="canonball"?1000:kind=="planebomb"?600:800;
     u->hp-=amount;
     if(showDamage) {createEffect("damage",u->side,u->x,u->y-u->h);effects.back().value=amount;}
     if(kind!="arrow")createEffect("explosion",e.side,e.x,e.y);
     e.alive=false;
    } else if(e.y>=0) {
     if(kind=="arrow")e.count=1;
     else {createEffect("explosion",e.side,e.x,e.y);e.alive=false;}
    }
   }
  } else if(kind=="lightning" || kind=="laserbeam") {
   e.time-=dt;e.alpha=e.time/0.5;if(e.time<=0)e.alive=false;
  } else if(kind=="damage") {
   e.time-=dt;e.y=displayPixel(e.y-e.time*dt*20);e.alpha=e.time/2;if(e.time<=0)e.alive=false;
  }
  effects[i]=e;
 }
 for(auto it=remove.rbegin();it!=remove.rend();++it)effects.erase(effects.begin()+*it);
}
}
