// Adapted from GameStage, GameCtrl, TeamVO and GameMainLogicCtrl.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_paint.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace bvn {
class SourceScene {
public:
    using Key=std::uint32_t;
    static constexpr Key Object=0x80000000u;
    static constexpr unsigned Missing=std::numeric_limits<unsigned>::max();
    struct Team {unsigned id;std::vector<Key> children;};
    struct Trace {
        void* context=nullptr;
        void (*render)(void*,Key)=nullptr;
        void (*pair)(void*,Key,Key)=nullptr;
    };
    struct Stats {unsigned renders=0,pairs=0,adds=0,removes=0,rangeFaults=0;};

    SourceScene(){displays_.reserve(128);nodes_.reserve(128);teams_.reserve(2);}
    void reset(){displays_.clear();nodes_.clear();teams_.clear();frameTeams_.clear();stats_={};trace_={};animate_=false;paint_.reset();}
    void addTeam(unsigned id){
        auto value=std::make_shared<Team>();value->id=id;value->children.reserve(66);
        const auto found=std::ranges::find_if(teams_,[&](const auto& t){return t->id==id;});
        if(found==teams_.end())teams_.push_back(std::move(value));else *found=std::move(value);
        // Original game teams use integer Object keys; the reference declares
        // numeric-key enumeration, with native 0/1 corresponding to P1/P2.
        std::ranges::sort(teams_,{},[](const auto& t){return t->id;});
    }
    bool hasTeam(unsigned id)const{return bool(team(id));}
    void removeTeam(unsigned id){std::erase_if(teams_,[&](const auto& t){return t->id==id;});}
    void track(Key key){nodes_.try_emplace(key);}
    void addStage(Key key,unsigned storage=Missing,unsigned paintIndex=SourcePaint::End){
        auto& node=nodes_[key];node.active=true;node.storage=storage;
        if(std::ranges::find(displays_,key)==displays_.end()){displays_.push_back(key);paint_.add(SourcePaint::Layer::Players,SourcePaint::key(SourcePaint::Kind::Sprite,key),paintIndex);++stats_.adds;}
    }
    void addChild(unsigned id,Key key){if(auto t=team(id)){nodes_.try_emplace(key);if(std::ranges::find(t->children,key)==t->children.end())t->children.push_back(key);}}
    void removeChild(unsigned id,Key key){if(auto t=team(id))std::erase(t->children,key);}
    void add(Key key,unsigned id,unsigned storage=Missing){
        addStage(key,storage);if(auto t=team(id)){nodes_[key].team=t;addChild(id,key);}
    }
    void destroy(Key key){nodes_[key].destroyed=true;}
    // GameStage removal alone leaves TeamVO membership and destruction alone.
    void removeStage(Key key){
        auto found=nodes_.find(key);if(found==nodes_.end())return;
        found->second.active=false;
        const auto at=std::ranges::find(displays_,key);if(at!=displays_.end()){displays_.erase(at);paint_.remove(SourcePaint::key(SourcePaint::Kind::Sprite,key));++stats_.removes;}
    }
    void remove(Key key,bool destroy=true){
        removeStage(key);auto found=nodes_.find(key);if(found==nodes_.end())return;
        if(auto t=found->second.team)std::erase(t->children,key);
        found->second.destroyed|=destroy;
    }
    void forget(Key key){nodes_.erase(key);}
    void storage(Key key,unsigned value){if(auto found=nodes_.find(key);found!=nodes_.end())found->second.storage=value;}
    unsigned storage(Key key)const{const auto found=nodes_.find(key);return found==nodes_.end()?Missing:found->second.storage;}
    bool registered(Key key)const{const auto found=nodes_.find(key);return found!=nodes_.end()&&found->second.active;}
    bool destroyed(Key key)const{const auto found=nodes_.find(key);return found==nodes_.end()||found->second.destroyed;}
    std::span<const Key> displays()const{return displays_;}
    std::span<const Key> children(unsigned id)const{auto t=team(id);return t?std::span<const Key>(t->children):std::span<const Key>{};}
    const Stats& stats()const{return stats_;}
    SourcePaint& paint(){return paint_;}
    const SourcePaint& paint()const{return paint_;}
    void trace(Trace value){trace_=value;}
    void beginFrame(bool requested=false){stats_.renders=stats_.pairs=0;animate_|=requested;frameTeams_=teams_;}
    bool animate()const{return animate_;}
    template<class Render> void render(Render&& visit){
        // Deliberately use the live length and an unconditional index increment.
        // Removing the current entry skips the neighbour shifted into its slot.
        for(unsigned i=0;i<displays_.size();++i){const auto key=displays_[i];++stats_.renders;if(trace_.render)trace_.render(trace_.context,key);visit(key);}
    }
    template<class Pair> bool pairs(Pair&& visit){
        // TeamMap.refreshTeams replaces its Vector; a running MainLogic call
        // keeps the old team vector. TeamVO children themselves remain live.
        const auto teams=frameTeams_;
        for(unsigned i=0;i<teams.size();++i){auto first=teams[i];
            for(unsigned j=0;j<first->children.size();++j){const auto a=first->children[j];if(destroyed(a))continue;
                for(unsigned k=i+1;k<teams.size();++k){auto second=teams[k];const auto count=second->children.size();
                    for(unsigned l=0;l<count;++l){
                        // AS3 Vector indexed access throws after a captured
                        // child length outlives a removal; stop this traversal.
                        if(l>=second->children.size()){++stats_.rangeFaults;return false;}
                        const auto b=second->children[l];if(destroyed(b))continue;
                        ++stats_.pairs;if(trace_.pair)trace_.pair(trace_.context,a,b);visit(a,b);
                    }
                }
            }
        }
        animate_=false;return true;
    }
private:
    struct Node {std::shared_ptr<Team> team;unsigned storage=Missing;bool active=false,destroyed=false;};
    std::shared_ptr<Team> team(unsigned id)const{const auto found=std::ranges::find_if(teams_,[&](const auto& t){return t->id==id;});return found==teams_.end()?nullptr:*found;}
    std::vector<Key> displays_;
    std::unordered_map<Key,Node> nodes_;
    std::vector<std::shared_ptr<Team>> teams_,frameTeams_;
    Stats stats_{};Trace trace_{};bool animate_=false;
    SourcePaint paint_{};
};
}
