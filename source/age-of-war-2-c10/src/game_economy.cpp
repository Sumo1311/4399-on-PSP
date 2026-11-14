#include "game.hpp"
#include <algorithm>

namespace aow {
namespace {
template<class T> bool valid(int id,const std::vector<T>& defs) {
 return id>0 && static_cast<std::size_t>(id)<=defs.size();
}
}
bool Game::unitAvailable(int id) const {
 if(!valid(id,unitDefs)) return false;
 // Hud.updateUnitButtons checks ascending IDs, resetting unavailableUnits on
 // EVERY check. Only the last visible ID survives in the purchase lock list.
 const int last=age[0]==7?29:age[0]*4;
 if(id!=last) return true;
 const std::string need=unitDefs[id-1].needUpgrade;
 if(need=="none") return true;
 for(std::size_t i=0;i<upgradeDefs.size() && i<18;++i)
  if(need==upgradeDefs[i].name && upgrades[0][i]==0) return false;
 return true;
}
bool Game::queueUnit(int id) {
 if(!valid(id,unitDefs)) return false;
 if(!unitAvailable(id)) { error="单位锁定"; return false; }
 const auto& d=unitDefs[id-1];
 bool ok=true;
 if(d.cost>money) { error="没有足够的金币"; ok=false; }
 if(d.pop+pop[0]>10) { error="没有足够的人口房间"; ok=false; }
 if(std::count(queue.begin(),queue.end(),id)>4) {
  error="单位队列已满"; ok=false;
 }
 if(!ok) return false;
 queue.push_back(id);
 money=static_cast<int>(money-d.cost);
 pop[0]=static_cast<int>(pop[0]+d.pop);
 return true;
}
bool Game::buyUpgrade(int id) {
 if(!valid(id,upgradeDefs) || id>18) {
  error="不能购买没有数据的升级"; return false;
 }
 const auto& d=upgradeDefs[id-1];
 const int level=upgrades[0][id-1];
 if(level<0 || level+1>=static_cast<int>(d.levels.size())) {
  error="升级达到最高等级"; return false;
 }
 const double cost=d.levels[level].cost;
 if(money<cost) { error="没有足够的金钱"; return false; }
 money=static_cast<int>(money-cost);
 ++upgrades[0][id-1];
 return true;
}
bool Game::addTower(int side) {
 if(side!=1 && side!=2) return false;
 int& n=spots[side-1];
 if(n<0 || n>=4) return false;
 if(side==1) {
  if(static_cast<std::size_t>(n)>=towers.size()) return false;
  const double cost=towers[n].cost;
  if(money<cost) { error="没有足够的金币"; return false; }
  money=static_cast<int>(money-cost);
 }
 ++n;
 return true;
}
bool Game::buyTurret(int id) {
 if(!valid(id,turretDefs)) { error="不能购买没有数据的炮塔"; return false; }
 const auto& d=turretDefs[id-1];
 int count=0;
 for(const auto& t:turrets) if(t.alive && t.side==1) ++count;
 bool ok=true;
 if(count>=spots[0]) { error="没有更多的炮塔位置"; ok=false; }
 if(money<d.cost) { error="没有足够的金币"; ok=false; }
 if(!ok) return false;
 money-=static_cast<int>(d.cost);
 pendingTurret=id;
 pendingRefund=static_cast<int>(d.cost);
 return true;
}
bool Game::placeTurret(int spot) {
 if(!valid(pendingTurret,turretDefs) || spot<1 || spot>spots[0] || spot>4) return false;
 for(const auto& t:turrets) if(t.alive && t.side==1 && t.spot==spot) return false;
 createTurret(pendingTurret,1,spot);
 pendingTurret=0;
 pendingRefund=0;
 return true;
}
void Game::cancelTurret() {
 if(pendingTurret!=0) {
  money+=pendingRefund;
  pendingTurret=0;
  pendingRefund=0;
 }
 selling=false;
}
bool Game::sellTurret(int side,int spot) {
 if(side!=1 && side!=2) return false;
 for(auto& t:turrets) {
  if(!t.alive || t.side!=side || t.spot!=spot) continue;
  if(!valid(t.id,turretDefs)) return false;
  t.alive=false;
  if(side==1) {
   money+=static_cast<int>(turretDefs[t.id-1].cost/2);
   selling=false;
  }
  return true;
 }
 return false;
}
bool Game::buySpecial(int id) {
 if(!valid(id,specialDefs)) return false;
 // Visibility is a HUD concern, not an extra age restriction here.
 if(specialTimer!=0) { error="特殊攻击不可用"; return false; }
 const double cost=specialDefs[id-1].cost;
 if(xp<cost) { error="没有足够的 XP"; return false; }
 xp=static_cast<int>(xp-cost);
 createSpecial(1,id);
 specialTimer=60;
 return true;
}
bool Game::buyEvolve() {
 if(age[0]>=7 || !valid(age[0],evolutions)) return false;
 const double cost=evolutions[age[0]-1].cost;
 if(xp<cost) { error="没有足够的 XP"; return false; }
 xp=static_cast<int>(xp-cost);
 evolve(1);
 return true;
}
void Game::evolve(int side) {
 if(side!=1 && side!=2) return;
 const int s=side-1;
 if(age[s]<1 || age[s]>=7) return;
 ++age[s];
 const int old=maxHP[s];
 maxHP[s]=baseHealth[age[s]-1];
 hp[s]+=maxHP[s]-old;
}
void Game::updateEconomy(double dt) {
 if(paused) return;
 if(specialTimer>0) specialTimer-=dt;
 else if(specialTimer!=0) specialTimer=0;
 if(building==0 && !queue.empty()) {
  building=queue.front();
  buildTime=unitDefs[building-1].creation;
  buildMax=buildTime;
  queue.erase(queue.begin());
 }
 if(building!=0) {
  if(buildTime>0) buildTime-=dt;
  else {
   createUnit(building,1);
   building=0;
  }
 }
}
}
