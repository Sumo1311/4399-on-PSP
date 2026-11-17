#include "device_font.hpp"
#include "device_font_data.hpp"
#include "gameswf/gameswf_render.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <vector>
#include <zlib.h>
namespace {
struct DeviceFont final : gameswf::glyph_provider {
    std::map<uint32_t,gameswf::gc_ptr<gameswf::glyph_entity>> cache;
    gameswf::bitmap_info* get_char_image(gameswf::character_def* embedded,Uint16 code,const tu_string&,bool,bool,int size,gameswf::rect* bounds,float* advance) override {
        if(embedded || code<32 || code>126 || size<=0 || size>1024)return nullptr;
        const auto& glyph=dnm::device_glyphs[code-32];
        uint32_t key=(uint32_t(size)<<16)|code;
        auto found=cache.find(key);
        if(found==cache.end()) {
            std::vector<uint8_t> alpha(glyph.width*glyph.height);uLongf alpha_size=alpha.size();
            if(uncompress(alpha.data(),&alpha_size,dnm::device_font_compressed+glyph.offset,glyph.size)!=Z_OK || alpha_size!=alpha.size())std::abort();
            auto item=new gameswf::glyph_entity;
            const float scale=float(size)/128;
            int w=std::max(1,int(std::ceil(glyph.width*scale))),h=std::max(1,int(std::ceil(glyph.height*scale)));
            std::vector<uint8_t> pixels(w*h);
            auto sample=[&](int x,int y)->float { if(x<0||y<0||x>=glyph.width||y>=glyph.height)return 0;return alpha[y*glyph.width+x]; };
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                float sx=(x+.5f)/scale-.5f,sy=(y+.5f)/scale-.5f;int ix=int(std::floor(sx)),iy=int(std::floor(sy));float fx=sx-ix,fy=sy-iy;
                float a=sample(ix,iy)*(1-fx)+sample(ix+1,iy)*fx,b=sample(ix,iy+1)*(1-fx)+sample(ix+1,iy+1)*fx;
                pixels[y*w+x]=uint8_t(std::clamp(a*(1-fy)+b*fy,0.f,255.f));
            }
            item->m_bi=gameswf::render::create_bitmap_info_alpha(w,h,pixels.data());
            if(!item->m_bi.get_ptr()){delete item;return nullptr;}
            item->m_advance=glyph.advance;
            item->m_bounds.m_x_min=-glyph.left*scale/w;item->m_bounds.m_y_min=-glyph.top*scale/h;
            item->m_bounds.m_x_max=1;item->m_bounds.m_y_max=1;
            found=cache.emplace(key,item).first;
        }
        *bounds=found->second->m_bounds;*advance=found->second->m_advance;return found->second->m_bi.get_ptr();
    }
};
}
gameswf::glyph_provider* make_device_font_provider() { return new DeviceFont; }
