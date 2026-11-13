// Read-only display ordering observations without changing prior QA layouts.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "source_scene.hpp"
#include <cstring>
namespace bvn {
struct SourcePaintQa {
 unsigned draws,players,roots,faults,hashLow,hashHigh;
 unsigned long long playerOrder[128],rootOrder[32];
};
static_assert(sizeof(SourcePaintQa)==1304);
inline void recordSourcePaintQa(SourcePaintQa& output,unsigned draws,const SourceScene* scene){
 SourcePaintQa next{};next.draws=draws;std::fill(std::begin(next.playerOrder),std::end(next.playerOrder),SourcePaint::None);std::fill(std::begin(next.rootOrder),std::end(next.rootOrder),SourcePaint::None);
 if(scene){const auto& paint=scene->paint();next.players=paint.players().size();next.roots=paint.stage().size();next.faults=paint.stats().faults;const auto hash=paint.hash();next.hashLow=unsigned(hash);next.hashHigh=unsigned(hash>>32);
  std::copy_n(paint.players().begin(),std::min<std::size_t>(128,paint.players().size()),next.playerOrder);std::copy_n(paint.stage().begin(),std::min<std::size_t>(32,paint.stage().size()),next.rootOrder);}
 std::memcpy(reinterpret_cast<char*>(&output)+4,reinterpret_cast<const char*>(&next)+4,sizeof(next)-4);asm volatile("" ::: "memory");output.draws=draws;
}
}
