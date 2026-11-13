// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_scene.hpp"
#include <array>
#include <cstdio>
#include <vector>
namespace bvn::scene_order_qa {
struct Checks {
 unsigned error=0,rows=0,rangeFaults=0,renderActions=0,contactActions=0,failedRow=0,failedColumn=0;
 int actual=0,expected=0;std::uint64_t hash=14695981039346656037ull;
 void word(unsigned value){for(unsigned i=0;i<4;++i)hash=(hash^((value>>(8*i))&255))*1099511628211ull;}
#define SCENE_REQUIRE(x) do{if(!(x)){error=__LINE__;return false;}}while(false)
 bool reference(const char* folder){
  const auto file=std::string(folder)+"SCENE.IN";FILE* f=std::fopen(file.c_str(),"r");SCENE_REQUIRE(f);unsigned count=0;SCENE_REQUIRE(std::fscanf(f,"%u",&count)==1&&count==12288);
  SourceScene scene;std::array<int,5> action{};bool used=false;std::vector<int> trace;trace.reserve(256);
  const auto operate=[&](int op,unsigned key,unsigned team){switch(op){
   case 1:scene.add(key,team);break;
   case 2:scene.remove(key,key%3!=1);break;
   case 3:scene.removeStage(key);break;
   case 4:scene.removeTeam(team);break;
   case 5:scene.addTeam(team);break;
   case 6:scene.destroy(key);break;
   case 7:scene.remove(key,true);break;
   case 8:scene.removeChild(team,key);break;
   case 9:scene.addChild(team,key);break;
   case 10:scene.addStage(key);break;
  }};
  const auto event=[&](int phase,unsigned a,unsigned b=0){trace.push_back(phase*4096+a*16+b);if(!used&&phase==action[0]&&a==unsigned(action[1])){used=true;operate(action[2],action[3],action[4]);if(phase<4)++renderActions;else ++contactActions;}};
  for(unsigned row=0;row<count;++row){std::array<int,11> input{};for(auto& v:input)SCENE_REQUIRE(std::fscanf(f,"%d",&v)==1);
   unsigned size=0;SCENE_REQUIRE(std::fscanf(f,"%u",&size)==1&&size<1024);std::vector<int> want(size);for(auto& v:want)SCENE_REQUIRE(std::fscanf(f,"%d",&v)==1);
   if(input[1]==0){scene.reset();for(unsigned key=0;key<12;++key)scene.track(key);scene.addTeam(0);scene.addTeam(1);for(unsigned key=0;key<4;++key)scene.add(key,key%2);}
   std::copy_n(input.begin()+6,5,action.begin());used=true;operate(input[3],input[4],input[5]);trace.clear();used=false;scene.beginFrame(input[2]);
   scene.render([&](unsigned key){event(1,key);if(!scene.destroyed(key))event(2,key);if(scene.animate()&&!scene.destroyed(key))event(3,key);});
   const bool complete=scene.pairs([&](unsigned a,unsigned b){event(4,a,b);if(scene.animate())event(5,a,b);});rangeFaults+=!complete;
   unsigned active=0,dead=0;for(unsigned key=0;key<12;++key){if(scene.registered(key))active|=1U<<key;if(scene.destroyed(key))dead|=1U<<key;}
   std::vector<int> got{!complete,int(scene.animate()),int(active),int(dead),int(scene.displays().size())};for(auto key:scene.displays())got.push_back(key);
   for(unsigned id=0;id<3;++id){got.push_back(scene.hasTeam(id)?int(scene.children(id).size()):-1);if(scene.hasTeam(id))for(auto key:scene.children(id))got.push_back(key);}
   got.push_back(trace.size());got.insert(got.end(),trace.begin(),trace.end());
   if(got.size()!=want.size()){failedRow=row;failedColumn=1024;actual=got.size();expected=want.size();error=__LINE__;std::fclose(f);return false;}
   for(unsigned c=0;c<got.size();++c){if(got[c]!=want[c]){failedRow=row;failedColumn=c;actual=got[c];expected=want[c];error=__LINE__;std::fclose(f);return false;}word(got[c]);}++rows;
  }
  std::fclose(f);return true;
 }
#undef SCENE_REQUIRE
};
}
