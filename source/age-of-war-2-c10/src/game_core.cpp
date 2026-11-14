#include "game.hpp"
#include <cmath>
#include <algorithm>
namespace aow {
namespace {
const Label* findLabel(const std::string& key,const std::string& name) {
 const auto* c=clip(key);
 if(c) for(const auto& l:c->labels) if(name==l.name) return &l;
 return nullptr;
}
}
bool hasLabel(const std::string& key,const std::string& label) { return findLabel(key,label)!=nullptr; }
bool play(Anim& a,const std::string& key,const std::string& label,bool loop) {
 const auto* l=findLabel(key,label);
 if(!l) return false;
 a.key=key; a.label=label; a.time=0; a.frame=l->start; a.loop=loop; a.stopped=false;
 return true;
}
bool advance(Anim& a,double dt) {
 if(a.stopped) return false;
 const auto* l=findLabel(a.key,a.label);
 if(!l || l->length<=0) return false;
 a.time+=dt*30.0;
 if(!a.loop && a.time>=l->length) {
  a.time=l->length-1; a.frame=l->start+l->length-1; a.stopped=true;
  return true; // Caller dispatches the completion callback.
 }
 if(a.loop) a.time=std::fmod(a.time,double(l->length));
 a.frame=l->start+int(a.time);
 return false;
}
double Game::random() {
 rngState=rngState*1664525u+1013904223u; ++rngCalls;
 return double(rngState)/4294967296.0;
}
double Game::bonus(const std::string& name,int side) const {
 if(side!=1 && side!=2) return 0;
 for(unsigned i=0;i<upgradeDefs.size() && i<18;++i) {
  const auto& d=upgradeDefs[i];
  if(name!=d.name) continue;
  int level=upgrades[side-1][i];
  if(level>0 && unsigned(level)<d.levels.size()) return d.levels[level].bonus;
  return 0;
 }
 return 0;
}
int Game::damage(const std::string& attacker,const std::string& victim,double lo,double hi,int side) {
 double b=0;
 if(attacker=="Support") b=bonus("Support Damage",side);
 else if(attacker=="Infantry") b=bonus("Infantry Damage",side);
 else if(attacker=="Anti-Armor") b=bonus("Anti-Armor Damage",side);
 else if(attacker=="Armored") b=bonus("Armored Damage",side);
 // AS parameters are int; preserve conversion before interpolation.
 double low=int(lo),high=int(hi);
 double value=std::floor((low+random()*(high-low))*(1+b)+0.5);
 if((attacker=="Support" && victim=="Anti-Armor") ||
    (attacker=="Infantry" && victim=="Support") ||
    (attacker=="Anti-Armor" && victim=="Armored") ||
    (attacker=="Armored" && victim=="Infantry")) value*=1.5;
 return int(std::ceil(value));
}
void Game::emitSound(const std::string& list,double x) {
 if(list.empty()) return;
 std::vector<std::string> names;
 std::size_t begin=0,end;
 do {
  end=list.find(',',begin);
  names.push_back(list.substr(begin,end==std::string::npos?end:end-begin));
  if(end==std::string::npos) break;
  begin=end+1;
 } while(true);
 auto index=std::size_t(std::floor(random()*double(names.size()-1)+0.5));
 sounds.push_back({names[index],x});
}
void Game::damageBase(int side,int amount) {
 if(side!=1 && side!=2) return;
 const int target=2-side; // side identifies attacker, not damaged base.
 hp[target]-=amount;
 if(showDamage) {
  createEffect("damage",target+1,target==0?-580:580,-140);
  effects.back().value=amount;
 }
 if(hp[target]<=0) { hp[target]=0; result=side==1?1:2; }
}
void Game::reset(double difficultyValue,uint32_t seed) {
 *this=Game{};
 difficulty=difficultyValue; rngState=seed;
 money=startMoney; xp=startXP; age[0]=startAge; age[1]=startAgeCPU;
 hp[0]=hp[1]=maxHP[0]=maxHP[1]=baseHealth[0];
 specialTimer=60; tutorial=true; paused=true;
 loadAI();
}
void Game::update(double dt) {
 if(paused || result) return;
 ++ticks; elapsed+=dt;
 updateEconomy(dt);
 animateAll(dt);
 updateUnits(dt); updateTurrets(dt);
 updateProjectiles(dt); updateEffects(dt);
 if(aiEnabled) updateAI(dt);
}
void Game::animateAll(double dt) {
 // Capture registration membership before any completion callback can append.
 const std::size_t nu=units.size(),nt=turrets.size(),ne=effects.size();
 auto dispatch=[&](int kind,std::size_t i) {
  if(kind==0) {if(advance(units[i].animation,dt))unitAnimEnd(units[i]);}
  if(kind==1 && advance(turrets[i].animation,dt)) {
   if(turrets[i].animation.label=="shoot") {
    turretHit(turrets[i]);
    // Reacquire by index after the callback, never retain an entity reference.
    play(turrets[i].animation,turretClip(turrets[i].id),"shootLaunch",false);
   } else if(turrets[i].animation.label=="shootLaunch") {
    turrets[i].shooting=false;turrets[i].state="idle";
   }
  }
  if(kind==2 && advance(effects[i].animation,dt) && !effects[i].animation.loop)effects[i].alive=false;
 };
 bool monotonic=true;
 for(std::size_t i=1;i<nu;++i)if(units[i-1].animId>=units[i].animId)monotonic=false;
 for(std::size_t i=1;i<nt;++i)if(turrets[i-1].animId>=turrets[i].animId)monotonic=false;
 bool haveEffect=false;int previousEffect=0;
 for(std::size_t i=0;i<ne;++i)if(effects[i].animId) {
  if(haveEffect && previousEffect>=effects[i].animId)monotonic=false;
  previousEffect=effects[i].animId;haveEffect=true;
 }
 if(!monotonic) {
  // Preserve the old sort path for unexpected nonmonotonic lists.
  struct Entry {int id,kind;std::size_t index;};std::vector<Entry> order;
  for(std::size_t i=0;i<nu;++i)order.push_back({units[i].animId,0,i});
  for(std::size_t i=0;i<nt;++i)order.push_back({turrets[i].animId,1,i});
  for(std::size_t i=0;i<ne;++i)if(effects[i].animId)order.push_back({effects[i].animId,2,i});
  std::sort(order.begin(),order.end(),[](const Entry& a,const Entry& b){return a.id<b.id;});
  for(const auto& entry:order)dispatch(entry.kind,entry.index);
  return;
 }
 // Source-valid lists are individually ordered by unique global animId.
 // Only indices survive callbacks; newly appended entries wait until next pass.
 std::size_t u=0,t=0,e=0;
 while(u<nu || t<nt || e<ne) {
  while(e<ne && !effects[e].animId)++e;
  int kind=-1,id=0;std::size_t index=0;
  if(u<nu) {kind=0;id=units[u].animId;index=u;}
  if(t<nt && (kind<0 || turrets[t].animId<id)) {kind=1;id=turrets[t].animId;index=t;}
  if(e<ne && (kind<0 || effects[e].animId<id)) {kind=2;index=e;}
  if(kind<0)break;
  if(kind==0)++u;
  else if(kind==1)++t;
  else ++e;
  dispatch(kind,index);
 }
}
}
