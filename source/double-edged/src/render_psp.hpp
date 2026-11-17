#pragma once
#include "gameswf/gameswf.h"
gameswf::render_handler* make_psp_renderer();
void finish_psp_frame(const char* overlay = nullptr,bool footer = false);
void prepare_psp_tick(unsigned tick);
namespace psp60 {struct Frame;}
void prepare_psp_capture(const psp60::Frame&,void(*lock)(),void(*unlock)());
void flush_psp_prepare_stats();
void flush_psp_media_stats();
bool release_psp_texture();

void set_psp_pointer(float x,float y,bool visible);
void set_psp_quality(const char* quality);
bool set_psp_texture_pool_mib(unsigned mib);
void set_psp_bank_scratch(bool enabled);
void set_psp_codec_reuse(bool enabled);
bool dump_psp_frame(const char* path);
void draw_psp_message(const char* message,void(*before_swap)(void*)=nullptr,void* context=nullptr);
void draw_psp_boot_loading();
void draw_psp_loading();
void set_psp_link_role(int role);
bool fast_color_filters();
void set_fast_color_filters(bool enabled);
void notify_psp_picture_mode();

unsigned long long psp_present_time();
unsigned psp_present_vcount();

void present_psp_frame(void(*before_swap)(void*)=nullptr,void* context=nullptr);
void hold_psp_frame();
void set_psp_latch_probe(bool);
bool psp_latch_confirmed();
unsigned long long psp_latch_time();
void submit_psp_frame(const char* overlay=nullptr,bool footer=false);
void set_psp_port_font(gameswf::glyph_provider*,gameswf::render_handler*);
void submit_psp_loading();
bool confirm_psp_frame_visible(bool wait_vblank=true);
