#pragma once
#include "gameswf/gameswf.h"
gameswf::render_handler* make_psp_renderer();
void finish_psp_frame(const char* overlay = nullptr,bool footer = false);
void prepare_psp_tick(unsigned tick);
bool release_psp_texture();

bool dump_psp_frame(const char* path);

void submit_psp_frame(const char* overlay=nullptr,bool footer=false);
void present_psp_frame();
void hold_psp_frame();

unsigned long long psp_present_time();
unsigned psp_present_vcount();
void set_psp_port_font(gameswf::glyph_provider*,gameswf::render_handler*);
void submit_psp_loading();
bool confirm_psp_frame_visible();

namespace psp60 {struct Frame;}
bool prepare_psp_frame_resources(const psp60::Frame&,unsigned& command,unsigned& tile,unsigned long long deadline);
