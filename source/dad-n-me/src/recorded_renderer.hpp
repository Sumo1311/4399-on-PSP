#pragma once
#include "gameswf/gameswf.h"
#include "gameswf/gameswf_types.h"
#include "base/tu_types.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <unordered_set>
#include "baked_bounds_cache.hpp"

// The VM writes one private buffer; the presentation thread reads a different
// immutable buffer. Commands contain values and retained immutable bitmaps,
// never a live ActionScript object or a pointer into its display list.
namespace psp60 {
using namespace gameswf;
enum class Op {Shape,Matrix,Color,Strip,Triangles,Line,FillOff,FillColor,
    FillBitmap,LineOff,LineColor,LineWidth,Bitmap,MaskBegin,MaskEnd,MaskOff};
struct Command {
    Op op{}; int a=0,b=0,c=0;
    matrix m,previous; cxform cx; rgba color;
    bitmap_info* bitmap=nullptr;
    rect bounds,uv;
    float width=0;
    unsigned offset=0;
};
struct Frame {
    rgba background;
    int x=0,y=0,w=0,h=0;
    float x0=0,x1=0,y0=0,y1=0;
    std::vector<Command> commands;
    std::vector<coord_component> coordinates;
    void clear() {commands.clear();coordinates.clear();}
    static matrix blend(const Command& p,float alpha) {
        if(alpha>=1)return p.m;
        matrix out;for(int y=0;y<2;++y)for(int x=0;x<3;++x)
            out.m_[y][x]=float(p.previous.m_[y][x])+(float(p.m.m_[y][x])-float(p.previous.m_[y][x]))*alpha;
        return out;
    }
    void play(gameswf::render_handler* r,float alpha=1) const {
        r->begin_display(background,x,y,w,h,x0,x1,y0,y1);
        for(const auto& p:commands) switch(p.op) {
        case Op::Shape:r->draw_shape(p.a,blend(p,alpha),p.cx,Uint16(p.b));break;
        case Op::Matrix:r->set_matrix(blend(p,alpha));break;
        case Op::Color:r->set_cxform(p.cx);break;
        case Op::Strip:r->draw_mesh_strip(coordinates.data()+p.offset,p.a);break;
        case Op::Triangles:r->draw_triangle_list(coordinates.data()+p.offset,p.a);break;
        case Op::Line:r->draw_line_strip(coordinates.data()+p.offset,p.a);break;
        case Op::FillOff:r->fill_style_disable(p.a);break;
        case Op::FillColor:r->fill_style_color(p.a,p.color);break;
        case Op::FillBitmap:r->fill_style_bitmap(p.a,p.bitmap,p.m,gameswf::render_handler::bitmap_wrap_mode(p.b),gameswf::render_handler::bitmap_blend_mode(p.c));break;
        case Op::LineOff:r->line_style_disable();break;
        case Op::LineColor:r->line_style_color(p.color);break;
        case Op::LineWidth:r->line_style_width(p.width);break;
        case Op::Bitmap:r->draw_bitmap(blend(p,alpha),p.bitmap,p.bounds,p.uv,p.color);break;
        case Op::MaskBegin:r->begin_submit_mask();break;
        case Op::MaskEnd:r->end_submit_mask();break;
        case Op::MaskOff:r->disable_mask();break;
        }
        r->end_display();
    }
};
class Recorder final : public gameswf::render_handler {
    gameswf::render_handler* resources;
    void (*lock)();void (*unlock)();
    Frame* frame=nullptr;
    matrix motion,motion_current,motion_previous;
    float motion_determinant=0;
    bool motion_hint=false,motion_ready=true,motion_direct=false;
    BakedBoundsCache bounds_cache;
    float cull_margin_x=0,cull_margin_y=0;
    std::vector<gc_ptr<bitmap_info>> bitmaps;
    std::unordered_set<bitmap_info*> retained;
    Command ignored;
    unsigned outside_calls=0;
    void retain(bitmap_info* p){if(p&&retained.insert(p).second)bitmaps.emplace_back(p);}
    Command& add(Op op) {if(!frame){++outside_calls;ignored.op=op;return ignored;}frame->commands.emplace_back();auto& p=frame->commands.back();p.op=op;return p;}
    matrix previous_for(const matrix& m) {
        // Reuse the supplied source endpoint when drawing uses that same
        // world matrix. A drawing-local transform retains the inverse path.
        if(motion_hint&&motion_direct&&std::memcmp(m.m_,motion_current.m_,sizeof(m.m_))==0)
            return motion_previous;
        if(!motion_ready){
            motion.set_identity();
            if(!(std::fabs(motion_determinant)<1e-12f)){
                matrix inverse;inverse.set_inverse(motion_current);
                motion=motion_previous;motion.concatenate(inverse);
            }
            motion_ready=true;
        }
        matrix previous=motion;previous.concatenate(m);return previous;
    }
    void transformed(Command& p,const matrix& m) {
        p.m=m;p.previous=previous_for(m);
    }
    void geometry(Op op,const void* input,int n) {
        if(!frame){++outside_calls;return;}
        auto& p=add(op);p.a=n;p.offset=frame->coordinates.size();
        auto coords=static_cast<const coord_component*>(input);
        frame->coordinates.insert(frame->coordinates.end(),coords,coords+n*2);
    }
    template<class F> bitmap_info* create(F factory) {
        lock();auto* result=factory();unlock();
        // Keep resource lifetimes on this thread; command playback never
        // increments/decrements gameswf's non-atomic reference counts.
        retain(result);return result;
    }
public:
    Recorder(gameswf::render_handler* r,void(*acquire)(),void(*release)()):resources(r),lock(acquire),unlock(release){}
    void record(Frame& out){frame=&out;out.clear();motion.set_identity();motion_hint=false;motion_ready=true;motion_direct=false;}
    void finish(){frame=nullptr;}
    unsigned discarded_commands()const{return outside_calls;}
    void set_motion(const matrix& current,const matrix& previous)override {
        motion_current=current;motion_previous=previous;motion_hint=true;motion_ready=false;
        motion_determinant=current.get_determinant();
        motion_direct=std::isfinite(motion_determinant)&&std::fabs(motion_determinant)>=1e-12f;
    }
    void open() override {}
    bool draw_shape(int id,const matrix& m,const cxform& c,Uint16 ratio) override {
        rect bounds;bool bounded=resources->baked_shape_bounds(id,ratio,&bounds);
        if(!bounded&&!resources->has_baked_shape(id,ratio))return false;
        matrix previous=previous_for(m);
        if(frame&&bounded){
            rect current,old;bounds_cache.transform(id,ratio,bounds,m,current);bounds_cache.transform(id,ratio,bounds,previous,old);
            // Every interpolated corner is between its two endpoints. Cull
            // only when both endpoint boxes lie outside the SAME viewport
            // edge, retaining a full pixel for filtering/rounding differences.
            const float mx=cull_margin_x,my=cull_margin_y;
            if((current.m_x_max<frame->x0-mx&&old.m_x_max<frame->x0-mx)||
               (current.m_x_min>frame->x1+mx&&old.m_x_min>frame->x1+mx)||
               (current.m_y_max<frame->y0-my&&old.m_y_max<frame->y0-my)||
               (current.m_y_min>frame->y1+my&&old.m_y_min>frame->y1+my))return true;
        }
        auto& p=add(Op::Shape);p.a=id;p.b=ratio;p.m=m;p.previous=previous;p.cx=c;return true;
    }
    bool can_release_shape_geometry(int id)const override{return resources->can_release_shape_geometry(id);}
    bitmap_info* create_bitmap_info_empty()override{return create([&]{return resources->create_bitmap_info_empty();});}
    bitmap_info* create_bitmap_info_alpha(int w,int h,unsigned char* p)override{return create([&]{return resources->create_bitmap_info_alpha(w,h,p);});}
    bitmap_info* create_bitmap_info_rgb(image::rgb* p)override{return create([&]{return resources->create_bitmap_info_rgb(p);});}
    bitmap_info* create_bitmap_info_rgba(image::rgba* p)override{return create([&]{return resources->create_bitmap_info_rgba(p);});}
    video_handler* create_video_handler()override{return nullptr;}
    void begin_display(rgba c,int x,int y,int w,int h,float x0,float x1,float y0,float y1)override {
        if(!frame){++outside_calls;return;}
        frame->background=c;frame->x=x;frame->y=y;frame->w=w;frame->h=h;
        frame->x0=x0;frame->x1=x1;frame->y0=y0;frame->y1=y1;
        cull_margin_x=w?std::fabs((x1-x0)/w):0;cull_margin_y=h?std::fabs((y1-y0)/h):0;
    }
    void end_display()override{}
    void set_matrix(const matrix& m)override{transformed(add(Op::Matrix),m);}
    void set_cxform(const cxform& c)override{add(Op::Color).cx=c;}
    void draw_mesh_strip(const void* p,int n)override{geometry(Op::Strip,p,n);}
    void draw_triangle_list(const void* p,int n)override{geometry(Op::Triangles,p,n);}
    void draw_line_strip(const void* p,int n)override{geometry(Op::Line,p,n);}
    void fill_style_disable(int side)override{add(Op::FillOff).a=side;}
    void fill_style_color(int side,const rgba& c)override{auto& p=add(Op::FillColor);p.a=side;p.color=c;}
    void fill_style_bitmap(int side,bitmap_info* b,const matrix& m,bitmap_wrap_mode w,bitmap_blend_mode mode)override {
        retain(b);auto& p=add(Op::FillBitmap);p.a=side;p.b=w;p.c=mode;p.bitmap=b;p.m=m;
    }
    void line_style_disable()override{add(Op::LineOff);}
    void line_style_color(rgba c)override{add(Op::LineColor).color=c;}
    void line_style_width(float w)override{add(Op::LineWidth).width=w;}
    void draw_bitmap(const matrix& m,bitmap_info* b,const rect& r,const rect& uv,rgba c)override {
        retain(b);auto& p=add(Op::Bitmap);transformed(p,m);p.bitmap=b;p.bounds=r;p.uv=uv;p.color=c;
    }
    void set_antialiased(bool)override{}
    bool test_stencil_buffer(const rect&,Uint8)override{return false;}
    void begin_submit_mask()override{add(Op::MaskBegin);}
    void end_submit_mask()override{add(Op::MaskEnd);}
    void disable_mask()override{add(Op::MaskOff);}
    bool is_visible(const rect& r)override {
        return !frame || (r.m_x_max>=frame->x0 && r.m_x_min<=frame->x1 && r.m_y_max>=frame->y0 && r.m_y_min<=frame->y1);
    }
};
}
