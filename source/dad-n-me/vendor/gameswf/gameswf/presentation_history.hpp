#pragma once
#include <cmath>
#include <cstdint>

namespace gameswf::presentation {
// Render-only history. Physics, hit tests and script getters use m_matrix.
inline unsigned tick = 0;
inline unsigned serial=0;
inline float alpha = 1;
inline unsigned moving_samples = 0;
inline void begin(unsigned source_tick, float fraction) {
    ++serial;
    tick = source_tick;
    alpha = fraction;
    moving_samples = 0;
}
struct History {
    mutable matrix previous,current,render_previous;
    mutable unsigned sampled_tick=~0u,interpolated_tick=~0u;
    mutable const void* parent=nullptr;
    mutable bool moving=false;
    matrix sample(const matrix& value,const void* owner_parent) const {
        if(sampled_tick!=tick){
            previous=sampled_tick!=~0u&&sampled_tick+1==tick&&parent==owner_parent?current:value;
            current=value;sampled_tick=tick;parent=owner_parent;
        }
        if(alpha>=1)return current;
        if(interpolated_tick!=tick){
            render_previous=current;
            float dx=current.m_[0][2]-previous.m_[0][2],dy=current.m_[1][2]-previous.m_[1][2];
            bool compatible=std::isfinite(dx)&&std::isfinite(dy)&&std::fabs(dx)<4000&&std::fabs(dy)<4000&&
                (current.m_[0][0]*current.m_[1][1]-current.m_[0][1]*current.m_[1][0])*
                (previous.m_[0][0]*previous.m_[1][1]-previous.m_[0][1]*previous.m_[1][0])>0;
            if(compatible){
                render_previous.m_[0][2]=previous.m_[0][2];render_previous.m_[1][2]=previous.m_[1][2];
                float alignment=0;for(int y=0;y<2;++y)for(int x=0;x<2;++x)alignment+=float(previous.m_[y][x])*float(current.m_[y][x]);
                if(alignment>0)for(int y=0;y<2;++y)for(int x=0;x<2;++x)render_previous.m_[y][x]=previous.m_[y][x];
            }
            moving=current!=render_previous;interpolated_tick=tick;
        }
        if(moving)++moving_samples;
        if(alpha<=0)return render_previous;
        matrix result;for(int y=0;y<2;++y)for(int x=0;x<3;++x)result.m_[y][x]=float(render_previous.m_[y][x])+(float(current.m_[y][x])-float(render_previous.m_[y][x]))*alpha;
        return result;
    }
};
}
