// Read-only native scene-list observations; existing QA layouts stay unchanged.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_scene.hpp"
#include <cstring>
namespace bvn {
struct SourceSceneQa {
    unsigned draws,count,children[2],renders,pairs,adds,removes,faults,order[12];
};
static_assert(sizeof(SourceSceneQa)==84);
inline void recordSourceSceneQa(SourceSceneQa& output,unsigned draws,const SourceScene* scene){
    SourceSceneQa next{};next.draws=draws;std::fill(std::begin(next.order),std::end(next.order),SourceScene::Missing);
    if(scene){const auto& s=scene->stats();next.count=scene->displays().size();next.children[0]=scene->children(0).size();next.children[1]=scene->children(1).size();
        next.renders=s.renders;next.pairs=s.pairs;next.adds=s.adds;next.removes=s.removes;next.faults=s.rangeFaults;
        std::copy_n(scene->displays().begin(),std::min<std::size_t>(12,scene->displays().size()),next.order);}
    std::memcpy(reinterpret_cast<char*>(&output)+4,reinterpret_cast<const char*>(&next)+4,sizeof(next)-4);
    asm volatile("" ::: "memory");output.draws=draws;
}
}
