#ifndef DNM_TEXTURE_CACHE_MB
#define DNM_TEXTURE_CACHE_MB 24
#endif
#include "psp_frame_pacer.hpp"
#include "boot_trace.hpp"
#include "frame_profile.hpp"
#include "../vendor/lz4/lz4.h"
#include "render_psp.hpp"
#include "recorded_renderer.hpp"
#include "button_hit_shapes.hpp"
#include "gameswf/gameswf_types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <memory>
#include <unordered_map>
#include <new>
#include <cstdio>
#include <malloc.h>
#include <zlib.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
extern "C" void dnm_graphics_trace(unsigned,unsigned,unsigned,unsigned);
extern "C" void dnm_cache_trace(unsigned,unsigned,unsigned,unsigned,unsigned);

namespace {
using namespace gameswf;
void* profile_malloc(size_t n){perf::Scope timer(perf::ALLOC);return std::malloc(n);}
void* profile_memalign(size_t alignment,size_t n){perf::Scope timer(perf::ALLOC);return memalign(alignment,n);}
constexpr int Stride=512, Width=480, Height=272;
constexpr int BufferBytes=Stride*Height*4;
alignas(64) uint32_t commands[256*1024];
void* draw_buffer=nullptr;
void* displayed_buffer=nullptr;
bool active_list=false;
PspFramePacer present_pacer;
bool baked_art_ready=false;
unsigned cache_hits=0,cache_misses=0,cache_decoded=0;
unsigned list_bytes=0;
unsigned frame_draws=0,largest_allocation=0,total_bytes=0;
void flush_gpu() {
    if(active_list) { sceGuFinish();{perf::Scope timer(perf::SYNC);sceGuSync(0,0);}sceGuStart(GU_DIRECT,commands);list_bytes=0; }
}
void* vertices(unsigned bytes) {
    ++frame_draws;largest_allocation=std::max(largest_allocation,bytes);total_bytes+=bytes;
    if(list_bytes+bytes+512>700*1024)flush_gpu();
    list_bytes+=bytes+512;return sceGuGetMemory(bytes);
}
uint32_t color(rgba c) { return c.m_r|(uint32_t(c.m_g)<<8)|(uint32_t(c.m_b)<<16)|(uint32_t(c.m_a)<<24); }
int power2(int n) { int v=1;while(v<n) v*=2;return v; }
uint8_t byte(float f) { return uint8_t(std::clamp(f,0.f,255.f)); }

struct ArtReadCache {
    static constexpr unsigned Capacity=64*1024;
    std::unique_ptr<uint8_t,decltype(&std::free)> data{nullptr,std::free};
    SceUID fd=-1;unsigned file_size=0,begin=0,valid=0;
    void open(SceUID source,unsigned size){
        fd=source;file_size=size;
        // Optional bounded read-ahead. Allocation failure retains streaming.
        if(fd>=0)data.reset(static_cast<uint8_t*>(profile_malloc(Capacity)));
    }
    const uint8_t* read(unsigned offset,unsigned size){
        if(!data||size>Capacity||offset>file_size||size>file_size-offset)return nullptr;
        if(offset>=begin&&size<=valid&&offset-begin<=valid-size)return data.get()+offset-begin;
        unsigned count=std::min(Capacity,file_size-offset);valid=0;
        {perf::Scope timer(perf::IO);
            if(sceIoLseek32(fd,offset,PSP_SEEK_SET)<0||sceIoRead(fd,data.get(),count)!=int(count))return nullptr;
        }
        begin=offset;valid=count;return data.get();
    }
};

struct Bitmap final : bitmap_info {
    int width=0,height=0,tw=0,th=0,rw=0,rh=0;
    uint32_t* data=nullptr;
    std::vector<uint8_t> compressed;
    FILE* bank=nullptr;
    SceUID bank_fd=-1;
    uint32_t bank_offset=0,bank_size=0;
    const uint8_t* bank_memory=nullptr;
    ArtReadCache* bank_cache=nullptr;
    Bitmap* tint_source=nullptr;
    cxform pixel_tint;
    bool apply_tint=false,paletted=false,four_bit=false,bank_lz4=false,swizzled=false;
    unsigned palette_offset()const{return ((tw*th/(four_bit?2:1)+63)&~63);}
    unsigned palette_bytes()const{return four_bit?64:1024;}
    unsigned storage_bytes() const {return paletted?palette_offset()+palette_bytes():tw*th*4;}
    unsigned used=0;
    static std::vector<Bitmap*> resident;
    static unsigned resident_bytes,clock;
    static bool evict_oldest() {
        perf::Scope timer(perf::EVICT);if(perf::enabled)++perf::evictions;
        if(resident.empty())return false;
        auto oldest=std::min_element(resident.begin(),resident.end(),[](Bitmap* a,Bitmap* b){return a->used<b->used;});
        flush_gpu();auto victim=*oldest;std::free(victim->data);victim->data=nullptr;
        resident_bytes-=victim->storage_bytes();resident.erase(oldest);return true;
    }
    Bitmap()=default;
    Bitmap(int w,int h,const uint8_t* bytes,int channels,int pitch):width(w),height(h) {
        if(baked_art_ready && channels>=3)return;
        float scale=std::min(std::max(w,h)>64?.625f:1.f,512.f/std::max(w,h));
        rw=std::max(1,int(std::ceil(w*scale))); rh=std::max(1,int(std::ceil(h*scale)));
        tw=power2(rw);th=power2(rh);
        data=static_cast<uint32_t*>(profile_memalign(16,tw*th*4));
        if(!data) { tw=th=0; return; }
        for(int y=0;y<th;y++) for(int x=0;x<tw;x++) {
            int sx=std::min(w-1,int(std::min(x,rw-1)/scale));
            int sy=std::min(h-1,int(std::min(y,rh-1)/scale));
            auto p=bytes+sy*pitch+sx*channels;
            data[y*tw+x]=channels==1 ? (0xffffffu|(uint32_t(p[0])<<24)) :
                uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|
                (uint32_t(channels==4?p[3]:255)<<24);
        }
        uLongf size=compressBound(tw*th*4);compressed.resize(size);
        if(compress2(compressed.data(),&size,reinterpret_cast<uint8_t*>(data),tw*th*4,1)!=Z_OK)std::abort();
        compressed.resize(size);compressed.shrink_to_fit();std::free(data);data=nullptr;
    }
    void activate() override {
        used=++clock;if(data){++cache_hits;return;}if(!tw||(compressed.empty()&&!bank&&!tint_source))return;
        unsigned size=storage_bytes();
        ++cache_misses;if(perf::enabled)++perf::misses;
        while(!resident.empty() && resident_bytes+size>DNM_TEXTURE_CACHE_MB*1024*1024) {
            evict_oldest();
        }
        std::unique_ptr<uint8_t,decltype(&std::free)> packed(nullptr,std::free);
        const uint8_t* cached=bank&&!tint_source&&!bank_memory&&bank_cache?bank_cache->read(bank_offset,bank_size):nullptr;
        if(bank && !tint_source && !bank_memory && !cached) {
            do { packed.reset(static_cast<uint8_t*>(profile_malloc(bank_size))); }while(!packed && evict_oldest());
            if(!packed)throw std::bad_alloc();
            {perf::Scope timer(perf::IO);
                bool valid=bank_fd>=0?(sceIoLseek32(bank_fd,bank_offset,PSP_SEEK_SET)>=0 && sceIoRead(bank_fd,packed.get(),bank_size)==int(bank_size)):
                    (std::fseek(bank,bank_offset,SEEK_SET)==0 && std::fread(packed.get(),1,bank_size,bank)==bank_size);
                if(!valid)std::abort();}
        }
        do { data=static_cast<uint32_t*>(profile_memalign(16,size)); }while(!data && evict_oldest());
        if(!data)throw std::bad_alloc();
        if(tint_source) {
            // Keep the immutable decoded image in the same bounded LRU. A changing
            // colour transform must not reread and inflate that image each frame.
            // The output is not resident yet, so source activation cannot evict it.
            tint_source->activate();
            std::memcpy(data,tint_source->data,size);
        } else {
            cache_decoded+=size;if(perf::enabled)perf::decoded+=size;
            perf::Scope timer(perf::DECODE);uLongf bytes=size;
            auto encoded=bank_memory?bank_memory+bank_offset:cached?cached:bank?packed.get():compressed.data();
            if(bank_lz4){if(LZ4_decompress_safe(reinterpret_cast<const char*>(encoded),reinterpret_cast<char*>(data),bank_size,size)!=int(size))std::abort();}
            else if(uncompress(reinterpret_cast<uint8_t*>(data),&bytes,encoded,bank?bank_size:compressed.size())!=Z_OK || bytes!=size)std::abort();
        }
        if(apply_tint) {
            perf::Scope timer(perf::FILTER);
            // Every channel is an independent function of an 8-bit input. These
            // tables evaluate the original float expression for all 256 inputs;
            // there is no colour quantisation or different rounding operation.
            uint8_t lookup[4][256];
            for(int c=0;c<4;++c)for(int v=0;v<256;++v)
                lookup[c][v]=byte(v*pixel_tint.m_[c][0]+pixel_tint.m_[c][1]);
            auto pixels=reinterpret_cast<uint8_t*>(data)+(paletted?palette_offset():0);
            for(unsigned i=0;i<(paletted?palette_bytes():size);i+=4) {
                if(!pixels[i+3])continue;
                for(int c=0;c<4;c++)pixels[i+c]=lookup[c][pixels[i+c]];
            }
        }
        resident.push_back(this);resident_bytes+=size;sceKernelDcacheWritebackRange(data,size);
    }
    ~Bitmap() override {
        if(data) { flush_gpu();resident_bytes-=storage_bytes();resident.erase(std::remove(resident.begin(),resident.end(),this),resident.end());std::free(data); }
    }
    int get_width() const override { return width; }
    int get_height() const override { return height; }
    unsigned char* get_data() const override { return reinterpret_cast<unsigned char*>(data); }
    int get_bpp() const override { return 4; }
};
std::vector<Bitmap*> Bitmap::resident;
unsigned Bitmap::resident_bytes=0,Bitmap::clock=0;
struct Vertex { float u,v; uint32_t color; float x,y,z; };
constexpr int VertexFormat=GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D;

struct Renderer final : gameswf::render_handler {
    struct ArtEntry { uint32_t id;uint16_t ratio,reserved;uint32_t first,count; };
    struct ArtTile { uint32_t offset,size;uint16_t tw,th,width,height;float left,top,w,h; };
    static_assert(sizeof(ArtEntry)==16 && sizeof(ArtTile)==32);
    FILE* art_file=nullptr;
    SceUID art_fd=-1;
    std::vector<ArtEntry> art_entries;
    std::vector<rect> art_bounds;
    std::vector<int> art_zero_entry;
    std::vector<uint8_t> compressed_art;
    ArtReadCache streamed_art;
    std::vector<ArtTile> art_tiles;
    std::vector<unsigned> art_formats;
    std::vector<std::shared_ptr<Bitmap>> art_textures;
    std::vector<unsigned> art_canonical;
    struct Variant { unsigned tile;cxform tint;std::shared_ptr<Bitmap> texture; };
    std::vector<Variant> variants;
    const ArtEntry* find_art(unsigned id,Uint16 ratio) const {
        if(!baked_art_ready)return nullptr;
        if(!ratio&&id<art_zero_entry.size()){
            int index=art_zero_entry[id];return index<0?nullptr:&art_entries[index];
        }
        auto e=std::lower_bound(art_entries.begin(),art_entries.end(),std::make_pair(id,ratio),
            [](const ArtEntry& e,auto k){return e.id<k.first||(e.id==k.first&&e.ratio<k.second);});
        return e!=art_entries.end()&&e->id==id&&e->ratio==ratio?&*e:nullptr;
    }
    bool can_release_shape_geometry(int id) const override {
        if(!baked_art_ready || std::binary_search(dnm::button_hit_shapes.begin(),dnm::button_hit_shapes.end(),unsigned(id)))return false;
        auto it=std::lower_bound(art_entries.begin(),art_entries.end(),unsigned(id),[](const ArtEntry& e,unsigned id){return e.id<id;});
        return it!=art_entries.end() && it->id==unsigned(id) && it->ratio==0;
    }
    bool submitting_mask=false;
    matrix transform;
    cxform tint;
    struct Style { bool enabled=false; Bitmap* bitmap=nullptr; matrix uv; rgba color; bitmap_wrap_mode wrap=WRAP_CLAMP; } fills[2],line;
    float line_width=20, sx=1,sy=1,ox=0,oy=0,movie_width=12800,movie_height=9600;
    int mask_level=0;
    bool drawing=false;
    rect local_view;
    ~Renderer() override { if(art_fd>=0)sceIoClose(art_fd);if(art_file)std::fclose(art_file);baked_art_ready=false; }
    void load_art() {
        art_file=std::fopen("art.pak","rb");if(!art_file)return;
        uint32_t header[5];
        if(std::fread(header,sizeof(header),1,art_file)!=1 || header[0]!=0x52414e44 || header[1]<1 || header[1]>6 || header[2]>20000 || header[3]>20000)std::abort();
        art_entries.resize(header[2]);art_tiles.resize(header[3]);art_textures.resize(header[3]);art_formats.resize(header[3]);
        if(std::fread(art_entries.data(),sizeof(ArtEntry),art_entries.size(),art_file)!=art_entries.size())std::abort();
        for(unsigned i=0;i<art_tiles.size();++i){
            if(std::fread(&art_tiles[i],sizeof(ArtTile),1,art_file)!=1)std::abort();
            if(header[1]>=2 && (std::fread(&art_formats[i],4,1,art_file)!=1 || art_formats[i]>(header[1]>=6?31u:header[1]==5?15u:header[1]==4?7u:header[1]==3?3u:1u)))std::abort();
            if((art_formats[i]&8)&&(!(art_formats[i]&1)||art_tiles[i].tw<32))std::abort();
            if(art_formats[i]&16){
                unsigned row=art_tiles[i].tw*((art_formats[i]&1)?1:4);if(art_formats[i]&8)row/=2;
                if(row<16 || row%16 || art_tiles[i].th%8)std::abort();
            }
        }
        // Immutable raster geometry, shared read-only with the recording
        // thread. This bounds drawing only; source hit-test shapes are intact.
        unsigned max_art_id=0;for(const auto& e:art_entries)max_art_id=std::max(max_art_id,unsigned(e.id));
        art_zero_entry.assign(max_art_id+1,-1);
        for(unsigned i=0;i<art_entries.size();++i)if(!art_entries[i].ratio)art_zero_entry[art_entries[i].id]=int(i);
        art_bounds.resize(art_entries.size());
        for(unsigned i=0;i<art_entries.size();++i){
            auto& b=art_bounds[i];b.m_x_min=b.m_y_min=1e30f;b.m_x_max=b.m_y_max=-1e30f;
            const auto& e=art_entries[i];
            for(unsigned n=e.first;n<e.first+e.count;++n){const auto& t=art_tiles[n];
                b.m_x_min=std::min(b.m_x_min,t.left);b.m_y_min=std::min(b.m_y_min,t.top);
                b.m_x_max=std::max(b.m_x_max,t.left+t.w);b.m_y_max=std::max(b.m_y_max,t.top+t.h);
            }
        }
        art_canonical.resize(art_tiles.size());std::unordered_map<uint32_t,unsigned> first;
        for(unsigned i=0;i<art_tiles.size();++i){
            auto [it,inserted]=first.emplace(art_tiles[i].offset,i);art_canonical[i]=it->second;
            if(!inserted){const auto& a=art_tiles[i];const auto& b=art_tiles[it->second];
                if(a.size!=b.size||a.tw!=b.tw||a.th!=b.th||(art_formats[i]&~4u)!=(art_formats[it->second]&~4u))std::abort();}
        }
        art_fd=sceIoOpen("art.pak",PSP_O_RDONLY,0);
        // The reduced-resolution bank fits in a bounded resident input buffer.
        // Decoded textures still use the LRU; cold draws no longer seek media.
        if(std::fseek(art_file,0,SEEK_END)==0){long size=std::ftell(art_file);if(size>0&&size<=6*1024*1024){compressed_art.resize(size);std::rewind(art_file);if(std::fread(compressed_art.data(),1,size,art_file)!=unsigned(size))std::abort();}else if(size>0)streamed_art.open(art_fd,unsigned(size));}
        baked_art_ready=true;
    }
    std::shared_ptr<Bitmap> art_texture(unsigned index,const cxform* tint=nullptr) {
        unsigned canonical=art_canonical[index];
        if(!tint&&canonical!=index){if(!art_textures[canonical])art_textures[canonical]=art_texture(canonical);return art_textures[canonical];}
        const auto& tile=art_tiles[index];auto b=std::make_shared<Bitmap>();
        b->width=b->rw=tile.width;b->height=b->rh=tile.height;b->tw=tile.tw;b->th=tile.th;
        b->paletted=(art_formats[index]&1)!=0;b->four_bit=(art_formats[index]&8)!=0;b->bank_lz4=(art_formats[index]&2)!=0;
        b->swizzled=(art_formats[index]&16)!=0;
        b->bank=art_file;b->bank_fd=art_fd;b->bank_offset=tile.offset;b->bank_size=tile.size;
        b->bank_memory=compressed_art.empty()?nullptr:compressed_art.data();
        b->bank_cache=compressed_art.empty()?&streamed_art:nullptr;
        if(tint){
            if(!art_textures[index])art_textures[index]=art_texture(index);
            b->tint_source=art_textures[index].get();
            b->pixel_tint=*tint;b->apply_tint=true;
        }
        return b;
    }
    bool has_baked_shape(int id,Uint16 ratio) const override {return find_art(unsigned(id),ratio)!=nullptr;}
    bool baked_shape_bounds(int id,Uint16 ratio,rect* bounds) const override {
        auto e=find_art(unsigned(id),ratio);if(!e)return false;
        *bounds=art_bounds[e-art_entries.data()];return true;
    }
    bool draw_shape(int id,const matrix& matrix,const cxform& tint,Uint16 ratio) override {
        auto entry=find_art(unsigned(id),ratio);if(!entry)return false;
        bool cpu_tint=false;
        for(int c=0;c<4;c++)if(tint.m_[c][1]!=0 || tint.m_[c][0]<0 || tint.m_[c][0]>1)cpu_tint=true;
        rgba modulation=cpu_tint?rgba():rgba(byte(tint.m_[0][0]*255),byte(tint.m_[1][0]*255),byte(tint.m_[2][0]*255),byte(tint.m_[3][0]*255));
#if DNM_SMOOTH_VISUALS
        // Omit expensive additive/inverting flashes. Keep base artwork and
        // ordinary GPU tint/fades; this changes display only, never source state.
        if(cpu_tint){float alpha=tint.m_[3][1]>0 || tint.m_[3][0]<0?1.f:std::clamp(float(tint.m_[3][0]),0.f,1.f);modulation=rgba(255,255,255,byte(alpha*255));cpu_tint=false;}
#endif
        for(unsigned index=entry->first;index<entry->first+entry->count;index++) {
            const auto& t=art_tiles[index];
            point corners[]={screen({t.left,t.top},matrix),screen({t.left+t.w,t.top},matrix),screen({t.left,t.top+t.h},matrix),screen({t.left+t.w,t.top+t.h},matrix)};
            float left=1e30f,right=-1e30f,top=1e30f,bottom=-1e30f;
            for(auto p:corners){left=std::min(left,p.m_x);right=std::max(right,p.m_x);top=std::min(top,p.m_y);bottom=std::max(bottom,p.m_y);}
            if(right<58 || left>422 || bottom<-1 || top>273)continue;
            Bitmap* b;
            if(!cpu_tint) {
                if(!art_textures[index])art_textures[index]=art_texture(index);
                b=art_textures[index].get();
            } else {
                auto variant=std::find_if(variants.begin(),variants.end(),[&](const Variant& v){return v.tile==index && !std::memcmp(&v.tint,&tint,sizeof(cxform));});
                if(variant==variants.end()){variants.push_back({index,tint,art_texture(index,&tint)});variant=variants.end()-1;}
                b=variant->texture.get();
            }
            bind(b,WRAP_CLAMP);if(!b->data)std::abort();
            if(submitting_mask)sceGuEnable(GU_ALPHA_TEST);
            auto out=static_cast<Vertex*>(vertices(4*sizeof(Vertex)));
            auto c=color(modulation);
            // v4 subtiles retain one source-pixel neighbour on each edge so
            // linear filtering uses the same pixels across tile boundaries.
            float inset=(art_formats[index]&4)?1.f:0.f;
            out[0]=vertex({t.left,t.top},matrix,c,inset,inset);
            out[1]=vertex({t.left+t.w,t.top},matrix,c,inset+t.width,inset);
            out[2]=vertex({t.left,t.top+t.h},matrix,c,inset,inset+t.height);
            out[3]=vertex({t.left+t.w,t.top+t.h},matrix,c,inset+t.width,inset+t.height);
            sceGuDrawArray(GU_TRIANGLE_STRIP,VertexFormat,4,nullptr,out);
            if(submitting_mask)sceGuDisable(GU_ALPHA_TEST);
        }
        return true;
    }
    void open() override {
        pspboot::mark("renderer-enter");
        load_art();
        pspboot::mark("art-index-ready");
        // GU uses an uncached alias; retire cached CRT zeros before its first
        // write so later cache eviction cannot overwrite the live command list.
        std::memset(commands,0,sizeof(commands));
        sceKernelDcacheWritebackInvalidateRange(commands,sizeof(commands));
        pspboot::mark("command-cache-ready",int(reinterpret_cast<uintptr_t>(commands)));
        sceGuInit();
        pspboot::mark("gu-initialized");
#if DNM_SMOOTH_VISUALS
        // Swap only inside vblank, then latch at the next horizontal sync so
        // computing the following frame can begin without touching scanout.
        guSwapBuffersBehaviour(PSP_DISPLAY_SETBUF_NEXTHSYNC);
#endif
        sceGuStart(GU_DIRECT,commands);
        sceGuDrawBuffer(GU_PSM_8888,nullptr,Stride);
        sceGuDispBuffer(Width,Height,reinterpret_cast<void*>(BufferBytes),Stride);
        sceGuOffset(2048-Width/2,2048-Height/2);sceGuViewport(2048,2048,Width,Height);
        sceGuScissor(0,0,Width,Height);sceGuEnable(GU_SCISSOR_TEST);
        sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);
        sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
        sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexScale(1,1);sceGuTexOffset(0,0);
        sceGuAlphaFunc(GU_GREATER,0,0xff);sceGuEnable(GU_ALPHA_TEST);
        sceGuFinish();{perf::Scope timer(perf::SYNC);sceGuSync(0,0);}{perf::Scope timer(perf::VBLANK);sceDisplayWaitVblankStart();}sceGuDisplay(GU_TRUE);
    }
    bitmap_info* create_bitmap_info_empty() override { return new Bitmap; }
    bitmap_info* create_bitmap_info_alpha(int w,int h,unsigned char* p) override { return new Bitmap(w,h,p,1,w); }
    bitmap_info* create_bitmap_info_rgb(image::rgb* p) override { return new Bitmap(p->m_width,p->m_height,p->m_data,3,p->m_pitch); }
    bitmap_info* create_bitmap_info_rgba(image::rgba* p) override { return new Bitmap(p->m_width,p->m_height,p->m_data,4,p->m_pitch); }
    video_handler* create_video_handler() override { return nullptr; }
    void begin_display(rgba bg,int x,int y,int w,int h,float x0,float x1,float y0,float y1) override {
        sx=w/(x1-x0);sy=h/(y1-y0);ox=x-x0*sx;oy=y-y0*sy;
        movie_width=x1-x0;movie_height=y1-y0;mask_level=0;
        sceGuStart(GU_DIRECT,commands);drawing=true;active_list=true;list_bytes=0;
        frame_draws=largest_allocation=total_bytes=0;
        cache_hits=cache_misses=cache_decoded=0;
        sceGuScissor(0,0,Width,Height);sceGuPixelMask(0);sceGuDisable(GU_STENCIL_TEST);
        sceGuClearColor(0);sceGuClearStencil(0);
        sceGuClear(GU_COLOR_BUFFER_BIT|GU_STENCIL_BUFFER_BIT|GU_FAST_CLEAR_BIT);
        sceGuPixelMask(0xff000000);sceGuScissor(x,y,w,h);
        sceGuDisable(GU_TEXTURE_2D);
        // Keep each 8888 full-height fill within a32-pixel GE cache stripe.
        unsigned stripes=(w+31)/32;
        auto background=static_cast<Vertex*>(vertices(stripes*2*sizeof(Vertex)));
        for(unsigned i=0;i<stripes;++i){
            background[2*i]={0,0,color(bg),float(x+i*32),float(y),0};
            background[2*i+1]={0,0,color(bg),float(x+std::min(w,int((i+1)*32))),float(y+h),0};
        }
        sceGuDrawArray(GU_SPRITES,VertexFormat,stripes*2,nullptr,background);
    }
    void end_display() override {
        static unsigned frame=0;
        if(frame<8 || frame%60==0 || largest_allocation>600*1024) {
            dnm_graphics_trace(frame,frame_draws,largest_allocation,total_bytes);
            dnm_cache_trace(frame,cache_hits,cache_misses,cache_decoded,Bitmap::resident_bytes);
        }
        ++frame;
    }
    void set_matrix(const matrix& m) override {
        transform=m;matrix inverse;inverse.set_inverse(m);
        local_view.m_x_min=local_view.m_y_min=1e30f;
        local_view.m_x_max=local_view.m_y_max=-1e30f;
        for(auto p:{point(0,0),point(movie_width,0),point(0,movie_height),point(movie_width,movie_height)}) {
            point q;inverse.transform(&q,p);
            local_view.m_x_min=std::min(local_view.m_x_min,q.m_x);
            local_view.m_y_min=std::min(local_view.m_y_min,q.m_y);
            local_view.m_x_max=std::max(local_view.m_x_max,q.m_x);
            local_view.m_y_max=std::max(local_view.m_y_max,q.m_y);
        }
    }
    void set_cxform(const cxform& c) override { tint=c; }
    void set_antialiased(bool) override {}
    void fill_style_disable(int side) override { fills[side].enabled=false; }
    void fill_style_color(int side,const rgba& c) override { fills[side].enabled=true;fills[side].bitmap=nullptr;fills[side].color=tint.transform(c); }
    void fill_style_bitmap(int side,bitmap_info* b,const matrix& m,bitmap_wrap_mode wrap,bitmap_blend_mode) override {
        fills[side].enabled=true;fills[side].bitmap=static_cast<Bitmap*>(b);fills[side].uv=m;fills[side].wrap=wrap;
        fills[side].color=rgba(byte(tint.m_[0][0]*255),byte(tint.m_[1][0]*255),byte(tint.m_[2][0]*255),byte(tint.m_[3][0]*255));
    }
    void line_style_disable() override { line.enabled=false; }
    void line_style_color(rgba c) override { line.enabled=true;line.color=tint.transform(c); }
    void line_style_width(float w) override { line_width=w; }
    point screen(point p,const matrix& m) {
        point result; m.transform(&result,p);return {ox+result.m_x*sx,oy+result.m_y*sy};
    }
    Vertex vertex(point p,const matrix& m,uint32_t c,float u=0,float v=0) {
        p=screen(p,m);return {u,v,c,p.m_x,p.m_y,0};
    }
    void bind(Bitmap* b,bitmap_wrap_mode wrap) {
        if(b)b->activate();
        if(b && b->data) {
            sceGuEnable(GU_TEXTURE_2D);
            sceGuTexMode(b->paletted?(b->four_bit?GU_PSM_T4:GU_PSM_T8):GU_PSM_8888,0,0,b->swizzled?1:0);
            if(b->paletted){sceGuClutMode(GU_PSM_8888,0,b->four_bit?15:255,0);sceGuClutLoad(b->four_bit?2:32,reinterpret_cast<uint8_t*>(b->data)+b->palette_offset());}
            sceGuTexImage(0,b->tw,b->th,b->tw,b->data);
            sceGuTexWrap(wrap==WRAP_REPEAT?GU_REPEAT:GU_CLAMP,wrap==WRAP_REPEAT?GU_REPEAT:GU_CLAMP);
        } else sceGuDisable(GU_TEXTURE_2D);
    }
    bool visible_coords(const void* coords,int count,float margin=1) {
        auto in=static_cast<const coord_component*>(coords);
        float left=1e30f,right=-1e30f,top=1e30f,bottom=-1e30f;
        for(int i=0;i<count;i++) {
            point p=screen(point(in[i*2],in[i*2+1]),transform);
            left=std::min(left,p.m_x);right=std::max(right,p.m_x);
            top=std::min(top,p.m_y);bottom=std::max(bottom,p.m_y);
        }
        return right>=59-margin && left<=421+margin && bottom>=-margin && top<=272+margin;
    }
    void mesh(const void* coords,int count,int primitive) {
        auto& style=fills[0];if(!style.enabled || count<=0) return;
        auto in=static_cast<const coord_component*>(coords);
        Vertex batch[768];unsigned used=0;bool bound=false;
        auto flush=[&] {
            if(!used)return;if(!bound){bind(style.bitmap,style.wrap);bound=true;}
            auto out=static_cast<Vertex*>(vertices(used*sizeof(Vertex)));std::memcpy(out,batch,used*sizeof(Vertex));
            sceGuDrawArray(GU_TRIANGLES,VertexFormat,used,nullptr,out);used=0;
        };
        const int step=primitive==GU_TRIANGLES?3:1;
        for(int i=2;i<count;i+=step) {
            int ids[]={i-2,i-1,i};
            const float ax=in[(i-2)*2],ay=in[(i-2)*2+1],bx=in[(i-1)*2],by=in[(i-1)*2+1],cx=in[i*2],cy=in[i*2+1];
            if((ax==bx && ay==by)||(ax==cx && ay==cy)||(bx==cx && by==cy))continue;
            if(std::max({ax,bx,cx})<local_view.m_x_min || std::min({ax,bx,cx})>local_view.m_x_max ||
               std::max({ay,by,cy})<local_view.m_y_min || std::min({ay,by,cy})>local_view.m_y_max)continue;
            for(int id:ids) {
                point p(in[id*2],in[id*2+1]),uv;
                if(style.bitmap)style.uv.transform(&uv,p);
                float u=style.bitmap?uv.m_x*style.bitmap->rw/style.bitmap->width:0;
                float v=style.bitmap?uv.m_y*style.bitmap->rh/style.bitmap->height:0;
                batch[used++]=vertex(p,transform,color(style.color),u,v);
            }
            if(used==768)flush();
        }
        flush();
    }
    void draw_mesh_strip(const void* p,int n) override { mesh(p,n,GU_TRIANGLE_STRIP); }
    void draw_triangle_list(const void* p,int n) override { mesh(p,n,GU_TRIANGLES); }
    void draw_line_strip(const void* p,int count) override {
        if(!line.enabled || count<2) return;
        auto in=static_cast<const coord_component*>(p);
        float width=std::max(1.f,line_width*(std::abs(transform.get_x_scale()*sx)+std::abs(transform.get_y_scale()*sy))/2);
        Vertex batch[768];unsigned used=0;bool bound=false;
        auto flush=[&] {
            if(!used)return;if(!bound){bind(nullptr,WRAP_CLAMP);bound=true;}
            auto out=static_cast<Vertex*>(vertices(used*sizeof(Vertex)));std::memcpy(out,batch,used*sizeof(Vertex));
            sceGuDrawArray(GU_TRIANGLES,VertexFormat,used,nullptr,out);used=0;
        };
        auto v=[&](float x,float y)->Vertex { return {0,0,color(line.color),x,y,0}; };
        for(int i=0;i<count-1;i++) {
            float ax=in[i*2],ay=in[i*2+1],bx=in[i*2+2],by=in[i*2+3],margin=line_width*2+40;
            if(std::max(ax,bx)<local_view.m_x_min-margin || std::min(ax,bx)>local_view.m_x_max+margin ||
               std::max(ay,by)<local_view.m_y_min-margin || std::min(ay,by)>local_view.m_y_max+margin)continue;
            point a=screen(point(in[i*2],in[i*2+1]),transform),b=screen(point(in[i*2+2],in[i*2+3]),transform);
            float dx=b.m_x-a.m_x,dy=b.m_y-a.m_y,len=std::hypot(dx,dy);
            float nx=len>0?dy/len*width/2:width/2,ny=len>0?-dx/len*width/2:0;
            Vertex q[4]={v(a.m_x+nx,a.m_y+ny),v(b.m_x+nx,b.m_y+ny),v(b.m_x-nx,b.m_y-ny),v(a.m_x-nx,a.m_y-ny)};
            static constexpr int indices[]={0,1,2,0,2,3};
            for(int j=0;j<6;j++)batch[used++]=q[indices[j]];
            if(used==768)flush();
        }
        flush();
    }
    void draw_bitmap(const matrix& m,bitmap_info* raw,const rect& r,const rect& uv,rgba c) override {
        point corners[]={screen({r.m_x_min,r.m_y_min},m),screen({r.m_x_max,r.m_y_min},m),screen({r.m_x_min,r.m_y_max},m),screen({r.m_x_max,r.m_y_max},m)};
        float left=1e30f,right=-1e30f,top=1e30f,bottom=-1e30f;
        for(auto p:corners){left=std::min(left,p.m_x);right=std::max(right,p.m_x);top=std::min(top,p.m_y);bottom=std::max(bottom,p.m_y);}
        if(right<59 || left>421 || bottom<0 || top>272)return;
        auto b=static_cast<Bitmap*>(raw);if(!b)return;b->activate();if(!b->data)return;
        bind(b,WRAP_CLAMP);auto out=static_cast<Vertex*>(vertices(4*sizeof(Vertex)));
        out[0]=vertex({r.m_x_min,r.m_y_min},m,color(c),uv.m_x_min*b->rw,uv.m_y_min*b->rh);
        out[1]=vertex({r.m_x_max,r.m_y_min},m,color(c),uv.m_x_max*b->rw,uv.m_y_min*b->rh);
        out[2]=vertex({r.m_x_min,r.m_y_max},m,color(c),uv.m_x_min*b->rw,uv.m_y_max*b->rh);
        out[3]=vertex({r.m_x_max,r.m_y_max},m,color(c),uv.m_x_max*b->rw,uv.m_y_max*b->rh);
        sceGuDrawArray(GU_TRIANGLE_STRIP,VertexFormat,4,nullptr,out);
    }
    void begin_submit_mask() override {
        submitting_mask=true;
        if(mask_level==0) { sceGuEnable(GU_STENCIL_TEST);sceGuClearStencil(0);sceGuClear(GU_STENCIL_BUFFER_BIT|GU_FAST_CLEAR_BIT); }
        sceGuPixelMask(0x00ffffff);sceGuStencilFunc(GU_EQUAL,mask_level++,0xff);
        sceGuDisable(GU_ALPHA_TEST);
        sceGuStencilOp(GU_KEEP,GU_KEEP,GU_INCR);
    }
    void end_submit_mask() override {
        submitting_mask=false;
        sceGuPixelMask(0xff000000);sceGuStencilFunc(GU_EQUAL,mask_level,0xff);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);
        sceGuEnable(GU_ALPHA_TEST);
    }
    void disable_mask() override {
        if(mask_level<=0)return;
        if(--mask_level==0) { sceGuDisable(GU_STENCIL_TEST);return; }
        sceGuPixelMask(0x00ffffff);sceGuStencilFunc(GU_EQUAL,mask_level+1,0xff);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_DECR);
        bind(nullptr,WRAP_CLAMP);constexpr unsigned stripes=(Width+31)/32;
        auto out=static_cast<Vertex*>(vertices(stripes*2*sizeof(Vertex)));
        for(unsigned i=0;i<stripes;++i){out[2*i]={0,0,0xffffffff,float(i*32),0,0};out[2*i+1]={0,0,0xffffffff,float(std::min(Width,int((i+1)*32))),Height,0};}
        sceGuDrawArray(GU_SPRITES,VertexFormat,stripes*2,nullptr,out);end_submit_mask();
    }
    bool test_stencil_buffer(const rect&,Uint8) override { return false; }
    bool is_visible(const rect& b) override {
        return b.m_x_max>=0 && b.m_x_min<=movie_width && b.m_y_max>=0 && b.m_y_min<=movie_height;
    }
};
}
gameswf::render_handler* make_psp_renderer() { return new Renderer; }
bool release_psp_texture() {return Bitmap::evict_oldest();}
void prepare_psp_tick(unsigned tick) {
    // Intro-only textures must not compete with the initial actor pool.
    if(tick==2)while(Bitmap::evict_oldest()){}
    // Newlib grows its malloc arena lazily. fordblks excludes the unused PSP
    // heap beyond the current break, so a small texture working set can report
    // <2 MiB forever. Evicting until that value rises empties it every tick.
    // The configured LRU limit and actual allocation-failure recovery bound memory;
    // allocation callers already evict and retry when more space is needed.

}
namespace {
gameswf::glyph_provider* port_font=nullptr;
Renderer* port_renderer=nullptr;
void draw_native_label(const char* message,float start_x,float baseline,int size){
    auto provider=port_font;auto renderer=port_renderer;
    float x=start_x;
    if(provider&&renderer)for(const unsigned char* p=reinterpret_cast<const unsigned char*>(message);*p;p++){
        if(*p=='\n'){x=start_x;baseline+=20;continue;}if(*p=='\r')continue;
        gameswf::rect origin;float advance=0;auto glyph=provider->get_char_image(nullptr,*p,"PSP",false,false,size,&origin,&advance);
        if(glyph&&*p!=' '){
            auto bitmap=static_cast<Bitmap*>(glyph);renderer->bind(bitmap,gameswf::render_handler::WRAP_CLAMP);
            float left=x-origin.m_x_min*bitmap->width,top=baseline-origin.m_y_min*bitmap->height;
            Vertex quad[]={{0,0,0xffffffff,left,top,0},{float(bitmap->rw),0,0xffffffff,left+bitmap->width,top,0},{0,float(bitmap->rh),0xffffffff,left,top+bitmap->height,0},{float(bitmap->rw),float(bitmap->rh),0xffffffff,left+bitmap->width,top+bitmap->height,0}};
            auto output=static_cast<Vertex*>(vertices(sizeof(quad)));std::memcpy(output,quad,sizeof(quad));sceGuDrawArray(GU_TRIANGLE_STRIP,VertexFormat,4,nullptr,output);
        }
        x+=advance*size/1000.f;
    }
}
}
void set_psp_port_font(gameswf::glyph_provider* font,gameswf::render_handler* renderer){port_font=font;port_renderer=static_cast<Renderer*>(renderer);}
bool prepare_psp_frame_resources(const psp60::Frame& frame,unsigned& command,unsigned& tile,unsigned long long deadline){
    auto r=port_renderer;if(!r||!baked_art_ready)return true;
    const float sx=frame.w/(frame.x1-frame.x0),sy=frame.h/(frame.y1-frame.y0);
    const float ox=frame.x-frame.x0*sx,oy=frame.y-frame.y0*sy;
    // Called after submit_psp_frame has completed the current GE list. Only CPU
    // cache contents change; no draw state, VM object or queue slot is touched.
    for(;command<frame.commands.size();++command,tile=0){
        if(sceKernelGetSystemTimeWide()>=deadline)return false;
        const auto& p=frame.commands[command];
        if(p.op==psp60::Op::Bitmap||p.op==psp60::Op::FillBitmap){
            auto b=static_cast<Bitmap*>(p.bitmap);if(b&&!b->data)b->activate();
            continue;
        }
        if(p.op!=psp60::Op::Shape)continue;
#if !DNM_SMOOTH_VISUALS
        bool cpu_tint=false;for(int c=0;c<4;++c)if(p.cx.m_[c][1]!=0||p.cx.m_[c][0]<0||p.cx.m_[c][0]>1)cpu_tint=true;
        if(cpu_tint)continue;
#endif
        auto entry=std::lower_bound(r->art_entries.begin(),r->art_entries.end(),std::make_pair(unsigned(p.a),Uint16(p.b)),
            [](const Renderer::ArtEntry& e,auto key){return e.id<key.first||(e.id==key.first&&e.ratio<key.second);});
        if(entry==r->art_entries.end()||entry->id!=unsigned(p.a)||entry->ratio!=Uint16(p.b))continue;
        for(;tile<entry->count;++tile){
            if(sceKernelGetSystemTimeWide()>=deadline)return false;
            unsigned index=entry->first+tile;const auto& t=r->art_tiles[index];
            // Preparation only activates missing CPU data. Resident tiles need
            // neither visibility tests nor an LRU touch, exactly as below.
            // Avoid transforming eight corners per hot tile on every scene.
            if(r->art_textures[index]&&r->art_textures[index]->data)continue;
            bool visible=false;
            // Cover the exact and interpolated positions without altering the
            // renderer's current viewport or matrix.
            for(float alpha:{.5f,1.f}){
                auto m=psp60::Frame::blend(p,alpha);
                gameswf::point corners[]={{t.left,t.top},{t.left+t.w,t.top},{t.left,t.top+t.h},{t.left+t.w,t.top+t.h}};
                float left=1e30f,right=-1e30f,top=1e30f,bottom=-1e30f;
                for(auto q:corners){gameswf::point v;m.transform(&v,q);float x=ox+v.m_x*sx,y=oy+v.m_y*sy;left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
                if(right>=58&&left<=422&&bottom>=-1&&top<=273)visible=true;
            }
            if(!visible)continue;
            if(!r->art_textures[index])r->art_textures[index]=r->art_texture(index);
            auto b=r->art_textures[index].get();if(!b->data)b->activate();
        }
    }
    return true;
}
void submit_psp_frame(const char* overlay,bool footer) {
    sceGuFinish();{perf::Scope timer(perf::SYNC);sceGuSync(0,0);}active_list=false;
    if(overlay) {
        pspDebugScreenSetOffset(reinterpret_cast<unsigned int>(draw_buffer));
        pspDebugScreenSetTextColor(0xffffffff);pspDebugScreenSetBackColor(0xff181818);
        pspDebugScreenSetXY(footer?20:3,footer?31:10);pspDebugScreenPrintf("%s",overlay);
    }
}
namespace {unsigned long long last_present_time=0;unsigned last_present_vcount=0;}
unsigned long long psp_present_time(){return last_present_time;}
unsigned psp_present_vcount(){return last_present_vcount;}
static void latch_psp_frame(bool swap) {
    // Only the short vblank latch outranks audio. Mixing runs normally while
    // this thread sleeps; it cannot delay the completed buffer past vblank.
    int priority=sceKernelGetThreadCurrentPriority();sceKernelChangeThreadPriority(0,0x10);
    {perf::Scope timer(perf::VBLANK);
#if DNM_SMOOTH_VISUALS
    present_pacer.wait();
#else
    sceDisplayWaitVblankStart();
#endif
    }
    if(swap){displayed_buffer=draw_buffer;draw_buffer=sceGuSwapBuffers();}
    last_present_time=sceKernelGetSystemTimeWide();last_present_vcount=sceDisplayGetVcount();
    sceKernelChangeThreadPriority(0,priority);
}
void present_psp_frame(){latch_psp_frame(true);}
void hold_psp_frame(){latch_psp_frame(false);}


void submit_psp_loading(){
    sceGuScissor(0,0,Width,Height);sceGuPixelMask(0);sceGuDisable(GU_STENCIL_TEST);sceGuEnable(GU_ALPHA_TEST);sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    Vertex box[]={{0,0,0xe0202020,170,113,0},{0,0,0xe0202020,310,113,0},{0,0,0xe0202020,170,159,0},{0,0,0xe0202020,310,159,0}};
    auto out=static_cast<Vertex*>(vertices(sizeof(box)));std::memcpy(out,box,sizeof(box));sceGuDrawArray(GU_TRIANGLE_STRIP,VertexFormat,4,nullptr,out);
    draw_native_label("LOADING...",197,142,14);submit_psp_frame();
}
bool confirm_psp_frame_visible(){
    // SwapBuffers schedules NEXTFRAME. Wait for that latch, then inspect the
    // CURRENT framebuffer; acknowledging immediately after the swap is early.
    sceDisplayWaitVblankStart();void* shown=nullptr;int stride=0,format=0;
    if(sceDisplayGetFrameBuf(&shown,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)<0)return false;
    auto expected=reinterpret_cast<std::uintptr_t>(sceGeEdramGetAddr())+reinterpret_cast<std::uintptr_t>(displayed_buffer);
    return (reinterpret_cast<std::uintptr_t>(shown)&0x0fffffffu)==(expected&0x0fffffffu)&&stride==Stride;
}

void finish_psp_frame(const char* overlay,bool footer) {submit_psp_frame(overlay,footer);present_psp_frame();}

bool dump_psp_frame(const char* path) {
    // Isolated graphical fixtures request a real GE transfer from VRAM to RAM.
    // This is never enabled in ordinary gameplay or speed measurements.
    std::uint32_t* pixels;
    do {pixels=static_cast<std::uint32_t*>(memalign(16,BufferBytes));}while(!pixels && Bitmap::evict_oldest());
    if(!pixels)return false;
    sceKernelDcacheWritebackInvalidateRange(pixels,BufferBytes);
    auto source=static_cast<unsigned char*>(sceGeEdramGetAddr())+reinterpret_cast<std::uintptr_t>(displayed_buffer);
    sceGuStart(GU_DIRECT,commands);
    sceGuCopyImage(GU_PSM_8888,0,0,Width,Height,Stride,source,0,0,Stride,pixels);sceGuTexSync();
    sceGuFinish();{perf::Scope timer(perf::SYNC);sceGuSync(0,0);}sceKernelDcacheInvalidateRange(pixels,BufferBytes);
    FILE* file=std::fopen(path,"wb");bool ok=file!=nullptr;
    if(file){for(int y=0;y<Height;++y)if(std::fwrite(pixels+y*Stride,4,Width,file)!=Width)ok=false;if(std::fclose(file))ok=false;}
    std::free(pixels);return ok;
}
