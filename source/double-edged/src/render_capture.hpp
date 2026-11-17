#pragma once
#include "quickjs.h"
#include "recorded_renderer.hpp"
void de_enable_capture(void(*lock)(),void(*unlock)());
void de_bind_render_mirror(JSContext*);
void de_flush_render_audit();
bool fast_color_filters();
int de_capture_source(JSContext*,psp60::Frame&,bool smooth_picture);
inline int de_capture_source(JSContext* c,psp60::Frame& frame){return de_capture_source(c,frame,fast_color_filters());}
void de_play_capture(const psp60::Frame&,float alpha);
