// SWF placement filters. Original gameswf filter parsing was public domain.
#ifndef GAMESWF_FILTERS_H
#define GAMESWF_FILTERS_H
#include "gameswf_types.h"
#include <memory>
#include <vector>
#include <array>

namespace gameswf {
struct stream;
struct filter_descriptor {
    unsigned type=0,passes=0;
    rgba color,highlight;
    float blur_x=0,blur_y=0,angle=0,distance=0,strength=0;
    bool inner=false,knockout=false,composite=true,on_top=false,supported=true;
    std::array<float,20> matrix={1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0};
};
using filter_list=std::vector<filter_descriptor>;
std::shared_ptr<const filter_list> read_filter_list(stream* in);
}
#endif
