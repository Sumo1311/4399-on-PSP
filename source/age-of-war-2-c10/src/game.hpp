#pragma once
#include "source_data.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
namespace aow {
constexpr double LOGIC_DT=1.0/150.0;
inline double displayPixel(double x) {return std::trunc(x*20)/20;}
struct Anim { std::string key,label; double time=0; int frame=0; bool loop=true,stopped=true; };
bool play(Anim&,const std::string& key,const std::string& label,bool loop);
bool advance(Anim&,double dt);
bool hasLabel(const std::string& key,const std::string& label);
struct Unit {
 int serial=0,animId=0,id=1,side=1; double x=0,y=0,hp=0,maxHP=0,speed=0,originalSpeed=0,range=0,w=0,h=0,wait=0.2,reward=0;
 bool alive=true,dead=false,stopped=false,canWalk=false,baseMelee=false,baseRanged=false,unitMelee=false,unitRange=false,onMelee=false;
 std::string state="idle"; Anim animation;
};
struct Turret { int animId=0,id=1,side=1,spot=1; double x=0,y=0,rotation=0,range=0; bool alive=true,shooting=false; std::string state; Anim animation; };
struct Projectile { std::string name,type; int side=1; double x=0,y=0,xs=0,ys=0,rotation=0,rotSpeed=0,mass=0,scale=1,explosion=0,lo=0,hi=0; bool alive=true,follow=false; };
struct Effect { std::string kind; int animId=0,side=1,age=1,count=0; double x=0,y=0,xs=0,ys=0,rotation=0,mass=0,scale=1,time=0,delay=0,life=0,alpha=1,value=0; bool alive=true; Anim animation; };
struct Sound { std::string name; double x; };
class Game {
public:
 int money=0,xp=0,age[2]={1,1},hp[2]={1000,1000},maxHP[2]={1000,1000},pop[2]={0,0},spots[2]={0,0};
 int upgrades[2][18]{}; std::vector<int> queue; int building=0; double buildTime=0,buildMax=0,specialTimer=60;
 int pendingTurret=0,pendingRefund=0; bool selling=false,paused=false,tutorial=false,showDamage=false; int result=0,speed=1;
 double difficulty=1,camera=800,elapsed=0; uint64_t ticks=0,rngCalls=0; uint32_t rngState=1; int nextSerial=1,nextAnim=1;
 int aiEvent=0; double aiTime=0,aiUnitTime=0,aiMin=0,aiMax=0; bool aiEnabled=true; std::vector<AIChoice> aiUnits;
 std::vector<Unit> units; std::vector<Turret> turrets; std::vector<Projectile> projectiles; std::vector<Effect> effects; std::vector<Sound> sounds;
 std::string error;
 void reset(double difficultyValue=1,uint32_t seed=1);
 double random(); double bonus(const std::string& name,int side) const;
 int damage(const std::string& attacker,const std::string& victim,double lo,double hi,int side);
 void emitSound(const std::string& list,double x);
 void update(double dt=LOGIC_DT);
 bool queueUnit(int id); bool unitAvailable(int id) const; bool buyUpgrade(int id); bool addTower(int side=1);
 bool buyTurret(int id); bool placeTurret(int spot); void cancelTurret(); bool sellTurret(int side,int spot);
 bool buySpecial(int id); bool buyEvolve(); void evolve(int side);
 void updateEconomy(double dt); void updateAI(double dt); void nextAIEvent(); void loadAI();
 void createUnit(int id,int side); void createTurret(int id,int side,int spot);
 void animateAll(double dt); void animateUnits(double dt); void updateUnits(double dt); void unitAnimEnd(Unit&); void checkUnitAnim(Unit&);
 Unit* nearest(double x,int side,bool enemyOnly,int ignoreSerial=0); void meleeHit(Unit&); void rangedHit(Unit&);
 void animateTurrets(double dt); void updateTurrets(double dt); void turretHit(Turret&);
 void createProjectile(const std::string& name,int side,double x,double y,double rotation,double velocity,double lo,double hi,const std::string& type);
 void updateProjectiles(double dt); void damageBase(int side,int amount);
 void createSpecial(int side,int age); void createEffect(const std::string& kind,int side,double x,double y,double scale=1);
 void updateEffects(double dt);
 bool save(const std::string& path) const; bool load(const std::string& path);
};
std::string turretClip(int id);
}
