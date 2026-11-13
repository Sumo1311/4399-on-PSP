// GameStage display children, BaseGameSprite.hit and effect display operations.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace bvn {
class SourcePaint {
public:
 using Key=std::uint64_t;
 enum class Kind:unsigned {Sprite,Combat,Ground,Reaction,Shadow,BlackBack,Base};
 enum class Layer:unsigned {Players,Stage};
 static constexpr Key None=std::numeric_limits<Key>::max();
 static constexpr unsigned End=std::numeric_limits<unsigned>::max();
 static constexpr Key key(Kind kind,unsigned id=0){return (Key(kind)<<32)|id;}
 static constexpr Kind kind(Key value){return Kind(value>>32);}
 static constexpr unsigned id(Key value){return unsigned(value);}
 struct Stats{unsigned adds=0,removes=0,moves=0,swaps=0,faults=0;};
 SourcePaint(){players_.reserve(128);stage_.reserve(16);reset();}
 void reset(){players_.clear();stage_={key(Kind::Base,0),key(Kind::Base,1),key(Kind::Base,2)};stats_={};}
 bool add(Layer layer,Key child,unsigned index=End){
  auto& list=children(layer);
  if(index!=End&&index>list.size()){++stats_.faults;return false;}
  remove(child);if(index==End||index>list.size())index=list.size();
  list.insert(list.begin()+index,child);++stats_.adds;return true;
 }
 bool remove(Key child){
  for(auto* list:{&players_,&stage_}){const auto found=std::ranges::find(*list,child);if(found!=list->end()){list->erase(found);++stats_.removes;return true;}}
  return false;
 }
 bool setIndex(Key child,unsigned index){
  for(auto* list:{&players_,&stage_}){const auto found=std::ranges::find(*list,child);if(found==list->end())continue;
   if(index>=list->size()){++stats_.faults;return false;}
   list->erase(found);list->insert(list->begin()+index,child);++stats_.moves;return true;
  }
  ++stats_.faults;return false;
 }
 bool swap(Key first,Key second){
  for(auto* list:{&players_,&stage_}){const auto a=std::ranges::find(*list,first),b=std::ranges::find(*list,second);
   if(a!=list->end()&&b!=list->end()){std::iter_swap(a,b);++stats_.swaps;return true;}}
  ++stats_.faults;return false;
 }
 void hit(Key attacker,Key target){
  for(auto* list:{&players_,&stage_}){const auto a=std::ranges::find(*list,attacker),b=std::ranges::find(*list,target);
   if(a!=list->end()&&b!=list->end()&&a<b){const unsigned i=a-list->begin(),j=b-list->begin();setIndex(target,i);setIndex(attacker,j);return;}}
 }
 std::span<const Key> players()const{return players_;}
 std::span<const Key> stage()const{return stage_;}
 const Stats& stats()const{return stats_;}
 unsigned index(Key value)const{for(auto list:{players(),stage()}){auto found=std::ranges::find(list,value);if(found!=list.end())return found-list.begin();}return End;}
 std::uint64_t hash()const{
  std::uint64_t h=14695981039346656037ull;const auto word=[&](unsigned v){for(unsigned i=0;i<4;++i)h=(h^((v>>(i*8))&255))*1099511628211ull;};
  for(auto list:{players(),stage()}){word(list.size());for(auto v:list){word(unsigned(v));word(unsigned(v>>32));}}return h;
 }
private:
 std::vector<Key>& children(Layer layer){return layer==Layer::Players?players_:stage_;}
 std::vector<Key> players_,stage_;
 Stats stats_{};
};
}
