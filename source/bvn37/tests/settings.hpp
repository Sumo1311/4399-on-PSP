// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game_settings.hpp"
#include "versus_round.hpp"
#include <cstdio>
#include <string>
namespace bvn::settings_qa {
struct Checks {
 unsigned error=0,variants=0,saves=0,inputRows=0,rounds=0,classicActions=0,standardActions=0,tapCases=0;
 std::uint64_t inputHash=14695981039346656037ULL;
#define SETTINGS_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
 bool options(){
  GameSettings s;SETTINGS_REQUIRE(s.valid()&&s.difficulty==1&&s.classic()&&s.healthScale()==1&&s.seconds()==60&&s.soundPercent()==70&&s.musicPercent()==70&&s.gameHz()==30&&!s.showHp);
  constexpr unsigned counts[]{6,2,5,4,6,6,4,2};
  for(unsigned row=0;row<8;++row){const auto start=s;for(unsigned i=0;i<counts[row];++i){SETTINGS_REQUIRE(s.change(row,1)&&s.valid());++variants;}SETTINGS_REQUIRE(s==start);for(unsigned i=0;i<counts[row];++i)SETTINGS_REQUIRE(s.change(row,-1));SETTINGS_REQUIRE(s==start);}
  for(unsigned value=0;value<64;++value){const auto before=s;const bool valid=(value&~31U)==0&&(value&7)<5&&(value>>3)<4;SETTINGS_REQUIRE(s.applyWireRules(value)==valid);if(valid)SETTINGS_REQUIRE(s.wireRules()==value);else SETTINGS_REQUIRE(s==before);++variants;}
  s={};for(unsigned i=0;i<4;++i){s.quality=i;SETTINGS_REQUIRE(s.gameHz()==(i?60:30)&&s.gameHz(true)==30&&s.classic(true));SETTINGS_REQUIRE(s.shineHz()==(i==0?15:i==3?60:30));}
  for(unsigned raw=0;raw<1024;++raw){unsigned physical=0;for(unsigned i=0;i<10;++i)if(raw&(1U<<i))physical|=DefaultButtons[i];SETTINGS_REQUIRE(KeyBindings{}.logical(physical)==raw);++variants;}
  s.keys[1].buttons[0]=s.keys[1].buttons[1];SETTINGS_REQUIRE(s.valid());s.keys[1].buttons[0]=8;SETTINGS_REQUIRE(!s.valid());return true;
 }
 bool journal(const std::string& folder){
  const auto a=folder+"/SETTINGS0.DAT",b=folder+"/SETTINGS1.DAT";std::remove(a.c_str());std::remove(b.c_str());
  SettingsStore store;GameSettings first;first.life=4;first.music=0;first.keys[1].buttons[4]=0x2000;
  GameSettings restored;SETTINGS_REQUIRE(!store.load(folder.c_str(),restored));
  SETTINGS_REQUIRE(store.save(folder.c_str(),first)&&store.serial()==1);++saves;
  auto second=first;second.difficulty=6;second.sound=5;second.mode=0;
  SETTINGS_REQUIRE(store.save(folder.c_str(),second)&&store.serial()==2);++saves;
  SettingsStore reopened;SETTINGS_REQUIRE(reopened.load(folder.c_str(),restored)&&restored==second&&reopened.serial()==2);
  FILE* damaged=std::fopen(a.c_str(),"wb");SETTINGS_REQUIRE(damaged);std::fputs("bad",damaged);std::fclose(damaged);
  SETTINGS_REQUIRE(reopened.load(folder.c_str(),restored)&&restored==first&&reopened.serial()==1);++saves;
  auto invalid=second;invalid.difficulty=0;SETTINGS_REQUIRE(!reopened.save(folder.c_str(),invalid)&&reopened.serial()==1);
  SETTINGS_REQUIRE(reopened.save(folder.c_str(),second)&&reopened.serial()==2);++saves;
  SettingsStore cold;SETTINGS_REQUIRE(cold.load(folder.c_str(),restored)&&restored==second);return true;
 }
 bool requests(FILE* trace){
  for(unsigned mode=0;mode<2;++mode)for(unsigned mask=0;mask<1024;++mask){
   HumanInput down,tapped;
   const auto expected=down.sample(mask,mask,mode==1),actual=tapped.sample(0,mask,mode==1);
   SETTINGS_REQUIRE(actual.held==expected.held&&actual.buffered==expected.buffered&&actual.intents==expected.intents&&actual.transform==expected.transform&&actual.quickJump==expected.quickJump&&actual.pressed==expected.pressed);
   const auto released=tapped.sample(0,0,mode==1);SETTINGS_REQUIRE(!released.held&&!released.pressed);
  }
  for(unsigned mode=0;mode<2;++mode)for(unsigned mask=0;mask<1024;++mask){
   HumanInput input;
   for(unsigned tick=0;tick<18;++tick){
    const unsigned held=tick<12?mask:0,pressed=tick==0?mask:0;const auto r=input.sample(held,pressed,mode==1);
    const unsigned words[]{mode,mask,tick,held,pressed,r.buffered,r.intents,unsigned(r.transform),unsigned(r.quickJump)};
    if(trace)std::fprintf(trace,"%u %u %u %u %u %u %u %u %u\n",words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],words[8]);
    for(const auto word:words)for(unsigned b=0;b<4;++b)inputHash=(inputHash^((word>>(8*b))&255))*1099511628211ULL;
    ++inputRows;
   }
  }
  SETTINGS_REQUIRE(inputRows==36864);return true;
 }
 bool actors(const std::string& package){
  StageAsset stage;FighterAsset asset;SETTINGS_REQUIRE(stage.open((package+"JIANCHANG.BVN").c_str()));SETTINGS_REQUIRE(asset.open((package+"ICHIGO.BVF").c_str(),FighterAsset::TextureResidency::LowMemory));
  for(const char* bank:{"ICHIGO.BVF","NARUTO.BVF"}) {
   FighterAsset tapAsset;SETTINGS_REQUIRE(tapAsset.open((package+bank).c_str(),FighterAsset::TextureResidency::LowMemory));
   for(unsigned hz:{30U,60U})for(unsigned mode:{0U,1U})for(unsigned offset=0;offset<4;++offset) {
    FighterMotion a(tapAsset);const auto floor=stage.geometry();a.reset({400,floor.playerBottom},1);a.setGameHz(hz);a.startTraining();HumanInput input;unsigned accepted=0;
    for(unsigned tick=0;tick<60;++tick) {
     const unsigned key=tick==offset?1U<<unsigned(HumanKey::Attack):0;
     a.step(floor,input.controls(key,key,mode==1),tick%2==0);
     if(const auto event=a.takeTrainingInput();event&&event->command==FighterCommand::Attack)++accepted;
    }
    SETTINGS_REQUIRE(accepted==1);++tapCases;
   }
  }
  SETTINGS_REQUIRE(tapCases==32);
  {
   FighterMotion a(asset);const auto floor=stage.geometry();a.reset({400,floor.playerBottom},1);a.setGameHz(30);a.startTraining();HumanInput input;
   const unsigned attack=1U<<unsigned(HumanKey::Attack);
   a.step(floor,input.controls(attack,attack,true),true);
   a.clearInput();input.clear();
   a.step(floor,input.controls(0,0,true),false);
   SETTINGS_REQUIRE(!a.takeTrainingInput());
  }
  for(unsigned mode=0;mode<2;++mode){
   FighterMotion a(asset),target(asset);const auto floor=stage.geometry();a.reset({400,floor.playerBottom},1);target.reset({600,floor.playerBottom},-1);a.startTraining();HumanInput input;unsigned actions=0;
   for(unsigned tick=0;tick<600;++tick){a.step(floor,input.controls(1U<<unsigned(HumanKey::Attack),tick?0:1U<<unsigned(HumanKey::Attack),mode==1),tick%2==0,{},&target);if(const auto event=a.takeTrainingInput();event&&event->command==FighterCommand::Attack)++actions;}
   if(mode==0)standardActions=actions;else classicActions=actions;
  }
  SETTINGS_REQUIRE(standardActions==1&&classicActions>standardActions);
  for(unsigned mode=0;mode<2;++mode){
   FighterMotion a(asset);const auto floor=stage.geometry();a.reset({400,floor.playerBottom},1);HumanInput input;
   for(unsigned tick=0;tick<22;++tick){const bool down=tick<2||(tick>=10&&tick<12);const unsigned held=down?1U<<unsigned(HumanKey::Jump):0;a.step(floor,input.controls(held,0,mode==1),tick%2==0);}
   SETTINGS_REQUIRE(a.remainingJumps()==(mode==0?0:1));
  }
  for(unsigned life=0;life<5;++life)for(unsigned time=0;time<4;++time){
   GameSettings config;config.life=life;config.time=time;RoundRules rules;rules.maxHealth=FighterMaxHealth*config.healthScale();
   VersusRound round(stage,std::span(&asset,1),std::span(&asset,1),config.seconds(),{},BattleMode::Versus,{}, {},rules);
   unsigned ticks=0;while(round.phase()!=RoundPhase::Playing&&ticks++<2000)round.step({},true);
   SETTINGS_REQUIRE(round.phase()==RoundPhase::Playing&&round.match().actor(0).maxHealth()==rules.maxHealth&&round.match().actor(1).maxHealth()==rules.maxHealth);
   // Original GameRoundCtrl uses > FPS_ANIMATE, hence 31 animation calls.
   const int before=round.seconds();for(unsigned i=0;i<(AnimationHz+1)*LogicHz/AnimationHz;++i)round.step({});SETTINGS_REQUIRE(round.seconds()==(before<0?-1:before-1));++rounds;
  }
  SETTINGS_REQUIRE(FightMatch::modeScore(41,false)==41&&FightMatch::modeScore(41,true)==32);return true;
 }
 bool run(const std::string& package,const std::string& folder,FILE* trace=nullptr){return options()&&journal(folder)&&requests(trace)&&actors(package);}
#undef SETTINGS_REQUIRE
};
}
