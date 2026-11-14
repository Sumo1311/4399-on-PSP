#pragma once
#include <string>
#include <vector>
namespace aow {
struct UnitDef { const char *name,*type,*needUpgrade,*projectile,*meleeSounds,*rangedSounds,*deathSounds; double cost,pop,health,speed,range,w,h,creation,scale,lo,hi,shoot; };
struct TurretDef { const char *name,*projectile,*sounds; double cost,range,lo,hi,scale,shoot; };
struct ProjectileDef { const char *name,*rotation,*sounds; double scale,mass,explosion; };
struct LevelDef { const char *name,*desc; double cost,bonus; };
struct UpgradeDef { const char *name; std::vector<LevelDef> levels; };
struct PriceDef { const char *name,*desc; double cost; };
struct AIChoice { int id; double ratio; };
struct AIAction { const char *kind; int id,spot; };
struct AIEvent { double duration,minimum,maximum; std::vector<AIChoice> units; std::vector<AIAction> sell,build; };
struct Label { const char *name; int start,length; };
struct PointFrame { double x,y,rotation,velocity; bool present; };
struct ClipDef { const char *key,*movie; int symbol,frames; std::vector<Label> labels; std::vector<PointFrame> points,points2; };
extern const std::vector<UnitDef> unitDefs;
extern const std::vector<TurretDef> turretDefs;
extern const std::vector<ProjectileDef> projectileDefs;
extern const std::vector<UpgradeDef> upgradeDefs;
extern const std::vector<PriceDef> towers,evolutions,specialDefs;
extern const std::vector<std::vector<AIEvent>> aiDefs;
extern const std::vector<ClipDef> clipDefs;
extern const int baseHealth[7];
extern const int startMoney,startXP,startAge,startAgeCPU;
const ClipDef* clip(const std::string& key);
}
