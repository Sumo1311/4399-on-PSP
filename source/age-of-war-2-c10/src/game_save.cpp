#include "game.hpp"
#include <zlib.h>
#include <type_traits>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <cmath>
namespace aow {
namespace {
// Native suspend extension. The supplied Flash has no battle save implementation.
const char identity[]="efd4e5d6138484f8f496a09cc1c117e2a26138fe66ffdafc18f565775ed44b5e";
struct Codec {
 std::vector<unsigned char> b;size_t pos=0;bool reading=false;
 template<class... T>void operator()(T&... x){(one(x),...);}
 template<class T,std::enable_if_t<std::is_arithmetic<T>::value,int> =0>void one(T& x) {
  if(reading){if(pos+sizeof(T)>b.size())throw std::runtime_error("save EOF");std::memcpy(&x,b.data()+pos,sizeof(T));pos+=sizeof(T);}
  else {const auto* p=reinterpret_cast<const unsigned char*>(&x);b.insert(b.end(),p,p+sizeof(T));}
 }
 void one(std::string& s) {uint32_t n=s.size();one(n);if(n>4096)throw std::runtime_error("save string");if(reading){if(pos+n>b.size())throw std::runtime_error("save EOF");s.assign((char*)b.data()+pos,n);pos+=n;}else b.insert(b.end(),s.begin(),s.end());}
 template<class T>void one(std::vector<T>& v) {uint32_t n=v.size();one(n);if(n>20000)throw std::runtime_error("save count");if(reading)v.resize(n);for(auto& x:v)one(x);}
 void one(Anim& a){(*this)(a.key,a.label,a.time,a.frame,a.loop,a.stopped);}
 void one(Unit& u){(*this)(u.serial,u.animId,u.id,u.side,u.x,u.y,u.hp,u.maxHP,u.speed,u.originalSpeed,u.range,u.w,u.h,u.wait,u.reward,u.alive,u.dead,u.stopped,u.canWalk,u.baseMelee,u.baseRanged,u.unitMelee,u.unitRange,u.onMelee,u.state,u.animation);}
 void one(Turret& t){(*this)(t.animId,t.id,t.side,t.spot,t.x,t.y,t.rotation,t.range,t.alive,t.shooting,t.state,t.animation);}
 void one(Projectile& p){(*this)(p.name,p.type,p.side,p.x,p.y,p.xs,p.ys,p.rotation,p.rotSpeed,p.mass,p.scale,p.explosion,p.lo,p.hi,p.alive,p.follow);}
 void one(Effect& e){(*this)(e.kind,e.animId,e.side,e.age,e.count,e.x,e.y,e.xs,e.ys,e.rotation,e.mass,e.scale,e.time,e.delay,e.life,e.alpha,e.value,e.alive,e.animation);}
 void one(AIChoice& c){(*this)(c.id,c.ratio);}
 void game(Game& g){
  (*this)(g.money,g.xp);for(int i=0;i<2;i++){(*this)(g.age[i],g.hp[i],g.maxHP[i],g.pop[i],g.spots[i]);for(auto& u:g.upgrades[i])one(u);}
  (*this)(g.queue,g.building,g.buildTime,g.buildMax,g.specialTimer,g.pendingTurret,g.pendingRefund,g.selling,g.paused,g.tutorial,g.showDamage,g.result,g.speed,g.difficulty,g.camera,g.elapsed,g.ticks,g.rngCalls,g.rngState,g.nextSerial,g.nextAnim,g.aiEvent,g.aiTime,g.aiUnitTime,g.aiMin,g.aiMax,g.aiEnabled,g.aiUnits,g.units,g.turrets,g.projectiles,g.effects);
 }
};
bool valid(const Game& g){
 for(int i=0;i<2;i++)if(g.age[i]<1||g.age[i]>7||g.spots[i]<0||g.spots[i]>4||g.maxHP[i]<=0)return false;
 if(!std::isfinite(g.elapsed)||g.elapsed<0||!std::isfinite(g.difficulty)||g.difficulty<=0||g.nextAnim<1||g.nextSerial<1)return false;
 for(auto& u:g.units)if(u.id<1||u.id>29||u.side<1||u.side>2||!std::isfinite(u.x)||!std::isfinite(u.hp))return false;
 for(auto& t:g.turrets)if(t.id<1||t.id>17||t.side<1||t.side>2||t.spot<1||t.spot>4)return false;
 for(auto id:g.queue)if(id<1||id>29)return false;
 return g.building>=0&&g.building<=29&&g.pendingTurret>=0&&g.pendingTurret<=17;
}
}
bool Game::save(const std::string& path) const {
 try {
  Codec c;c.game(const_cast<Game&>(*this));if(c.b.size()>4*1024*1024)return false;
  uint32_t n=c.b.size(),crc=crc32(0,c.b.data(),n);std::string tmp=path+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
  bool ok=std::fwrite("AOW2PSP2",1,8,f)==8&&std::fwrite(identity,1,64,f)==64&&std::fwrite(&n,4,1,f)==1&&std::fwrite(&crc,4,1,f)==1&&std::fwrite(c.b.data(),1,n,f)==n;
  if(std::fflush(f)!=0)ok=false;if(std::fclose(f)!=0)ok=false;
  if(!ok){std::remove(tmp.c_str());return false;}
  if(std::rename(tmp.c_str(),path.c_str())!=0){std::remove(tmp.c_str());return false;}return true;
 }catch(...){return false;}
}
bool Game::load(const std::string& path) {
 try {
  FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;
  char magic[8],source[64];uint32_t n=0,crc=0;
  bool ok=std::fread(magic,1,8,f)==8&&std::memcmp(magic,"AOW2PSP2",8)==0&&std::fread(source,1,64,f)==64&&std::memcmp(source,identity,64)==0&&std::fread(&n,4,1,f)==1&&std::fread(&crc,4,1,f)==1&&n<=4*1024*1024;
  if(!ok){std::fclose(f);return false;}
  Codec c;c.reading=true;c.b.resize(n);ok=std::fread(c.b.data(),1,n,f)==n&&std::fgetc(f)==EOF;std::fclose(f);
  if(!ok||crc32(0,c.b.data(),n)!=crc)return false;
  Game restored;c.game(restored);if(c.pos!=c.b.size()||!valid(restored))return false;
  restored.sounds.clear();*this=std::move(restored);return true;
 }catch(...){return false;}
}
}
