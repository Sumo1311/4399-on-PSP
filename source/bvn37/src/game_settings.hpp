// Adapted from ConfigVO, SetBtnGroup and FighterKeyCtrl.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fighter_motion.hpp"
#include <array>
#include <cstdint>
#include <span>
namespace bvn {
enum class HumanKey : unsigned { Up, Down, Left, Right, Attack, Jump, Dash, Skill, Super, Assist, Count };
inline constexpr unsigned HumanKeyCount=unsigned(HumanKey::Count);
inline constexpr std::array<std::uint16_t,HumanKeyCount> DefaultButtons{0x10,0x40,0x80,0x20,0x8000,0x4000,0x100,0x2000,0x200,0x1000};
struct KeyBindings {
 std::array<std::uint16_t,HumanKeyCount> buttons=DefaultButtons;
 bool valid()const;
 unsigned logical(unsigned physical)const;
 bool operator==(const KeyBindings&)const=default;
};
enum class InputMode : unsigned { Standard, Classic };
enum class GameQuality : unsigned { Low, Medium, High, Best };
struct GameSettings {
 unsigned difficulty=1,mode=1,life=1,time=1,sound=4,music=4,quality=0,showHp=0;
 std::array<KeyBindings,2> keys{};
 inline static constexpr unsigned LifePercent[]{50,100,200,300,500};
 inline static constexpr int Seconds[]{30,60,90,-1};
 inline static constexpr unsigned VolumePercent[]{0,10,30,50,70,100};
 bool valid()const;
 bool change(unsigned row,int direction);
 unsigned gameHz(bool wlan=false)const{return !wlan&&quality?60:30;}
 bool classic(bool wlan=false)const{return wlan||mode==1;}
 unsigned shineHz()const{return quality==0?15:quality==3?60:30;}
 unsigned soundPercent()const{return VolumePercent[sound];}
 unsigned musicPercent()const{return VolumePercent[music];}
 double healthScale()const{return LifePercent[life]/100.0;}
 int seconds()const{return Seconds[time];}
 unsigned wireRules()const{return life|(time<<3);}
 bool applyWireRules(unsigned value);
 bool operator==(const GameSettings&)const=default;
};
// Two independent journal slots survive interruption; settings do not share
// the Musou progress files or mutate installed assets.
class SettingsStore {
public:
 bool load(const char* folder,GameSettings& settings);
 bool save(const char* folder,const GameSettings& settings);
 std::uint32_t serial()const{return serial_;}
private:std::uint32_t serial_=0;
};
struct HumanRequests {
 unsigned held=0,buffered=0,intents=0;
 bool transform=false,quickJump=false;
    unsigned pressed=0;
};
class HumanInput {
public:
 void clear(){delay_={};previous_=0;}
 HumanRequests sample(unsigned held,unsigned pressed,bool classic);
 FighterControls controls(unsigned held,unsigned pressed,bool classic);
 static FighterControls controlsForFrame(unsigned held,unsigned buffered,unsigned pressed,bool classic);
private:
 static HumanRequests requests(unsigned held,unsigned buffered,unsigned pressed,bool classic);
 static FighterControls makeControls(const HumanRequests& request,bool classic);
 std::array<unsigned,HumanKeyCount> delay_{};
 unsigned previous_=0;
};
}
