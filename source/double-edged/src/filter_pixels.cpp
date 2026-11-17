#include "raster_math.hpp"
#include "filter_pixels.hpp"
#include <algorithm>
#include <cmath>
#include <array>

namespace glorton {
namespace {
using Pixel=std::array<float,4>;
float unit(float x){return std::clamp(x,0.f,1.f);}
float scale_blur(float blur,float scale) {
    auto value=std::int64_t(std::round(blur*65536.f))-65536;
    auto factor=std::int64_t(scale*65536.f);
    return float((value*factor)>>16)/65536.f+1;
}
unsigned channel(std::uint32_t p,int c){return (p>>(c*8))&255;}
// The rasterizer unpacks several texels for every destination pixel. Preserve
// the exact single-precision division results while computing them once.
const std::array<float,256> normalized_channels=[] {
    std::array<float,256> result{};
    for(unsigned i=0;i<result.size();++i)result[i]=float(i)/255.f;
    return result;
}();
Pixel unpack(std::uint32_t p){return {normalized_channels[channel(p,0)],normalized_channels[channel(p,1)],normalized_channels[channel(p,2)],normalized_channels[channel(p,3)]};}
std::uint32_t pack(const Pixel& p) {
    std::uint32_t result=0;
    for(int c=0;c<4;++c)result|=std::uint32_t(std::clamp(int(glorton::raster_floor(p[c]*255.f+.5f)),0,255))<<(c*8);
    return result;
}
Pixel sample(const TextureView& texture,float x,float y) {
    auto fetch=[&](int x,int y) {
        if(texture.repeat){x=(x%texture.width+texture.width)%texture.width;y=(y%texture.height+texture.height)%texture.height;}
        else {x=std::clamp(x,0,texture.width-1);y=std::clamp(y,0,texture.height-1);}
        auto offset=y*texture.width+x;
        if(!texture.indices)return unpack(texture.pixels[offset]);
        unsigned index=texture.index_bits==4?(texture.indices[offset/2]>>((offset&1)*4))&15:texture.indices[offset];
        return unpack(texture.palette[index]);
    };
    if(!texture.linear)return fetch(int(glorton::raster_floor(x)),int(glorton::raster_floor(y)));
    x-=.5f;y-=.5f;int ix=int(glorton::raster_floor(x)),iy=int(glorton::raster_floor(y));float fx=x-ix,fy=y-iy;
    auto a=fetch(ix,iy),b=fetch(ix+1,iy),c=fetch(ix,iy+1),d=fetch(ix+1,iy+1);Pixel result;
    for(int k=0;k<4;++k)result[k]=(a[k]*(1-fx)+b[k]*fx)*(1-fy)+(c[k]*(1-fx)+d[k]*fx)*fy;
    return result;
}
float edge(const RasterVertex& a,const RasterVertex& b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
bool top_left(const RasterVertex& a,const RasterVertex& b){return b.y<a.y || (b.y==a.y && b.x>a.x);}

// Fractional box kernel with the same 8-bit edge weights and per-pass floor
// used by the independent reference renderer. Sliding sums keep large intro
// blurs linear in the number of pixels rather than in the kernel area.
template<bool AlphaOnly> void blur_axis(std::vector<std::uint32_t>& pixels,int width,int height,float full,bool horizontal) {
    full=std::min(full,255.f);if(full<=1)return;
    float radius=(full-1)*.5f;int m=int(glorton::raster_ceil(radius))-1;
    float alpha=glorton::raster_floor((radius-m)*255.f)/255.f;
    std::vector<std::uint32_t> output(pixels.size());
    int lines=horizontal?height:width,length=horizontal?width:height;
    for(int line=0;line<lines;++line) {
        auto at=[&](int i)->std::uint32_t {return i<0 || i>=length?0:pixels[horizontal?line*width+i:i*width+line];};
        int sums[4]={};
        for(int i=-m;i<=m;++i){auto p=at(i);for(int c=AlphaOnly?3:0;c<4;++c)sums[c]+=channel(p,c);}
        for(int i=0;i<length;++i) {
            auto before=at(i-m-1),after=at(i+m+1);std::uint32_t out=0;
            for(int c=AlphaOnly?3:0;c<4;++c) {
                float value=(sums[c]+alpha*(channel(before,c)+channel(after,c)))/full;
                out|=std::uint32_t(std::clamp(int(glorton::raster_floor(value)),0,255))<<(c*8);
            }
            output[horizontal?line*width+i:i*width+line]=out;
            auto remove=at(i-m),add=at(i+m+1);
            for(int c=AlphaOnly?3:0;c<4;++c)sums[c]+=int(channel(add,c))-int(channel(remove,c));
        }
    }
    pixels.swap(output);
}
template<bool AlphaOnly> void blur(std::vector<std::uint32_t>& pixels,int width,int height,float x,float y,unsigned passes) {
    for(unsigned i=0;i<passes;++i){blur_axis<AlphaOnly>(pixels,width,height,x,true);blur_axis<AlphaOnly>(pixels,width,height,y,false);}
}
float alpha_at(const std::vector<std::uint32_t>& pixels,int width,int height,float x,float y) {
    // The reference filter compositor uses point sampling for filter offsets.
    // Its blur passes have already applied their fractional linear kernels.
    int ix=int(glorton::raster_floor(x+.5f)),iy=int(glorton::raster_floor(y+.5f));
    return ix<0||iy<0||ix>=width||iy>=height?0.f:channel(pixels[iy*width+ix],3)/255.f;
}
}

FilterSurface::FilterSurface(int x,int y,int w,int h):left(x),top(y),width(w),height(h),pixels(w*h){}
void FilterSurface::begin_mask(){if(stencil.empty())stencil.resize(pixels.size());submitting_mask=true;++mask_level;}
void FilterSurface::end_mask(){submitting_mask=false;}
void FilterSurface::pop_mask(){if(!mask_level)return;for(auto& p:stencil)if(p==mask_level)--p;--mask_level;submitting_mask=false;}
void FilterSurface::triangle(RasterVertex a,RasterVertex b,RasterVertex c,const TextureView& texture) {
    float area=edge(a,b,c.x,c.y);if(std::fabs(area)<1e-9f)return;
    if(area<0){std::swap(b,c);area=-area;}
    int x0=std::max(left,int(glorton::raster_floor(std::min({a.x,b.x,c.x}))));
    int x1=std::min(left+width,int(glorton::raster_ceil(std::max({a.x,b.x,c.x}))));
    int y0=std::max(top,int(glorton::raster_floor(std::min({a.y,b.y,c.y}))));
    int y1=std::min(top+height,int(glorton::raster_ceil(std::max({a.y,b.y,c.y}))));
    auto ca=unpack(a.color),cb=unpack(b.color),cc=unpack(c.color);
    bool ab=top_left(a,b),bc=top_left(b,c),ca_edge=top_left(c,a);
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
        float e0=edge(b,c,x+.5f,y+.5f),e1=edge(c,a,x+.5f,y+.5f),e2=edge(a,b,x+.5f,y+.5f);
        if(e0<0 || (e0==0&&!bc) || e1<0 || (e1==0&&!ca_edge) || e2<0 || (e2==0&&!ab))continue;
        float wa=e0/area,wb=e1/area,wc=e2/area;
        Pixel src{1,1,1,1};
        if(texture.pixels||texture.indices)src=sample(texture,a.u*wa+b.u*wb+c.u*wc,a.v*wa+b.v*wb+c.v*wc);
        for(int k=0;k<4;++k)src[k]*=ca[k]*wa+cb[k]*wb+cc[k]*wc;
        unsigned index=(y-top)*width+x-left;
        if(submitting_mask){if(src[3]>0 && stencil[index]==mask_level-1)++stencil[index];continue;}
        if(mask_level && stencil[index]!=mask_level)continue;
        auto dest=unpack(pixels[index]);for(int k=0;k<3;++k)src[k]*=src[3];
        for(int k=0;k<4;++k)src[k]+=dest[k]*(1-src[3]);
        pixels[index]=pack(src);
    }
}

FilterPadding filter_padding(const gameswf::filter_list& filters,float sx,float sy) {
    FilterPadding result;
    for(const auto& f:filters) {
        if(f.type==6)continue;
        float dx=f.type==0||f.type==3?std::fabs(std::cos(f.angle)*f.distance*sx):0;
        float dy=f.type==0||f.type==3?std::fabs(std::sin(f.angle)*f.distance*sy):0;
        result.x+=int(glorton::raster_ceil(std::max(0.f,std::min(255.f,scale_blur(f.blur_x,sx))-1)*.5f))*f.passes+int(glorton::raster_ceil(dx))+2;
        result.y+=int(glorton::raster_ceil(std::max(0.f,std::min(255.f,scale_blur(f.blur_y,sy))-1)*.5f))*f.passes+int(glorton::raster_ceil(dy))+2;
    }
    return result;
}
void FilterSurface::apply(const gameswf::filter_list& filters,float sx,float sy) {
    for(const auto& f:filters) {
        if(!f.supported)continue;
        if(f.type==6){
            std::array<float,4> bias;
            for(int row=0;row<4;++row)bias[row]=f.matrix[row*5+4]/255.f;
            bool diagonal=f.matrix[15]==0&&f.matrix[16]==0&&f.matrix[17]==0&&f.matrix[18]==1&&f.matrix[19]==0;
            for(int row=0;row<3;++row)for(int column=0;column<4;++column)
                if((row!=column&&f.matrix[row*5+column]!=0)||!std::isfinite(f.matrix[row*5+column]))diagonal=false;
            for(float value:bias)if(!std::isfinite(value))diagonal=false;
            std::array<std::array<std::uint32_t,256>,3> opaque;
            if(diagonal)for(int row=0;row<3;++row)for(unsigned value=0;value<256;++value){
                float result=unit(f.matrix[row*5+row]*normalized_channels[value]+bias[row]);
                opaque[row][value]=std::uint32_t(std::clamp(int(glorton::raster_floor(result*255.f+.5f)),0,255))<<(row*8);
            }
            for(auto& pixel:pixels){
                // Original theme2/3 filters are diagonal and preserve alpha.
                // For opaque pixels the exact float calculation has only256
                // possible inputs per channel. Translucent pixels retain the
                // full premultiplied path, including all original rounding.
                if(diagonal&&(pixel>>24)==255){pixel=0xff000000|opaque[0][channel(pixel,0)]|opaque[1][channel(pixel,1)]|opaque[2][channel(pixel,2)];continue;}
                Pixel input=unpack(pixel);if(input[3]>0&&input[3]<1)for(int i=0;i<3;i++)input[i]/=input[3];
                Pixel output;
                for(int row=0;row<4;row++){const auto m=f.matrix.data()+row*5;output[row]=unit(m[0]*input[0]+m[1]*input[1]+m[2]*input[2]+m[3]*input[3]+bias[row]);}
                for(int i=0;i<3;i++)output[i]*=output[3];pixel=pack(output);
            }
            continue;
        }
        float bx=scale_blur(f.blur_x,sx),by=scale_blur(f.blur_y,sy);
        if(f.type==1){blur<false>(pixels,width,height,bx,by,f.passes);continue;}
        // Glow, shadow and bevel sample only the blurred alpha channel. Color
        // channels cannot influence their result and need no blur arithmetic.
        auto blurred=pixels;blur<true>(blurred,width,height,bx,by,f.passes);
        float distance=glorton::raster_floor(f.distance*std::int64_t(sy*65536.f))/65536.f;
        float dx=(f.type==0||f.type==3)?std::cos(f.angle)*distance:0;
        float dy=(f.type==0||f.type==3)?std::sin(f.angle)*distance:0;
        if((f.type==0 || f.type==2) && !f.inner && !f.knockout && f.composite &&
           f.color.m_r==0 && f.color.m_g==0 && f.color.m_b==0 && f.color.m_a==255) {
            // An opaque black outer glow preserves premultiplied RGB. Its
            // alpha can be evaluated exactly as a rational 8.8 expression,
            // avoiding four-channel float unpack/composite/repack on the PSP.
            unsigned strength=unsigned(std::round(f.strength*256));
            int ox=int(glorton::raster_floor(-dx+.5f)),oy=int(glorton::raster_floor(-dy+.5f));
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                unsigned index=y*width+x,alpha=pixels[index]>>24;
                int sx=x+ox,sy=y+oy;
                unsigned a=sx<0||sy<0||sx>=width||sy>=height?0:blurred[sy*width+sx]>>24;
                unsigned a256=std::min(65280u,a*strength);
                unsigned out=alpha+(a256*(255-alpha)+32640)/65280;
                pixels[index]=(pixels[index]&0xffffff)|(out<<24);
            }
            continue;
        }
        Pixel shadow{f.color.m_r/255.f,f.color.m_g/255.f,f.color.m_b/255.f,f.color.m_a/255.f};
        Pixel highlight{f.highlight.m_r/255.f,f.highlight.m_g/255.f,f.highlight.m_b/255.f,f.highlight.m_a/255.f};
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            unsigned i=y*width+x;auto dest=unpack(pixels[i]);Pixel out{};
            if(f.type==3) {
                float l=alpha_at(blurred,width,height,x+dx,y+dy),r=alpha_at(blurred,width,height,x-dx,y-dy);
                float hi=unit((l-r)*f.strength),lo=unit((r-l)*f.strength);Pixel glow;
                for(int k=0;k<3;++k)glow[k]=highlight[k]*highlight[3]*hi+shadow[k]*shadow[3]*lo;
                glow[3]=highlight[3]*hi+shadow[3]*lo;
                bool inner=f.inner||f.on_top,outer=!f.inner||f.on_top;
                for(int k=0;k<4;++k) {
                    if(inner&&outer)out[k]=glow[k]+(f.knockout?0:dest[k]*(1-glow[3]));
                    else if(inner)out[k]=glow[k]*dest[3]+(f.knockout?0:dest[k]*(1-glow[3]));
                    else out[k]=glow[k]*(1-dest[3])+(f.knockout?0:dest[k]);
                }
            } else {
                float b=alpha_at(blurred,width,height,x-dx,y-dy);
                float alpha=shadow[3]*unit((f.inner?1-b:b)*f.strength);
                float factor=f.inner?dest[3]:f.knockout||f.composite?1-dest[3]:1;
                for(int k=0;k<4;++k) {
                    float color=k==3?1:shadow[k];out[k]=color*alpha*factor;
                    if(f.composite&&!f.knockout)out[k]+=dest[k]*(f.inner?1-alpha:1);
                }
            }
            pixels[i]=pack(out);
        }
    }
}
void FilterSurface::unpremultiply() {
    for(auto& p:pixels){unsigned a=channel(p,3);if(a==255)continue;if(!a){p=0;continue;}std::uint32_t out=a<<24;
        for(int c=0;c<3;++c)out|=std::min(255u,(channel(p,c)*255+a/2)/a)<<(c*8);p=out;}
}
}
