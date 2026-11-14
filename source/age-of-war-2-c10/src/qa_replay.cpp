#include "qa_replay.hpp"
#include <sstream>
#include <iomanip>
#include <cstdio>
namespace aow {
bool Replay::read(const std::string& path) {
 FILE* f=std::fopen(path.c_str(),"r");if(!f)return false;
 char name[128],action[128];unsigned value;int ticks,interval;double diff;
 if(std::fscanf(f,"%127s %u %lf %d %d",name,&value,&diff,&ticks,&interval)!=5){std::fclose(f);return false;}
 caseId=name;seed=value;difficulty=diff;total=ticks;every=interval;events.clear();
 int t,id;while(std::fscanf(f,"%d %127s %d",&t,action,&id)==3)events.push_back({t,action,id});
 std::fclose(f);return total>0&&total<=10000000&&every>0&&every<=total;
}
void Replay::apply(Game& g,int tick) const {
 for(auto& e:events)if(e.tick==tick) {
  if(e.action=="duel" || e.action=="turretTest" || e.action=="specialTest" || e.action=="victoryTest") {
   g.units.clear();g.turrets.clear();g.projectiles.clear();g.effects.clear();g.sounds.clear();g.pop[0]=g.pop[1]=0;g.queue.clear();g.building=0;g.buildTime=g.buildMax=0;g.specialTimer=60;g.result=0;g.money=g.xp=500000;g.nextAnim=1;
   for(int i=0;i<2;i++)g.hp[i]=g.maxHP[i]=100000;
   if(e.action=="victoryTest"){g.hp[1]=10;g.createUnit(29,1);g.pop[0]=2;g.units.back().x=550;}
   else if(e.action=="duel") {g.createUnit(e.id,1);g.pop[0]=int(unitDefs[e.id-1].pop);g.units.back().x=-150;g.createUnit(e.id,2);g.pop[1]=int(unitDefs[e.id-1].pop);g.units.back().x=150;}
   else {g.createUnit(29,2);g.pop[1]=2;g.units.back().x=e.action=="turretTest"?-400:0;g.units.back().hp=g.units.back().maxHP=100000;if(e.action=="turretTest")g.createTurret(e.id,1,1);else g.createSpecial(1,e.id);}
  }
  else if(e.action=="queue")g.queueUnit(e.id);else if(e.action=="tower")g.addTower();
  else if(e.action=="turret")g.buyTurret(e.id);else if(e.action=="place")g.placeTurret(e.id);
  else if(e.action=="upgrade")g.buyUpgrade(e.id);else if(e.action=="special")g.buySpecial(e.id);
  else if(e.action=="evolve")g.buyEvolve();else if(e.action=="sell")g.sellTurret(1,e.id);else if(e.action=="cancel")g.cancelTurret();
 }
}
std::string sampleRow(const Game& g,int tick) {
 std::ostringstream s;s<<"AOWQA|"<<tick<<'|'<<g.money<<'|'<<g.xp<<'|'<<g.pop[0]<<'|'<<g.pop[1]<<'|'<<g.hp[0]<<'|'<<g.hp[1]<<'|'<<g.age[0]<<'|'<<g.age[1]<<'|'<<g.rngState<<'|'<<g.rngCalls<<'|';
 s<<g.building<<','<<std::fixed<<std::setprecision(6)<<g.buildTime<<','<<g.specialTimer<<',';
 for(size_t i=0;i<g.queue.size();i++){if(i)s<<':';s<<g.queue[i];}s<<'|';
 for(size_t i=0;i<g.units.size();i++) {
  const auto& u=g.units[i];if(i)s<<';';
  s<<u.id<<','<<u.side<<','<<u.x<<','<<std::setprecision(0)<<u.hp<<','<<u.state<<','<<u.animation.label<<','<<u.animation.frame<<std::setprecision(6);
 }s<<'|';
 bool first=true;for(auto& t:g.turrets)if(t.alive){if(!first)s<<';';first=false;s<<t.id<<','<<t.side<<','<<t.spot<<','<<t.animation.frame;}
 s<<'|'<<g.projectiles.size()<<'|'<<g.effects.size()<<'|';
 for(size_t i=0;i<g.projectiles.size();++i){auto& p=g.projectiles[i];if(i)s<<';';s<<p.name<<','<<p.side<<','<<std::setprecision(6)<<p.x<<','<<p.y<<','<<std::setprecision(9)<<p.xs<<','<<p.ys;}
 return s.str();
}
}
