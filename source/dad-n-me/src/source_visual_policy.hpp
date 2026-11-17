#pragma once
#include <cstdint>
namespace gameswf {
struct as_s_function; struct fn_call; struct as_environment; struct as_object; struct character;
bool dnm_visual_accelerate(const as_s_function&,const fn_call&,as_environment*,as_object*);
void dnm_finish_visual_bindings();
int dnm_visual_frame(character*,int);
bool dnm_visual_hidden(character*);
bool dnm_run_frame_event(character*);
struct DnmVisualStats {unsigned deferred=0,rebound=0,fire_suppressed=0,pending=0,flame_callbacks_skipped=0;};
DnmVisualStats dnm_visual_stats();
}
