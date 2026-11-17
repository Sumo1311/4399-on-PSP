#pragma once
#include "gameswf/gameswf.h"
#include "../src/button_hit_shapes.hpp"
#include <algorithm>
#include <cstdio>
// Geometry-retention qualification only; it never claims visual coverage.
struct NullRenderer : gameswf::render_handler {
    bool release_geometry=true;
    bool trace_shapes=false;
    bool draw_shape(int id,const gameswf::matrix& m,const gameswf::cxform&,Uint16 ratio) override{
        if(trace_shapes)std::printf("{\"resource_shape\":%d,\"ratio\":%u,\"matrix\":[[%g,%g,%g],[%g,%g,%g]]}\n",id,unsigned(ratio),double(m.m_[0][0]),double(m.m_[0][1]),double(m.m_[0][2]),double(m.m_[1][0]),double(m.m_[1][1]),double(m.m_[1][2]));
        return true;
    }
    bool can_release_shape_geometry(int id) const override {return release_geometry && !std::binary_search(dnm::button_hit_shapes.begin(),dnm::button_hit_shapes.end(),unsigned(id));}
    gameswf::bitmap_info* create_bitmap_info_empty() override{return new gameswf::bitmap_info;}
    gameswf::bitmap_info* create_bitmap_info_alpha(int,int,unsigned char*) override{return new gameswf::bitmap_info;}
    gameswf::bitmap_info* create_bitmap_info_rgb(image::rgb*) override{return new gameswf::bitmap_info;}
    gameswf::bitmap_info* create_bitmap_info_rgba(image::rgba*) override{return new gameswf::bitmap_info;}
    gameswf::video_handler* create_video_handler() override{return nullptr;}
    void begin_display(gameswf::rgba,int,int,int,int,float,float,float,float) override{}
    void end_display() override{}
    void set_matrix(const gameswf::matrix&) override{}
    void set_cxform(const gameswf::cxform&) override{}
    void draw_mesh_strip(const void*,int) override{}
    void draw_triangle_list(const void*,int) override{}
    void draw_line_strip(const void*,int) override{}
    void fill_style_disable(int) override{}
    void fill_style_color(int,const gameswf::rgba&) override{}
    void fill_style_bitmap(int,gameswf::bitmap_info*,const gameswf::matrix&,bitmap_wrap_mode,bitmap_blend_mode) override{}
    void line_style_disable() override{}
    void line_style_color(gameswf::rgba) override{}
    void line_style_width(float) override{}
    void draw_bitmap(const gameswf::matrix&,gameswf::bitmap_info*,const gameswf::rect&,const gameswf::rect&,gameswf::rgba) override{}
    void set_antialiased(bool) override{}
    bool test_stencil_buffer(const gameswf::rect&,Uint8) override{return true;}
    void begin_submit_mask() override{}
    void end_submit_mask() override{}
    void disable_mask() override{}
    bool is_visible(const gameswf::rect&) override{return true;}
    void open() override{}
};
