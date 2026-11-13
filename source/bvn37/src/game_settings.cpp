// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_settings.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>
namespace bvn {
bool KeyBindings::valid()const{
 return std::ranges::all_of(buttons,[](unsigned b){return std::ranges::find(DefaultButtons,b)!=DefaultButtons.end();});
}
unsigned KeyBindings::logical(unsigned physical)const{
 unsigned result=0;for(unsigned i=0;i<buttons.size();++i)if(physical&buttons[i])result|=1U<<i;return result;
}
bool GameSettings::valid()const{
 return difficulty>=1&&difficulty<=6&&mode<2&&life<5&&time<4&&sound<6&&music<6&&quality<4&&showHp<2&&keys[0].valid()&&keys[1].valid();
}
bool GameSettings::change(unsigned row,int direction){
 if(!direction)return false;
 unsigned* values[]{&difficulty,&mode,&life,&time,&sound,&music,&quality,&showHp};
 constexpr unsigned counts[]{6,2,5,4,6,6,4,2};if(row>=std::size(values))return false;
 auto& value=*values[row];const unsigned origin=row?0:1;value=origin+(value-origin+counts[row]+(direction>0?1:-1))%counts[row];return true;
}
bool GameSettings::applyWireRules(unsigned value){
 if(value&~31U||(value&7)>=5||(value>>3)>=4)return false;
 life=value&7;time=value>>3;return true;
}
namespace {
constexpr unsigned SaveSize=72,Version=1;
void put(std::vector<unsigned char>& data,unsigned at,std::uint32_t value,unsigned bytes){for(unsigned i=0;i<bytes;++i)data[at+i]=value>>(8*i);}
unsigned get(std::span<const unsigned char> data,unsigned at,unsigned bytes){unsigned n=0;for(unsigned i=0;i<bytes;++i)n|=unsigned(data[at+i])<<(8*i);return n;}
std::string slot(const char* folder,unsigned n){return std::string(folder)+"/SETTINGS"+char('0'+n)+".DAT";}
bool readSlot(const char* folder,unsigned n,GameSettings& out,std::uint32_t& serial){
 FILE* file=std::fopen(slot(folder,n).c_str(),"rb");if(!file)return false;std::vector<unsigned char> data(SaveSize);
 const bool read=std::fread(data.data(),1,data.size(),file)==data.size()&&std::fgetc(file)==EOF&&!std::ferror(file);std::fclose(file);
 if(!read||std::memcmp(data.data(),"BVNSET01",8)||get(data,8,4)!=Version||get(data,12,4)!=SaveSize||get(data,68,4)!=crc32(0,data.data(),68))return false;
 serial=get(data,16,4);if(!serial||(serial&1)!=n)return false;
 unsigned* fields[]{&out.difficulty,&out.mode,&out.life,&out.time,&out.sound,&out.music,&out.quality,&out.showHp};
 for(unsigned i=0;i<std::size(fields);++i)*fields[i]=data[20+i];
 for(unsigned p=0;p<2;++p)for(unsigned i=0;i<HumanKeyCount;++i)out.keys[p].buttons[i]=get(data,28+20*p+2*i,2);
 return out.valid();
}
}
bool SettingsStore::load(const char* folder,GameSettings& settings){
 GameSettings best;std::uint32_t newest=0;
 for(unsigned n=0;n<2;++n){GameSettings next;std::uint32_t seq;if(readSlot(folder,n,next,seq)&&(!newest||std::int32_t(seq-newest)>0)){best=next;newest=seq;}}
 serial_=newest;if(!newest)return false;settings=best;return true;
}
bool SettingsStore::save(const char* folder,const GameSettings& settings){
 if(!settings.valid())return false;
 auto next=serial_+1;if(!next)next=2;
 std::vector<unsigned char> data(SaveSize);std::memcpy(data.data(),"BVNSET01",8);put(data,8,Version,4);put(data,12,SaveSize,4);put(data,16,next,4);
 const unsigned values[]{settings.difficulty,settings.mode,settings.life,settings.time,settings.sound,settings.music,settings.quality,settings.showHp};
 for(unsigned i=0;i<std::size(values);++i)data[20+i]=values[i];
 for(unsigned p=0;p<2;++p)for(unsigned i=0;i<HumanKeyCount;++i)put(data,28+20*p+2*i,settings.keys[p].buttons[i],2);
 put(data,68,crc32(0,data.data(),68),4);
 FILE* file=std::fopen(slot(folder,next&1).c_str(),"wb");if(!file)return false;
 const bool wrote=std::fwrite(data.data(),1,data.size(),file)==data.size()&&std::fflush(file)==0;const bool closed=std::fclose(file)==0;
 GameSettings check;std::uint32_t checkSerial;if(!wrote||!closed||!readSlot(folder,next&1,check,checkSerial)||checkSerial!=next||check!=settings)return false;
 serial_=next;return true;
}
HumanRequests HumanInput::sample(unsigned held,unsigned pressed,bool classic){
 // A network frame may retain a completed tap after the physical key is up.
 // Present it as one down frame so Classic's isDown checks also see the tap.
 held=(held|pressed)&((1U<<HumanKeyCount)-1);pressed=(pressed|(held&~previous_))&((1U<<HumanKeyCount)-1);previous_=held;
 unsigned buffered=0;
 for(unsigned i=0;i<HumanKeyCount;++i){if(pressed&(1U<<i))delay_[i]=LogicHz/10;else if(delay_[i])--delay_[i];if(delay_[i])buffered|=1U<<i;}
 return requests(held,buffered,pressed,classic);
}
HumanRequests HumanInput::requests(unsigned held,unsigned buffered,unsigned pressed,bool classic){
 const auto down=[&](HumanKey key){return bool(held&(1U<<unsigned(key)));};
 const auto key=[&](HumanKey k){return bool((classic?held:buffered)&(1U<<unsigned(k)));};
 unsigned mask=0;const auto add=[&](AiIntent i,bool value){if(value)mask|=1U<<unsigned(i);};
 const bool up=down(HumanKey::Up),low=down(HumanKey::Down),attack=down(HumanKey::Attack),jump=down(HumanKey::Jump);
 const bool a=key(HumanKey::Attack),j=key(HumanKey::Jump),d=key(HumanKey::Dash),s=key(HumanKey::Skill),u=key(HumanKey::Super);
 add(AiIntent::Left,down(HumanKey::Left)&&!down(HumanKey::Right));add(AiIntent::Right,down(HumanKey::Right)&&!down(HumanKey::Left));
 add(AiIntent::Guard,low);add(AiIntent::Attack,a&&!up&&!low&&!jump);add(AiIntent::Jump,j&&!attack);add(AiIntent::JumpDown,low&&j);
 add(AiIntent::Dash,d&&!low);add(AiIntent::SkillDown,low&&a);add(AiIntent::SkillUp,up&&a);
 add(AiIntent::Special,s&&!up&&!low);add(AiIntent::SpecialDown,low&&s);add(AiIntent::SpecialUp,up&&s);
 const bool horizontal=down(HumanKey::Left)||down(HumanKey::Right);
 add(AiIntent::CatchAttack,horizontal&&bool(buffered&(1U<<unsigned(HumanKey::Attack))));
 add(AiIntent::CatchSpecial,horizontal&&bool(buffered&(1U<<unsigned(HumanKey::Skill))));
 add(AiIntent::Super,u&&!up&&!low);add(AiIntent::SuperUp,u&&up);add(AiIntent::Ultimate,u&&low);
 add(AiIntent::AirAttack,a);add(AiIntent::AirSpecial,s);add(AiIntent::AirSuper,u);
 add(AiIntent::Assist,down(HumanKey::Assist));add(AiIntent::Counter,down(HumanKey::Assist));
 add(AiIntent::GhostStep,d&&low);add(AiIntent::GhostJump,d&&up);add(AiIntent::GhostJumpDown,d&&low);add(AiIntent::DownJump,d);
 return {held,buffered,mask,attack&&jump,!classic&&j&&!attack,pressed};
}
FighterControls HumanInput::controls(unsigned held,unsigned pressed,bool classic){
 return makeControls(sample(held,pressed,classic),classic);
}
FighterControls HumanInput::controlsForFrame(unsigned held,unsigned buffered,unsigned pressed,bool classic){
 return makeControls(requests(held,buffered,pressed,classic),classic);
}
FighterControls HumanInput::makeControls(const HumanRequests& request,bool classic){
 const auto down=[&](HumanKey k){return bool(request.held&(1U<<unsigned(k)));};
 FighterControls value;value.horizontal=int(down(HumanKey::Right))-int(down(HumanKey::Left));value.vertical=int(down(HumanKey::Up))-int(down(HumanKey::Down));
 value.attackHeld=down(HumanKey::Attack);value.assistHeld=down(HumanKey::Assist);value.transformHeld=request.transform;
 value.jumpPressed=bool(request.intents&(1U<<unsigned(AiIntent::Jump)));value.dashPressed=bool(request.intents&(1U<<unsigned(AiIntent::DownJump)));
 value.aiIntents=request.intents;value.sourceHuman=true;value.classicInput=classic;
 value.upHeld=down(HumanKey::Up);value.downHeld=down(HumanKey::Down);value.quickJump=request.quickJump;
 value.heldKeys=request.held;value.pressedKeys=request.pressed;value.bufferedKeys=request.buffered;return value;
}
}
