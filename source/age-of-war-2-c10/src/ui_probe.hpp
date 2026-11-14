#pragma once
#include "ui.hpp"
#include <cstdio>
namespace aow {
inline void uiProbe(FILE* f,const UI& ui,const Game& g,unsigned frame,unsigned pressed,unsigned held){
 if(!f)return;
 std::fprintf(f,"U|%u|%u|%u|%d|%d|%d|%d|%s|%d|%d|%.6f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d\n",frame,pressed,held,int(ui.page),ui.selection,ui.pauseSelection,ui.focus,ui.focusKey.c_str(),ui.focusKind,ui.focusId,g.camera,g.money,g.xp,g.pop[0],g.spots[0],g.pendingTurret,g.selling,g.paused,g.tutorial,g.age[0],g.result);
 for(auto& a:ui.actions(g))std::fprintf(f,"B|%u|%s|%d|%d|%.3f|%.3f|%.3f|%.3f\n",frame,a.key.c_str(),a.kind,a.id,a.x,a.y,a.w,a.h);
 std::fflush(f);
}
}
