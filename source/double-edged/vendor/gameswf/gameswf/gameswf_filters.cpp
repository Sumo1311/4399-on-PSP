// SWF placement filters. Original gameswf filter parsing was public domain.
#include "gameswf/gameswf_filters.h"
#include "gameswf/gameswf_stream.h"
#include "gameswf/gameswf_log.h"
#include <stdexcept>

namespace gameswf {
std::shared_ptr<const filter_list> read_filter_list(stream* in) {
    auto result=std::make_shared<filter_list>();
    unsigned count=in->read_u8();result->reserve(count);
    for(unsigned i=0;i<count;++i) {
        filter_descriptor f;f.type=in->read_u8();
        if(f.type==0 || f.type==2 || f.type==3) {
            if(f.type==3){f.highlight.read_rgba(in);f.color.read_rgba(in);}
            else f.color.read_rgba(in);
            f.blur_x=in->read_fixed();f.blur_y=in->read_fixed();
            if(f.type!=2){f.angle=in->read_fixed();f.distance=in->read_fixed();}
            // FIXED8 is the full little-endian 8.8 strength, not one signed byte.
            f.strength=in->read_u16()/256.f;
            unsigned flags=in->read_u8();
            f.inner=flags&0x80;f.knockout=flags&0x40;f.composite=flags&0x20;
            f.on_top=f.type==3 && (flags&0x10);f.passes=flags&(f.type==3?15:31);
        } else if(f.type==1) {
            f.blur_x=in->read_fixed();f.blur_y=in->read_fixed();f.passes=in->read_u8()>>3;
        } else if(f.type==4 || f.type==7) {
            unsigned colors=in->read_u8();
            for(unsigned j=0;j<colors*5;++j)in->read_u8();
            for(int j=0;j<4;++j)in->read_fixed();
            in->read_u16();in->read_u8();f.supported=false;
        } else if(f.type==5) {
            unsigned columns=in->read_u8(),rows=in->read_u8();
            in->read_float();in->read_float();
            for(unsigned j=0;j<columns*rows;++j)in->read_float();
            for(int j=0;j<5;++j)in->read_u8();f.supported=false;
        } else if(f.type==6) {
            for(auto& value:f.matrix)value=in->read_float();
        } else throw std::runtime_error("Unknown SWF placement filter");
        result->push_back(f);
    }
    return result;
}
}
