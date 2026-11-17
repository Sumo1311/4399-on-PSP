#include "raster_math.hpp"
#include "device_font.hpp"
#include "device_font_data.hpp"
#include "gameswf/gameswf_render.h"
#include "gameswf/gameswf_character.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <vector>
#include <zlib.h>
namespace {
struct DeviceFont final : gameswf::glyph_provider {
    struct SourceGlyph {uint32_t id;int16_t left,top;uint16_t width,height;uint32_t offset,size;};
    static_assert(sizeof(SourceGlyph)==20);
    FILE* source=nullptr;
    std::vector<SourceGlyph> source_glyphs;
    std::map<uint64_t,gameswf::gc_ptr<gameswf::glyph_entity>> cache;
    gameswf::render_handler* factory=nullptr;
    explicit DeviceFont(gameswf::render_handler* renderer):factory(renderer) {
        source=std::fopen("fonts.pak","rb");if(!source)return;
        uint32_t header[3];
        if(std::fread(header,sizeof(header),1,source)!=1 || header[0]!=0x4e464c47 || header[1]!=1 || header[2]>8192)std::abort();
        source_glyphs.resize(header[2]);
        if(std::fread(source_glyphs.data(),sizeof(SourceGlyph),source_glyphs.size(),source)!=source_glyphs.size())std::abort();
    }
    ~DeviceFont() override {if(source)std::fclose(source);}
    bool get_font_metrics(const tu_string&,bool,bool,float* units,float* ascent,float* descent) override {
        *units=glorton::device_font_units;*ascent=glorton::device_font_ascent;*descent=glorton::device_font_descent;return true;
    }
    gameswf::bitmap_info* get_char_image(gameswf::character_def* embedded,Uint16 code,const tu_string&,bool,bool,int size,gameswf::rect* bounds,float* advance) override {
        if(size<=0 || size>1024)return nullptr;
        SourceGlyph original{};
        if(embedded) {
            auto found=std::lower_bound(source_glyphs.begin(),source_glyphs.end(),uint32_t(embedded->get_resource_id()),[](const SourceGlyph& g,uint32_t id){return g.id<id;});
            if(found==source_glyphs.end() || found->id!=uint32_t(embedded->get_resource_id()))return nullptr;
            original=*found;
        } else if(code<32 || code>126)return nullptr;
        auto glyph=embedded?glorton::DeviceGlyph{code,original.left,original.top,original.width,original.height,512,original.offset,original.size}:glorton::device_glyphs[code-32];
        uint64_t key=(uint64_t(size)<<32)|(embedded?original.id:code);
        auto found=cache.find(key);
        if(found==cache.end()) {
            std::vector<uint8_t> alpha(glyph.width*glyph.height);uLongf alpha_size=alpha.size();
            std::vector<uint8_t> packed;
            const uint8_t* bytes=embedded?nullptr:glorton::device_font_compressed+glyph.offset;
            if(embedded){packed.resize(glyph.size);if(std::fseek(source,glyph.offset,SEEK_SET) || std::fread(packed.data(),1,packed.size(),source)!=packed.size())std::abort();bytes=packed.data();}
            if(uncompress(alpha.data(),&alpha_size,bytes,glyph.size)!=Z_OK || alpha_size!=alpha.size())std::abort();
            auto item=new gameswf::glyph_entity;
            const float scale=float(size)/128;
            int w=std::max(1,int(glorton::raster_ceil(glyph.width*scale))),h=std::max(1,int(glorton::raster_ceil(glyph.height*scale)));
            std::vector<uint8_t> pixels(w*h);
            auto sample=[&](int x,int y)->float { if(x<0||y<0||x>=glyph.width||y>=glyph.height)return 0;return alpha[y*glyph.width+x]; };
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                float sx=(x+.5f)/scale-.5f,sy=(y+.5f)/scale-.5f;int ix=int(glorton::raster_floor(sx)),iy=int(glorton::raster_floor(sy));float fx=sx-ix,fy=sy-iy;
                float a=sample(ix,iy)*(1-fx)+sample(ix+1,iy)*fx,b=sample(ix,iy+1)*(1-fx)+sample(ix+1,iy+1)*fx;
                pixels[y*w+x]=uint8_t(std::clamp(a*(1-fy)+b*fy,0.f,255.f));
            }
            item->m_bi=factory?factory->create_bitmap_info_alpha(w,h,pixels.data()):gameswf::render::create_bitmap_info_alpha(w,h,pixels.data());
            if(!item->m_bi.get_ptr()){delete item;return nullptr;}
            item->m_advance=glyph.advance;
            item->m_bounds.m_x_min=-glyph.left*scale/w;item->m_bounds.m_y_min=-glyph.top*scale/h;
            item->m_bounds.m_x_max=1;item->m_bounds.m_y_max=1;
            found=cache.emplace(key,item).first;
        }
        *bounds=found->second->m_bounds;if(advance)*advance=found->second->m_advance;return found->second->m_bi.get_ptr();
    }
};
}
gameswf::glyph_provider* make_device_font_provider(gameswf::render_handler* factory) { return new DeviceFont(factory); }
