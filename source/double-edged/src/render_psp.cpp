#include <pspiofilemgr.h>
#include "bank_io.hpp"
#include "bank_read_cache.hpp"
#include "boot_trace.hpp"
#include "../vendor/lz4/lz4.h"
#include "raster_math.hpp"
#include "render_psp.hpp"
#include "button_hit_shapes.hpp"
#include "filter_pixels.hpp"
#include "filter_tiles.hpp"
#include "filter_prefix_cache.hpp"
#include "gameswf/gameswf_character.h"
#include "gameswf/gameswf_types.h"
#include "gameswf/gameswf_render.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <memory>
#include <new>
#include <cstdio>
#include <malloc.h>
#include <zlib.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include "texture_arena.hpp"
#include "original_loading_art.hpp"
#include "art_shape_index.hpp"
#if DE_COMPACT_TEXTURE_ARENA
#include "compact_texture_arena.hpp"
#endif
#if DE_PREPARE_TEXTURES
#include "recorded_renderer.hpp"
#include <array>
#endif
volatile unsigned g_double_edged_picture_mode=0;
extern "C" void glorton_graphics_trace(unsigned,unsigned,unsigned,unsigned);
extern "C" void glorton_cache_trace(unsigned,unsigned,unsigned,unsigned,unsigned);
extern "C" void glorton_gpu_trace(unsigned,unsigned,unsigned,unsigned);
extern "C" void glorton_filter_trace(unsigned,unsigned,unsigned,unsigned);
extern "C" void glorton_filter_detail(unsigned,const char*,int,int,unsigned);
extern "C" void glorton_prefix_trace(unsigned,unsigned);
extern "C" void glorton_pool_trace(unsigned,unsigned,unsigned,unsigned,unsigned);
extern "C" void de_art_bank_trace(unsigned,unsigned,unsigned);
extern "C" void de_texture_arena_trace(unsigned,unsigned,unsigned,unsigned);
extern "C" void glorton_graphics_failure(const char*);
extern "C" void glorton_codec_recovery(const char*,int);

namespace {
using namespace gameswf;
constexpr int Stride=512, Width=480, Height=272;
constexpr int BufferBytes=Stride*Height*4;
#ifndef GLORTON_TEXTURE_CACHE_MB
#define GLORTON_TEXTURE_CACHE_MB 12
#endif
constexpr unsigned TextureCacheBytes=GLORTON_TEXTURE_CACHE_MB*1024*1024;
#ifndef DE_RESIDENT_BANK_MB
#define DE_RESIDENT_BANK_MB 3
#endif
constexpr unsigned ResidentBankBytes=DE_RESIDENT_BANK_MB*1024*1024;
#if DE_COMPACT_TEXTURE_ARENA
doubleedged::CompactTextureArena<TextureCacheBytes> texture_arena;
#else
doubleedged::TextureArena<TextureCacheBytes> texture_arena;
#endif
constexpr unsigned TintVariantLimit=512;
#ifdef DE_CACHE_DIAGNOSTICS
unsigned budget_evictions=0,pressure_evictions=0,tint_hits=0,tint_misses=0;
#endif
alignas(64) uint32_t commands[256*1024];
void* draw_buffer=nullptr;
void* displayed_buffer=nullptr;
bool active_list=false;
float pointer_x=0,pointer_y=0;bool pointer_visible=false;
bool baked_art_ready=false;
std::atomic<bool> fast_color_mode{false};
unsigned picture_notice_frames=0;
#if GLORTON_WLAN
int link_role=-1;
#endif
[[noreturn]] void graphics_failure(const char* reason);
// One bank read/decode is active at a time. Reserving its compressed input
// once keeps these short-lived allocations from splitting decoded-texture RAM.
std::vector<uint8_t> bank_scratch;
BankReadCache<> art_bank_cache;
bool read_art_bank(SceUID fd,unsigned offset,unsigned count,unsigned char* output){
    auto read=[&](unsigned at,unsigned n,unsigned char* out){return sceIoLseek32(fd,at,PSP_SEEK_SET)>=0&&sceIoRead(fd,out,n)==int(n);};
    return art_bank_cache.bytes()?art_bank_cache.read(offset,count,output,read):read(offset,count,output);
}
bool use_bank_scratch=true;
bool use_persistent_codecs=true;
struct TextureCodecs {
    z_stream encoder{},decoder{};
    void open() {
        if(deflateInit(&encoder,1)!=Z_OK || inflateInit(&decoder)!=Z_OK)graphics_failure("Texture codec initialization");
    }
    int compress(Bytef* out,uLongf* length,const Bytef* in,uLong size) {
        int result=deflateReset(&encoder);if(result!=Z_OK)return result;
        encoder.next_in=const_cast<Bytef*>(in);encoder.avail_in=size;encoder.next_out=out;encoder.avail_out=*length;
        result=deflate(&encoder,Z_FINISH);*length=encoder.total_out;
        return result==Z_STREAM_END?Z_OK:result==Z_OK?Z_BUF_ERROR:result;
    }
    int decompress(Bytef* out,uLongf* length,const Bytef* in,uLong size) {
        int result=inflateReset(&decoder);if(result!=Z_OK)return result;
        decoder.next_in=const_cast<Bytef*>(in);decoder.avail_in=size;decoder.next_out=out;decoder.avail_out=*length;
        result=inflate(&decoder,Z_FINISH);*length=decoder.total_out;
        return result==Z_STREAM_END?Z_OK:result==Z_OK?Z_BUF_ERROR:result;
    }
} texture_codecs;
int compress_texture(Bytef*,uLongf*,const Bytef*,uLong);
int decompress_texture(Bytef*,uLongf*,const Bytef*,uLong);
int image_quality=1; // Original MEDIUM default: preserve the qualified raster path.
#ifndef DE_RASTER_DENSITY_SCALE
#define DE_RASTER_DENSITY_SCALE 1.0
#endif
constexpr float ArtDensityScale=float(DE_RASTER_DENSITY_SCALE);
static_assert(ArtDensityScale>0.f&&ArtDensityScale<=4.f);
unsigned cache_hits=0,cache_misses=0,cache_decoded=0;
unsigned list_bytes=0;
unsigned frame_draws=0,largest_allocation=0,total_bytes=0;
unsigned gpu_flushes=0,gpu_flush_us=0,retired_peak=0;
// Optional aggregate counters only: no per-frame file writes or extra queue.
struct MediaRenderStats {
    bool enabled=false,native_bank_io=false;uint64_t frame_begin=0,draw_us=0,finish_us=0,bank_read_us=0,decode_us=0;
    unsigned frames=0,max_draw_us=0,max_finish_us=0,max_bank_read_us=0,max_decode_us=0;
    unsigned misses=0,decoded_bytes=0,resident_peak=0,t4_binds=0,t8_binds=0,rgba_binds=0;
} media_stats;
struct RetiredTexture {void* data;unsigned bytes;};
RetiredTexture retired[1024];unsigned retired_count=0,retired_bytes=0;
#ifndef GLORTON_TEXTURE_POOL_MB
#define GLORTON_TEXTURE_POOL_MB 0
#endif
RetiredTexture texture_pool[64];unsigned pool_count=0,pool_bytes=0,pool_reuses=0,texture_failures=0,largest_texture_failure=0;
unsigned pool_limit_bytes=GLORTON_TEXTURE_POOL_MB*1024*1024;
[[noreturn]] void graphics_failure(const char* reason){glorton_graphics_failure(reason);std::abort();}
bool free_pool_block() {
    if(!pool_count)return false;
    unsigned largest=0;for(unsigned i=1;i<pool_count;++i)if(texture_pool[i].bytes>texture_pool[largest].bytes)largest=i;
    auto block=texture_pool[largest];texture_pool[largest]=texture_pool[--pool_count];
    pool_bytes-=block.bytes;std::free(block.data);return true;
}
void retain_texture(void* data,unsigned bytes) {
    if(texture_arena.contains(data)){
        if(!texture_arena.release(data))graphics_failure("Texture arena release");return;
    }
    // Keep large, completed GPU allocations intact so small VM/text buffers
    // cannot split them before the next decoded texture needs the same size.
    if(bytes>=64*1024 && pool_count<64 && pool_bytes+bytes<=pool_limit_bytes) {
        texture_pool[pool_count++]={data,bytes};pool_bytes+=bytes;
    } else std::free(data);
}
void* allocate_texture(unsigned bytes) {
    if(texture_arena.ready()){
        auto data=texture_arena.allocate(bytes);
        if(!data){++texture_failures;largest_texture_failure=std::max(largest_texture_failure,bytes);}
        return data;
    }
    for(unsigned i=0;i<pool_count;++i)if(texture_pool[i].bytes==bytes) {
        auto data=texture_pool[i].data;texture_pool[i]=texture_pool[--pool_count];pool_bytes-=bytes;++pool_reuses;return data;
    }
    auto data=memalign(16,bytes);
    if(!data){++texture_failures;largest_texture_failure=std::max(largest_texture_failure,bytes);}
    return data;
}
void release_retired_textures() {
    for(unsigned i=0;i<retired_count;++i)retain_texture(retired[i].data,retired[i].bytes);
    retired_count=retired_bytes=0;
}
void flush_gpu() {
    if(active_list) {
        auto began=sceKernelGetSystemTimeWide();sceGuFinish();sceGuSync(0,0);
        ++gpu_flushes;gpu_flush_us+=unsigned(sceKernelGetSystemTimeWide()-began);
    }
    release_retired_textures();
    if(active_list){sceGuStart(GU_DIRECT,commands);list_bytes=0;}
}
void retire_texture(void* data,unsigned bytes) {
#if GLORTON_DEFER_TEXTURE_FREE
    if(active_list) {
        if(retired_count==1024)flush_gpu();
        retired[retired_count++]={data,bytes};retired_bytes+=bytes;
        retired_peak=std::max(retired_peak,retired_bytes);return;
    }
#else
    flush_gpu();
#endif
    retain_texture(data,bytes);
}
void* vertices(unsigned bytes) {
    ++frame_draws;largest_allocation=std::max(largest_allocation,bytes);total_bytes+=bytes;
    if(list_bytes+bytes+512>700*1024)flush_gpu();
    list_bytes+=bytes+512;return sceGuGetMemory(bytes);
}
uint32_t color(rgba c) { return c.m_r|(uint32_t(c.m_g)<<8)|(uint32_t(c.m_b)<<16)|(uint32_t(c.m_a)<<24); }
int power2(int n) { int v=1;while(v<n) v*=2;return v; }
uint8_t byte(float f) { return uint8_t(std::clamp(f,0.f,255.f)); }

struct Bitmap final : bitmap_info {
    static unsigned next_id;
    const unsigned id=++next_id;
    int width=0,height=0,tw=0,th=0,rw=0,rh=0;
    uint32_t* data=nullptr;
    unsigned palette_entries=0,index_bits=8;
    unsigned index_bytes()const{return unsigned(tw)*th*index_bits/8;}
    unsigned storage_bytes()const{return palette_entries?index_bytes()+palette_entries*4:unsigned(tw)*th*4;}
    const uint32_t* palette()const{return reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(data)+index_bytes());}
    std::vector<uint8_t> compressed;
    FILE* bank=nullptr;
    SceUID bank_fd=-1;bool bank_lz4=false;
    uint32_t bank_offset=0,bank_size=0;const uint8_t* bank_memory=nullptr;
    cxform pixel_tint;
    bool apply_tint=false;
    unsigned used=0;
    static std::vector<Bitmap*> resident;
    static unsigned resident_bytes,clock;
    static bool evict_oldest(bool immediate=false) {
#ifdef DE_CACHE_DIAGNOSTICS
        if(immediate)++pressure_evictions;else ++budget_evictions;
#endif
        if(immediate && free_pool_block())return true;
        // Allocation-pressure callers need actual RAM now. Ordinary cache
        // eviction keeps GPU-referenced allocations alive until the frame sync.
        if(immediate && retired_count){flush_gpu();return true;}
        if(resident.empty())return false;
        auto oldest=std::min_element(resident.begin(),resident.end(),[](Bitmap* a,Bitmap* b){return a->used<b->used;});
        auto victim=*oldest;auto data=victim->data;unsigned bytes=victim->storage_bytes();
        victim->data=nullptr;resident_bytes-=bytes;resident.erase(oldest);
        if(immediate){flush_gpu();if(texture_arena.contains(data)){if(!texture_arena.release(data))graphics_failure("Texture arena eviction");}else std::free(data);}else retire_texture(data,bytes);
        return true;
    }
    Bitmap()=default;
    Bitmap(int w,int h,const uint8_t* bytes,int channels,int pitch):width(w),height(h) {
        if(baked_art_ready && channels>=3)return;
        float scale=std::min(std::max(w,h)>64?.625f:1.f,512.f/std::max(w,h));
        rw=std::max(1,int(glorton::raster_ceil(w*scale))); rh=std::max(1,int(glorton::raster_ceil(h*scale)));
        tw=power2(rw);th=power2(rh);
        do{data=static_cast<uint32_t*>(allocate_texture(tw*th*4));}while(!data&&evict_oldest(true));
        if(!data)throw std::bad_alloc();
        for(int y=0;y<th;y++) for(int x=0;x<tw;x++) {
            int sx=std::min(w-1,int(std::min(x,rw-1)/scale));
            int sy=std::min(h-1,int(std::min(y,rh-1)/scale));
            auto p=bytes+sy*pitch+sx*channels;
            data[y*tw+x]=channels==1 ? (0xffffffu|(uint32_t(p[0])<<24)) :
                uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|
                (uint32_t(channels==4?p[3]:255)<<24);
        }
        uLongf size=compressBound(tw*th*4);compressed.resize(size);
        if(compress_texture(compressed.data(),&size,reinterpret_cast<uint8_t*>(data),tw*th*4)!=Z_OK)graphics_failure("Bitmap compression");
        compressed.resize(size);compressed.shrink_to_fit();if(texture_arena.contains(data)){if(!texture_arena.release(data))graphics_failure("Texture arena scratch release");}else std::free(data);data=nullptr;
    }
    void activate() override {
        used=++clock;if(data){++cache_hits;return;}if(!tw||(compressed.empty()&&!bank))return;
        unsigned size=storage_bytes();
        ++cache_misses;cache_decoded+=size;
        while(!resident.empty() && resident_bytes+size>TextureCacheBytes) {
            evict_oldest();
        }
        std::unique_ptr<uint8_t,decltype(&std::free)> packed(nullptr,std::free);
        const uint8_t* packed_data=nullptr;
        if(bank && !bank_memory) {
            uint8_t* destination;
            if(use_bank_scratch && bank_size<=bank_scratch.size())destination=bank_scratch.data();
            else {
                do { packed.reset(static_cast<uint8_t*>(std::malloc(bank_size))); }while(!packed && evict_oldest(true));
                if(!packed)throw std::bad_alloc();
                destination=packed.get();
            }
            auto read_begin=media_stats.enabled?sceKernelGetSystemTimeWide():0;
            bool valid=bank_fd>=0?read_art_bank(bank_fd,bank_offset,bank_size,destination):
                (!std::fseek(bank,bank_offset,SEEK_SET)&&std::fread(destination,1,bank_size,bank)==bank_size);
            if(media_stats.enabled){unsigned elapsed=unsigned(sceKernelGetSystemTimeWide()-read_begin);media_stats.bank_read_us+=elapsed;media_stats.max_bank_read_us=std::max(media_stats.max_bank_read_us,elapsed);}
            if(!valid)graphics_failure("Texture bank read");
            packed_data=destination;
        }
        if(bank_memory)packed_data=bank_memory+bank_offset;
        do { data=static_cast<uint32_t*>(allocate_texture(size)); }while(!data && evict_oldest(true));
        if(!data)throw std::bad_alloc();
        uLongf bytes=size;
        auto decode_begin=media_stats.enabled?sceKernelGetSystemTimeWide():0;
        if(bank_lz4){if(LZ4_decompress_safe(reinterpret_cast<const char*>(packed_data),reinterpret_cast<char*>(data),bank_size,size)!=int(size))graphics_failure("LZ4 texture byte count");}
        else if(decompress_texture(reinterpret_cast<uint8_t*>(data),&bytes,bank?packed_data:compressed.data(),bank?bank_size:compressed.size())!=Z_OK || bytes!=size)graphics_failure("Texture decompression or byte count");
        if(media_stats.enabled){unsigned elapsed=unsigned(sceKernelGetSystemTimeWide()-decode_begin);media_stats.decode_us+=elapsed;media_stats.max_decode_us=std::max(media_stats.max_decode_us,elapsed);}
        if(apply_tint) {
            auto pixels=reinterpret_cast<uint8_t*>(data)+(palette_entries?index_bytes():0);
            unsigned tint_bytes=palette_entries?palette_entries*4:size;
            for(unsigned i=0;i<tint_bytes;i+=4) {
                if(!pixels[i+3])continue;
                for(int c=0;c<4;c++)pixels[i+c]=byte(pixels[i+c]*pixel_tint.m_[c][0]+pixel_tint.m_[c][1]);
            }
        }
        resident.push_back(this);resident_bytes+=size;sceKernelDcacheWritebackRange(data,size);
    }
    ~Bitmap() override {
        if(data) { unsigned bytes=storage_bytes();resident_bytes-=bytes;resident.erase(std::remove(resident.begin(),resident.end(),this),resident.end());retire_texture(data,bytes); }
    }
    int get_width() const override { return width; }
    int get_height() const override { return height; }
    unsigned char* get_data() const override { return reinterpret_cast<unsigned char*>(data); }
    int get_bpp() const override { return palette_entries?1:4; }
};
std::vector<Bitmap*> Bitmap::resident;
unsigned Bitmap::resident_bytes=0,Bitmap::clock=0;
unsigned Bitmap::next_id=0;
int compress_texture(Bytef* out,uLongf* length,const Bytef* in,uLong size) {
    if(use_persistent_codecs)return texture_codecs.compress(out,length,in,size);
    const uLongf capacity=*length;int result;
    do {
        *length=capacity;result=compress2(out,length,in,size,1);
        if(result==Z_MEM_ERROR)glorton_codec_recovery("compress",result);
    } while(result==Z_MEM_ERROR && Bitmap::evict_oldest(true));
    return result;
}
int decompress_texture(Bytef* out,uLongf* length,const Bytef* in,uLong size) {
    if(use_persistent_codecs)return texture_codecs.decompress(out,length,in,size);
    const uLongf capacity=*length;int result;
    do {
        *length=capacity;result=uncompress(out,length,in,size);
        if(result==Z_MEM_ERROR)glorton_codec_recovery("decompress",result);
    } while(result==Z_MEM_ERROR && Bitmap::evict_oldest(true));
    return result;
}
struct Vertex { float u,v; uint32_t color; float x,y,z; };
constexpr int VertexFormat=GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D;

struct Renderer final : gameswf::render_handler {
#if DE_PREPARE_TEXTURES
    bool preparing=false;
    SceUID prepare_fd=-1;
    std::vector<Bitmap*> prepare_requests;
    std::vector<uint8_t> prepare_packed;
    struct PrepareStats {unsigned scenes=0,decoded=0,duplicates=0,failures=0,peak_pending=0;uint64_t bytes=0,ready_skips=0;} prepare_stats;
    void consider(Bitmap* b){
        // Bank textures and recorder-retained glyphs have stable lifetimes.
        // Mutable tint variants remain on the original presentation path.
        if(!b||!b->tw||b->apply_tint||(!b->bank&&b->compressed.empty()))return;
        if(b->data){b->used=++Bitmap::clock;return;}
        if(std::find(prepare_requests.begin(),prepare_requests.end(),b)==prepare_requests.end())prepare_requests.push_back(b);
    }
    void prepare(const psp60::Frame& frame,void(*lock)(),void(*unlock)()){
        if(frame.w<=0||frame.h<=0||frame.x1==frame.x0||frame.y1==frame.y0)return;
        ++prepare_stats.scenes;prepare_requests.clear();
        lock();
        const std::array<float,4> saved{sx,sy,ox,oy};
        sx=frame.w/(frame.x1-frame.x0);sy=frame.h/(frame.y1-frame.y0);ox=frame.x-frame.x0*sx;oy=frame.y-frame.y0*sy;preparing=true;
        try{
            // Use the exact playback interpolation and raster selection.
            // No GU lists, source ticks, input events or queue slots are added.
            for(float alpha:{.5f,1.f})for(const auto& p:frame.commands){
                if(p.op==psp60::Op::Shape)draw_shape(p.a,psp60::Frame::blend(p,alpha),p.cx,Uint16(p.b));
                else if(p.op==psp60::Op::Bitmap)draw_bitmap(psp60::Frame::blend(p,alpha),p.bitmap,p.bounds,p.uv,p.color);
                else if(p.op==psp60::Op::FillBitmap)consider(static_cast<Bitmap*>(p.bitmap));
            }
        }catch(const std::bad_alloc&){++prepare_stats.failures;}
        preparing=false;sx=saved[0];sy=saved[1];ox=saved[2];oy=saved[3];unlock();
        for(auto* b:prepare_requests){
            lock();
            if(b->data){b->used=++Bitmap::clock;unlock();continue;}
            unsigned size=b->storage_bytes();uint32_t* pending=nullptr;
            do{pending=static_cast<uint32_t*>(allocate_texture(size));}while(!pending&&Bitmap::evict_oldest(true));
            unlock();
            if(!pending){++prepare_stats.failures;continue;}
            prepare_stats.peak_pending=std::max(prepare_stats.peak_pending,size);
            bool valid=false;
            try{
                const uint8_t* packed=nullptr;unsigned packed_size=0;
                if(b->bank){
                    packed_size=b->bank_size;
                    if(b->bank_memory)packed=b->bank_memory+b->bank_offset;
                    else if(prepare_fd>=0){
                        prepare_packed.resize(packed_size);
                        if(sceIoLseek32(prepare_fd,b->bank_offset,PSP_SEEK_SET)>=0&&sceIoRead(prepare_fd,prepare_packed.data(),packed_size)==int(packed_size))packed=prepare_packed.data();
                    }
                }else{packed=b->compressed.data();packed_size=b->compressed.size();}
                if(packed){
                    if(b->bank_lz4)valid=LZ4_decompress_safe(reinterpret_cast<const char*>(packed),reinterpret_cast<char*>(pending),packed_size,size)==int(size);
                    else{uLongf bytes=size;valid=uncompress(reinterpret_cast<Bytef*>(pending),&bytes,packed,packed_size)==Z_OK&&bytes==size;}
                }
            }catch(const std::bad_alloc&){}
            // Pending memory is reserved in the existing texture arena, hidden
            // from the resident/LRU list until complete. Decode and file I/O
            // run without the GPU mutex; presentation can continue meanwhile.
            if(valid)sceKernelDcacheWritebackRange(pending,size);
            lock();
            if(valid&&!b->data){
                try{Bitmap::resident.push_back(b);b->data=pending;b->used=++Bitmap::clock;Bitmap::resident_bytes+=size;++prepare_stats.decoded;prepare_stats.bytes+=size;pending=nullptr;}
                catch(const std::bad_alloc&){++prepare_stats.failures;}
            }else if(valid)++prepare_stats.duplicates;else ++prepare_stats.failures;
            if(pending){
                if(texture_arena.contains(pending))texture_arena.release(pending);else std::free(pending);
            }
            unlock();
        }
    }
    void write_prepare_stats()const{
        if(FILE* f=std::fopen("PREPARE.JSON","w")){
            std::fprintf(f,"{\"scenes\":%u,\"decoded_resources\":%u,\"duplicate_inflight\":%u,\"fallbacks\":%u,\"decoded_bytes\":%llu,\"ready_skips\":%llu,\"peak_pending_bytes\":%u,\"packed_scratch_bytes\":%u,\"queue_slots\":%u}\n",prepare_stats.scenes,prepare_stats.decoded,prepare_stats.duplicates,prepare_stats.failures,(unsigned long long)prepare_stats.bytes,(unsigned long long)prepare_stats.ready_skips,prepare_stats.peak_pending,unsigned(prepare_packed.capacity()),unsigned(DE_SCENE_SLOTS));std::fclose(f);
        }
    }
#endif
    struct ArtEntryV2 { uint32_t id;uint16_t ratio,lod;uint32_t first,count; };
    struct ArtTileV2 { uint32_t offset,size;uint16_t tw,th,width,height;float left,top,w,h; };
    struct ArtEntry { uint32_t id;uint16_t ratio,lod;uint32_t first,count;float density; };
    struct ArtTile { uint32_t offset,size;uint16_t tw,th,width,height;float left,top,right,bottom;uint16_t u0,v0,u1,v1; };
    struct ArtTileV6 {ArtTile tile;uint16_t format,palette_entries;};
    struct ArtFormat {uint16_t format=0,palette_entries=0;};
    static_assert(sizeof(ArtEntryV2)==16 && sizeof(ArtTileV2)==32 && sizeof(ArtEntry)==20 && sizeof(ArtTile)==40);
    static_assert(sizeof(ArtTileV6)==44&&sizeof(ArtFormat)==4);
    FILE* art_file=nullptr;
    SceUID art_fd=-1;bool art_lz4=false;
    bool art_density_derivative=false;
    std::vector<ArtEntry> art_entries;
    doubleedged::ArtShapeIndex art_shape_index;
    auto find_art(int id,Uint16 ratio)const->std::vector<ArtEntry>::const_iterator{
        return art_shape_index.find(art_entries,unsigned(id),ratio);
    }
    std::vector<uint8_t> compressed_art;
    std::vector<ArtTile> art_tiles;
    std::vector<ArtFormat> art_formats;
    std::vector<std::unique_ptr<Bitmap>> art_textures;
    struct Variant { unsigned tile;cxform tint;std::unique_ptr<Bitmap> texture;unsigned used; };
    std::vector<Variant> variants;
    struct FilterOp {unsigned kind=0;Bitmap* texture=nullptr;bitmap_wrap_mode wrap=WRAP_CLAMP;Vertex v[3]{};};
    struct FilterLayer {float x=0,y=0;std::vector<FilterOp> ops;};
    FilterLayer* capture=nullptr;
    struct FilterTile {int x,y,w,h;std::unique_ptr<Bitmap> bitmap;};
    struct FilterCache {std::uint64_t key;unsigned used;std::vector<FilterTile> tiles;};
    std::vector<FilterCache> filter_cache;
    doubleedged::FilterPrefixCache filter_prefix;
    struct FilterTint {unsigned id;cxform tint;unsigned used;std::unique_ptr<Bitmap> bitmap;};
    std::vector<FilterTint> filter_tints;
    unsigned filter_clock=0,filter_hits=0,filter_misses=0;
    unsigned display_frame=0;
    void record_triangle(Bitmap* texture,bitmap_wrap_mode wrap,const Vertex* p) {
        FilterOp op;op.texture=texture;op.wrap=wrap;
        for(int i=0;i<3;++i){op.v[i]=p[i];op.v[i].x-=capture->x;op.v[i].y-=capture->y;}
        capture->ops.push_back(op);
    }
    void submit_quad(Bitmap* texture,bitmap_wrap_mode wrap,const Vertex* p) {
        if(capture) {
            Vertex a[]={p[0],p[1],p[2]},b[]={p[2],p[1],p[3]};
            record_triangle(texture,wrap,a);record_triangle(texture,wrap,b);return;
        }
        bind(texture,wrap);if(!texture->data)graphics_failure("Bound texture lost allocation");
        if(submitting_mask)sceGuEnable(GU_ALPHA_TEST);
        auto out=static_cast<Vertex*>(vertices(4*sizeof(Vertex)));std::memcpy(out,p,4*sizeof(Vertex));
        sceGuDrawArray(GU_TRIANGLE_STRIP,VertexFormat,4,nullptr,out);
        if(submitting_mask)sceGuDisable(GU_ALPHA_TEST);
    }
    std::unique_ptr<Bitmap> filtered_bitmap(int w,int h,const glorton::FilterSurface& surface,int x,int y) {
        auto pixels=doubleedged::pack_filter_tile(surface.pixels.data(),surface.width,surface.height,x,y,w,h);
        auto b=std::make_unique<Bitmap>();b->width=b->rw=w+2;b->height=b->rh=h+2;b->tw=pixels.width;b->th=pixels.height;
        uLongf size=compressBound(pixels.pixels.size()*4);b->compressed.resize(size);
        if(compress_texture(b->compressed.data(),&size,reinterpret_cast<const Bytef*>(pixels.pixels.data()),pixels.pixels.size()*4)!=Z_OK)graphics_failure("Filter surface compression");
        b->compressed.resize(size);b->compressed.shrink_to_fit();return b;
    }
    bool draw_filtered(character* ch) override {
        for(const auto& filter:*ch->m_filters)if(!filter.supported)return false;
        FilterLayer layer;auto world=ch->get_world_matrix();layer.x=ox+world.m_[0][2]*sx;layer.y=oy+world.m_[1][2]*sy;
        cxform result_tint=ch->get_world_cxform();
        struct RestoreColors {
            std::vector<std::pair<character*,cxform>> colors;
            ~RestoreColors(){for(auto& p:colors)p.first->m_color_transform=p.second;}
        } saved;
        // Filters act before the object's world color transform. Preserve child
        // color changes while removing this object's and its ancestors' tints
        // from the off-screen source. No timeline advances while displaying.
        for(auto node=ch;node;node=node->get_parent()){saved.colors.emplace_back(node,node->m_color_transform);node->m_color_transform=cxform();}
        struct CaptureGuard {FilterLayer*& slot;FilterLayer* previous;CaptureGuard(FilterLayer*& s,FilterLayer* next):slot(s),previous(s){slot=next;}~CaptureGuard(){slot=previous;}};
        {CaptureGuard guard(capture,&layer);ch->display();}
        for(auto& p:saved.colors)p.first->m_color_transform=p.second;saved.colors.clear();
        if(layer.ops.empty())return true;
        std::uint64_t hash=14695981039346656037ULL;
        auto add=[&](const void* data,unsigned bytes){auto p=static_cast<const unsigned char*>(data);while(bytes--){hash^=*p++;hash*=1099511628211ULL;}};
        auto number=[&](float value){auto fixed=std::int64_t(std::round(value*65536));add(&fixed,sizeof(fixed));};
        float left=1e30f,top=1e30f,right=-1e30f,bottom=-1e30f;
        for(const auto& f:*ch->m_filters) {
            add(&f.type,sizeof(f.type));add(&f.passes,sizeof(f.passes));add(&f.color,4);add(&f.highlight,4);
            number(f.blur_x);number(f.blur_y);number(f.angle);number(f.distance);number(f.strength);
            if(f.type==6)for(float value:f.matrix)number(value);
            unsigned flags=f.inner|(f.knockout<<1)|(f.composite<<2)|(f.on_top<<3);add(&flags,sizeof(flags));
        }
        number(sx);number(sy);add(&image_quality,sizeof(image_quality));
        bool mask_geometry=false;
        for(const auto& op:layer.ops) {
            add(&op.kind,sizeof(op.kind));
            if(op.kind){mask_geometry=op.kind==1;continue;}
            unsigned id=op.texture?op.texture->id:0;add(&id,sizeof(id));add(&op.wrap,sizeof(op.wrap));
            for(const auto& v:op.v) {
                number(v.x);number(v.y);number(v.u);number(v.v);
                auto key_color=v.color;
                if(mask_geometry){key_color&=0xff000000;if(!op.texture && key_color)key_color=0xff000000;}
                add(&key_color,sizeof(key_color));
                // A TextField's clipping rectangle can span the whole screen
                // while its visible content is one digit. Stencil geometry is
                // not painted content and must not enlarge the filter image.
                if(!mask_geometry && (v.color>>24)) {
                    left=std::min(left,v.x);right=std::max(right,v.x);top=std::min(top,v.y);bottom=std::max(bottom,v.y);
                }
            }
        }
        if(right<left || bottom<top)return true;
        auto padding=glorton::filter_padding(*ch->m_filters,sx*20,sy*20);
        int x0=std::max(int(glorton::raster_floor(left))-padding.x,int(glorton::raster_floor(36-layer.x))-padding.x);
        int y0=std::max(int(glorton::raster_floor(top))-padding.y,int(glorton::raster_floor(-layer.y))-padding.y);
        int x1=std::min(int(glorton::raster_ceil(right))+padding.x,int(glorton::raster_ceil(444-layer.x))+padding.x);
        int y1=std::min(int(glorton::raster_ceil(bottom))+padding.y,int(glorton::raster_ceil(272-layer.y))+padding.y);
        if(x1<=x0 || y1<=y0)return true;
        for(int value:{x0,y0,x1,y1})add(&value,sizeof(value));
        auto found=std::find_if(filter_cache.begin(),filter_cache.end(),[&](const FilterCache& item){return item.key==hash;});
        if(found==filter_cache.end()) {
            glorton_filter_detail(ch->get_id(),ch->get_name().c_str(),x1-x0,y1-y0,layer.ops.size());
            const char* allocation_stage="surface";Bitmap* allocation_texture=nullptr;
            try {
            ++filter_misses;glorton::FilterSurface surface(x0,y0,x1-x0,y1-y0);
            bool prefix_enabled=layer.ops.size()>=64&&surface.pixels.size()>=16000;
            std::vector<doubleedged::FilterRasterKey> prefix_keys;
            unsigned start=0;
            if(prefix_enabled){
                prefix_keys.reserve(layer.ops.size());
                for(const auto& op:layer.ops){
                    doubleedged::FilterRasterKey key{};
                    static_assert(sizeof(op.v)==18*4);
                    key[0]=op.kind;key[1]=op.texture?op.texture->id:0;key[2]=unsigned(op.wrap)|(image_quality!=0?0x100u:0u);
                    std::memcpy(key.data()+3,op.v,sizeof(op.v));prefix_keys.push_back(std::move(key));
                }
                start=filter_prefix.resume(prefix_keys,surface);
                glorton_prefix_trace(start,layer.ops.size());
            }
            for(unsigned index=start;index<layer.ops.size();++index) {
                const auto& op=layer.ops[index];
                if(op.kind){if(op.kind==1)surface.begin_mask();else if(op.kind==2)surface.end_mask();else surface.pop_mask();}
                else{
                glorton::TextureView texture;
                allocation_stage="texture";allocation_texture=op.texture;
                if(op.texture){op.texture->activate();texture={op.texture->data,op.texture->tw,op.texture->th,image_quality!=0,op.wrap==WRAP_REPEAT};if(op.texture->palette_entries){texture.indices=reinterpret_cast<const uint8_t*>(op.texture->data);texture.palette=op.texture->palette();texture.index_bits=op.texture->index_bits;}}
                allocation_stage="triangle";
                auto v=[&](const Vertex& in){return glorton::RasterVertex{in.x,in.y,in.u,in.v,in.color};};
                surface.triangle(v(op.v[0]),v(op.v[1]),v(op.v[2]),texture);
                }
                if(prefix_enabled)filter_prefix.checkpoint(index+1,surface);
            }
            if(prefix_enabled)filter_prefix.finish(std::move(prefix_keys));
            allocation_stage="apply";surface.apply(*ch->m_filters,sx*20,sy*20);surface.unpremultiply();
            allocation_stage="pack";
            FilterCache item{hash,++filter_clock,{}};
            for(int y=0;y<surface.height;y+=254)for(int x=0;x<surface.width;x+=254) {
                int w=std::min(254,surface.width-x),h=std::min(254,surface.height-y);
                item.tiles.push_back({x0+x,y0+y,w,h,filtered_bitmap(w,h,surface,x,y)});
            }
            filter_cache.push_back(std::move(item));found=filter_cache.end()-1;
            } catch(const std::bad_alloc&) {
                if(FILE* f=std::fopen("FILTER_MEMORY.LOG","a")){std::fprintf(f,"frame=%u symbol=%d phase=%s surface=%dx%d texture=%u size=%dx%d bank=%u packed=%u resident=%u pool=%u retired=%u\n",display_frame,ch->get_id(),allocation_stage,x1-x0,y1-y0,allocation_texture?allocation_texture->id:0,allocation_texture?allocation_texture->tw:0,allocation_texture?allocation_texture->th:0,allocation_texture?allocation_texture->bank_size:0,allocation_texture?unsigned(allocation_texture->compressed.size()):0,Bitmap::resident_bytes,pool_bytes,retired_bytes);std::fclose(f);}
                throw;
            }
        } else {++filter_hits;found->used=++filter_clock;}
        bool cpu_tint=false;
        for(int c=0;c<4;++c)if(result_tint.m_[c][1]!=0 || result_tint.m_[c][0]<0 || result_tint.m_[c][0]>1)cpu_tint=true;
        rgba modulation=cpu_tint?rgba():rgba(byte(result_tint.m_[0][0]*255),byte(result_tint.m_[1][0]*255),byte(result_tint.m_[2][0]*255),byte(result_tint.m_[3][0]*255));
        for(const auto& tile:found->tiles) {
            auto b=tile.bitmap.get();
            if(cpu_tint) {
                auto variant=std::find_if(filter_tints.begin(),filter_tints.end(),[&](const FilterTint& v){return v.id==b->id&&!std::memcmp(&v.tint,&result_tint,sizeof(cxform));});
                if(variant==filter_tints.end()) {
                    auto tinted=std::make_unique<Bitmap>();tinted->width=b->width;tinted->height=b->height;tinted->rw=b->rw;tinted->rh=b->rh;tinted->tw=b->tw;tinted->th=b->th;
                    tinted->compressed=b->compressed;tinted->apply_tint=true;tinted->pixel_tint=result_tint;
                    filter_tints.push_back({b->id,result_tint,filter_clock,std::move(tinted)});variant=filter_tints.end()-1;
                }
                variant->used=filter_clock;b=variant->bitmap.get();
            }
            float x=layer.x+tile.x,y=layer.y+tile.y;auto c=color(modulation);
            Vertex quad[]={{1,1,c,x,y,0},{float(tile.w+1),1,c,x+tile.w,y,0},{1,float(tile.h+1),c,x,y+tile.h,0},{float(tile.w+1),float(tile.h+1),c,x+tile.w,y+tile.h,0}};
            submit_quad(b,WRAP_CLAMP,quad);
        }
        return true;
    }
    bool can_release_shape_geometry(int id) const override {
        if(!baked_art_ready || std::binary_search(glorton::button_hit_shapes.begin(),glorton::button_hit_shapes.end(),unsigned(id)))return false;
        return find_art(id,0)!=art_entries.end();
    }
    bool submitting_mask=false;
    matrix transform;
    cxform tint;
    struct Style { bool enabled=false; Bitmap* bitmap=nullptr; matrix uv; rgba color; bitmap_wrap_mode wrap=WRAP_CLAMP; } fills[2],line;
    float line_width=20, sx=1,sy=1,ox=0,oy=0,movie_width=12000,movie_height=8000;
    int mask_level=0;
    bool drawing=false;
#if DE_CRISP_ART
    int bound_min_filter=-1,bound_mag_filter=-1;
#endif
    rect local_view;
    ~Renderer() override {
#if DE_PREPARE_TEXTURES
        if(prepare_fd>=0)sceIoClose(prepare_fd);
#endif
        if(art_fd>=0)sceIoClose(art_fd);if(art_file)std::fclose(art_file);baked_art_ready=false;
    }
    void load_art() {
        if(FILE* marker=std::fopen("SOURCE_PROFILE.QA","rb")){media_stats.enabled=true;std::fclose(marker);}
        art_file=std::fopen("art.pak","rb");if(!art_file)return;
        uint32_t header[5];
        if(std::fread(header,sizeof(header),1,art_file)!=1 || header[0]!=0x52414e44 || (header[1]!=2 && header[1]!=3 && header[1]!=4 && header[1]!=5 && header[1]!=6 && header[1]!=7) || header[2]>20000 || header[3]>100000)std::abort();
        art_density_derivative=header[1]==5;
        art_entries.resize(header[2]);art_tiles.resize(header[3]);art_formats.resize(header[3]);art_textures.resize(header[3]);
        if(header[1]==2) {
            for(auto& e:art_entries){ArtEntryV2 old;if(std::fread(&old,sizeof(old),1,art_file)!=1)std::abort();e={old.id,old.ratio,old.lod,old.first,old.count,0};}
            for(auto& t:art_tiles){ArtTileV2 old;if(std::fread(&old,sizeof(old),1,art_file)!=1)std::abort();t={old.offset,old.size,old.tw,old.th,old.width,old.height,old.left,old.top,old.left+old.w,old.top+old.h,0,0,old.width,old.height};}
            // Evaluate the original density expression with the original width,
            // before splitting geometry or rounding its endpoint subtraction.
            for(auto& e:art_entries)if(e.count) {
                ArtTileV2 old;if(std::fseek(art_file,sizeof(header)+header[2]*sizeof(ArtEntryV2)+e.first*sizeof(ArtTileV2),SEEK_SET) || std::fread(&old,sizeof(old),1,art_file)!=1)std::abort();
                e.density=20*old.width/old.w;
            }
        } else {
            if(std::fread(art_entries.data(),sizeof(ArtEntry),art_entries.size(),art_file)!=art_entries.size())std::abort();
            if(header[1]==6||header[1]==7){
                for(unsigned i=0;i<art_tiles.size();++i){ArtTileV6 tile;
                    if(std::fread(&tile,sizeof(tile),1,art_file)!=1||tile.format>(header[1]==7?2:1)||
                       (!tile.format&&tile.palette_entries)||(tile.format&&(!tile.palette_entries||tile.palette_entries>256||tile.palette_entries%8||tile.tile.tw<16||tile.tile.tw%16))||
                       (tile.format==2&&(tile.palette_entries!=16||tile.tile.tw<32||tile.tile.tw%32)))std::abort();
                    art_tiles[i]=tile.tile;art_formats[i]={tile.format,tile.palette_entries};
                }
            }else if(std::fread(art_tiles.data(),sizeof(ArtTile),art_tiles.size(),art_file)!=art_tiles.size())std::abort();
        }
        uint32_t maximum=0;for(const auto& tile:art_tiles)maximum=std::max(maximum,tile.size);
        bank_scratch.resize(maximum);art_lz4=header[1]>=4;art_fd=open_psp_bank("art.pak");media_stats.native_bank_io=art_fd>=0;
#if DE_PREPARE_TEXTURES
        prepare_fd=open_psp_bank("art.pak");
#endif
        unsigned bank_bytes=0;
        if(std::fseek(art_file,0,SEEK_END)==0){long size=std::ftell(art_file);if(size>0){
            bank_bytes=unsigned(size);
            if(bank_bytes<=ResidentBankBytes){
                // Optional residency must not make the streaming path fail
                // under memory pressure. Every tile still has its bank offset.
                try{compressed_art.resize(bank_bytes);}catch(const std::bad_alloc&){compressed_art.clear();}
                if(!compressed_art.empty()){std::rewind(art_file);if(std::fread(compressed_art.data(),1,bank_bytes,art_file)!=bank_bytes)graphics_failure("Resident texture bank read");}
            }
        }}
        if(art_fd>=0&&compressed_art.empty())art_bank_cache.initialize(bank_bytes);
        de_art_bank_trace(unsigned(compressed_art.size()),ResidentBankBytes,bank_bytes);
#if DE_ART_SHAPE_INDEX
        art_shape_index.build(art_entries);
#endif
        baked_art_ready=true;
    }
    std::unique_ptr<Bitmap> art_texture(unsigned index,const cxform* tint=nullptr) {
        const auto& tile=art_tiles[index];auto b=std::make_unique<Bitmap>();
        b->width=b->rw=tile.width;b->height=b->rh=tile.height;b->tw=tile.tw;b->th=tile.th;
        b->palette_entries=art_formats[index].palette_entries;b->index_bits=art_formats[index].format==2?4:8;
        b->bank=art_file;b->bank_fd=art_fd;b->bank_lz4=art_lz4;b->bank_offset=tile.offset;b->bank_size=tile.size;b->bank_memory=compressed_art.empty()?nullptr:compressed_art.data();
        if(tint){b->pixel_tint=*tint;b->apply_tint=true;}
        return b;
    }
    bool has_baked_shape(int id,Uint16 ratio) const override {
        if(!baked_art_ready)return false;
        return find_art(id,ratio)!=art_entries.end();
    }
    bool draw_shape(int id,const matrix& matrix,const cxform& tint,Uint16 ratio) override {
        if(!baked_art_ready)return false;
#if DE_PREPARE_TEXTURES
        if(preparing&&!fast_color_filters())return true;
#endif
        auto entry=find_art(id,ratio);
        if(entry==art_entries.end())return false;
        // Screen-space scale selects an offline raster level. Source geometry,
        // transforms, hit tests and timeline ratios remain in author coordinates.
        auto alternative=entry+1;
        if(alternative!=art_entries.end() && alternative->id==unsigned(id) && alternative->ratio==ratio) {
            // A single raster level has no selection decision. Most shapes in
            // the bounded-density bank take this path without two square roots.
            float a=matrix.m_[0][0]*sx,b=matrix.m_[1][0]*sy,c=matrix.m_[0][1]*sx,d=matrix.m_[1][1]*sy;
            float trace=a*a+b*b+c*c+d*d,determinant=(a*d-b*c)*(a*d-b*c);
            float density_factor=(image_quality==2?.5f:image_quality==0?.25f:.35f)*ArtDensityScale;
            float needed=density_factor*20*std::sqrt((trace+std::sqrt(std::max(0.f,trace*trace-4*determinant)))/2);
            for(auto next=alternative;next!=art_entries.end() && next->id==unsigned(id) && next->ratio==ratio;++next) {
                if(!next->count)continue;
                float density=next->density;
                if(density>=needed)entry=next;else break;
            }
        }
        // DNARv5 retains original tile rectangles and integer UV endpoints.
        // The high LOD bits encode an offline power-of-two raster reduction;
        // scale only texture coordinates, never geometry or source transforms.
        unsigned raster_shift=art_density_derivative?entry->lod>>12:0;
        float raster_uv=1.f/float(1U<<raster_shift);
        bool cpu_tint=false;
        for(int c=0;c<4;c++)if(tint.m_[c][1]!=0 || tint.m_[c][0]<0 || tint.m_[c][0]>1)cpu_tint=true;
        rgba modulation=cpu_tint?rgba():rgba(byte(tint.m_[0][0]*255),byte(tint.m_[1][0]*255),byte(tint.m_[2][0]*255),byte(tint.m_[3][0]*255));
        // 60 FPS candidate: approximate additive/inverting flashes using GPU
        // modulation; never allocate/decode a changing full-image tint variant.
        // Script color values and all combat/physics state remain untouched.
        if(cpu_tint && fast_color_filters()) {
            modulation=rgba(byte(std::clamp(float(tint.m_[0][0])+float(tint.m_[0][1])/255.f,0.f,1.f)*255),
                byte(std::clamp(float(tint.m_[1][0])+float(tint.m_[1][1])/255.f,0.f,1.f)*255),
                byte(std::clamp(float(tint.m_[2][0])+float(tint.m_[2][1])/255.f,0.f,1.f)*255),
                byte(std::clamp(float(tint.m_[3][0])+float(tint.m_[3][1])/255.f,0.f,1.f)*255));
            cpu_tint=false;
        }
        for(unsigned index=entry->first;index<entry->first+entry->count;index++) {
#if DE_PREPARE_TEXTURES
            // Preparation only needs missing immutable resources. Presentation
            // still does its original visibility, LRU and geometry work. The
            // GPU mutex protects this residency check from eviction.
            if(preparing && art_textures[index] && art_textures[index]->data){++prepare_stats.ready_skips;continue;}
#endif
            const auto& t=art_tiles[index];
            point corners[]={screen({t.left,t.top},matrix),screen({t.right,t.top},matrix),screen({t.left,t.bottom},matrix),screen({t.right,t.bottom},matrix)};
            float left=1e30f,right=-1e30f,top=1e30f,bottom=-1e30f;
            for(auto p:corners){left=std::min(left,p.m_x);right=std::max(right,p.m_x);top=std::min(top,p.m_y);bottom=std::max(bottom,p.m_y);}
            if(!capture && (right<35 || left>445 || bottom<-1 || top>273))continue;
            Bitmap* b;
            if(!cpu_tint) {
                if(!art_textures[index])art_textures[index]=art_texture(index);
                b=art_textures[index].get();
            } else {
                auto variant=std::find_if(variants.begin(),variants.end(),[&](const Variant& v){return v.tile==index && !std::memcmp(&v.tint,&tint,sizeof(cxform));});
                if(variant==variants.end()){
#ifdef DE_CACHE_DIAGNOSTICS
                    ++tint_misses;
#endif
                    if(!capture && variants.size()>=TintVariantLimit){auto oldest=std::min_element(variants.begin(),variants.end(),[](const Variant& a,const Variant& b){return a.used<b.used;});variants.erase(oldest);}
                    variants.push_back({index,tint,art_texture(index,&tint),Bitmap::clock});variant=variants.end()-1;
                }
#ifdef DE_CACHE_DIAGNOSTICS
                else ++tint_hits;
#endif
                variant->used=Bitmap::clock;
                b=variant->texture.get();
            }
#if DE_PREPARE_TEXTURES
            if(preparing){consider(b);continue;}
#endif
            auto c=color(modulation);
            Vertex quad[]={vertex({t.left,t.top},matrix,c,t.u0*raster_uv,t.v0*raster_uv),vertex({t.right,t.top},matrix,c,t.u1*raster_uv,t.v0*raster_uv),
                vertex({t.left,t.bottom},matrix,c,t.u0*raster_uv,t.v1*raster_uv),vertex({t.right,t.bottom},matrix,c,t.u1*raster_uv,t.v1*raster_uv)};
            submit_quad(b,WRAP_CLAMP,quad);
        }
        return true;
    }
    void open() override {
        pspboot::mark("renderer-enter");
        if(!texture_arena.ready()){
            auto storage=memalign(16,TextureCacheBytes);
            if(!storage)graphics_failure("Texture arena initialization");
            texture_arena.initialize(storage);
        }
        texture_codecs.open();
        load_art();
        pspboot::mark("art-index-ready");
        // Retire the cached BSS alias before GU starts writing uncached data.
        std::memset(commands,0,sizeof(commands));
        sceKernelDcacheWritebackInvalidateRange(commands,sizeof(commands));
        pspboot::mark("command-cache-ready",int(reinterpret_cast<uintptr_t>(commands)));
        sceGuInit();guSwapBuffersBehaviour(PSP_DISPLAY_SETBUF_NEXTHSYNC);sceGuStart(GU_DIRECT,commands);
        pspboot::mark("gu-initialized");
        sceGuDrawBuffer(GU_PSM_8888,nullptr,Stride);
        sceGuDispBuffer(Width,Height,reinterpret_cast<void*>(BufferBytes),Stride);
        sceGuOffset(2048-Width/2,2048-Height/2);sceGuViewport(2048,2048,Width,Height);
        sceGuScissor(0,0,Width,Height);sceGuEnable(GU_SCISSOR_TEST);
        sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);
        sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
        sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexScale(1,1);sceGuTexOffset(0,0);
        sceGuAlphaFunc(GU_GREATER,0,0xff);sceGuEnable(GU_ALPHA_TEST);
        sceGuFinish();sceGuSync(0,0);sceDisplayWaitVblankStart();sceGuDisplay(GU_TRUE);
    }
    bitmap_info* create_bitmap_info_empty() override { return new Bitmap; }
    bitmap_info* create_bitmap_info_alpha(int w,int h,unsigned char* p) override { return new Bitmap(w,h,p,1,w); }
    bitmap_info* create_bitmap_info_rgb(image::rgb* p) override { return new Bitmap(p->m_width,p->m_height,p->m_data,3,p->m_pitch); }
    bitmap_info* create_bitmap_info_rgba(image::rgba* p) override { return new Bitmap(p->m_width,p->m_height,p->m_data,4,p->m_pitch); }
    video_handler* create_video_handler() override { return nullptr; }
    void begin_display(rgba bg,int x,int y,int w,int h,float x0,float x1,float y0,float y1) override {
        if(media_stats.enabled)media_stats.frame_begin=sceKernelGetSystemTimeWide();
        sx=w/(x1-x0);sy=h/(y1-y0);ox=x-x0*sx;oy=y-y0*sy;
        movie_width=x1-x0;movie_height=y1-y0;mask_level=0;
        sceGuStart(GU_DIRECT,commands);drawing=true;active_list=true;list_bytes=0;
        sceGuTexFilter(image_quality==0?GU_NEAREST:GU_LINEAR,image_quality==0?GU_NEAREST:GU_LINEAR);
#if DE_CRISP_ART
        bound_min_filter=bound_mag_filter=image_quality==0?GU_NEAREST:GU_LINEAR;
#endif
        frame_draws=largest_allocation=total_bytes=0;
        gpu_flushes=gpu_flush_us=retired_peak=0;
        cache_hits=cache_misses=cache_decoded=0;
#ifdef DE_CACHE_DIAGNOSTICS
        budget_evictions=pressure_evictions=tint_hits=tint_misses=0;
#endif
        filter_hits=filter_misses=0;
        sceGuScissor(0,0,Width,Height);sceGuPixelMask(0);sceGuDisable(GU_STENCIL_TEST);
        sceGuClearColor(0);sceGuClearStencil(0);
        sceGuClear(GU_COLOR_BUFFER_BIT|GU_STENCIL_BUFFER_BIT);
        sceGuPixelMask(0xff000000);sceGuScissor(x,y,w,h);
        sceGuDisable(GU_TEXTURE_2D);
        auto background=static_cast<Vertex*>(vertices(2*sizeof(Vertex)));
        background[0]={0,0,color(bg),float(x),float(y),0};background[1]={0,0,color(bg),float(x+w),float(y+h),0};
        sceGuDrawArray(GU_SPRITES,VertexFormat,2,nullptr,background);
    }
    void end_display() override {
        static unsigned frame=0;
#ifdef DE_CACHE_DIAGNOSTICS
        static FILE* diagnostic=std::fopen("CACHE_DIAGNOSTIC.LOG","w");
        if(diagnostic){std::fprintf(diagnostic,"frame=%u budget=%u resident=%u textures=%u variant_limit=%u variants=%u tint_hits=%u tint_new=%u evictions=%u pressure=%u failures=%u pool=%u decoded=%u misses=%u\n",frame,TextureCacheBytes,Bitmap::resident_bytes,unsigned(Bitmap::resident.size()),TintVariantLimit,unsigned(variants.size()),tint_hits,tint_misses,budget_evictions,pressure_evictions,texture_failures,pool_bytes,cache_decoded,cache_misses);std::fflush(diagnostic);}
#endif
        if(frame<8 || frame%60==0 || largest_allocation>600*1024) {
            glorton_graphics_trace(frame,frame_draws,largest_allocation,total_bytes);
            glorton_cache_trace(frame,cache_hits,cache_misses,cache_decoded,Bitmap::resident_bytes);
            de_texture_arena_trace(frame,TextureCacheBytes,unsigned(texture_arena.used_bytes()),Bitmap::resident_bytes);
            glorton_gpu_trace(frame,gpu_flushes,gpu_flush_us,retired_peak);
            glorton_filter_trace(frame,filter_hits,filter_misses,filter_cache.size());
            glorton_pool_trace(frame,pool_bytes,pool_reuses,texture_failures,largest_texture_failure);
        }
        // Captured operations hold raw bitmap pointers only for this frame.
        // Prune after all layers have completed, while deferred texture frees
        // still retain allocations referenced by the submitted GU display list.
        unsigned packed=0;for(const auto& item:filter_cache)for(const auto& tile:item.tiles)packed+=tile.bitmap->compressed.capacity();
        while(filter_cache.size()>64 || packed>2*1024*1024) {
            auto oldest=std::min_element(filter_cache.begin(),filter_cache.end(),[](const FilterCache& a,const FilterCache& b){return a.used<b.used;});
            if(oldest==filter_cache.end())break;
            for(const auto& tile:oldest->tiles)packed-=tile.bitmap->compressed.capacity();filter_cache.erase(oldest);
        }
        while(filter_tints.size()>64){auto oldest=std::min_element(filter_tints.begin(),filter_tints.end(),[](const FilterTint& a,const FilterTint& b){return a.used<b.used;});filter_tints.erase(oldest);}
        while(variants.size()>TintVariantLimit){auto oldest=std::min_element(variants.begin(),variants.end(),[](const Variant& a,const Variant& b){return a.used<b.used;});variants.erase(oldest);}
        ++frame;
        ++display_frame;
        if(media_stats.enabled){unsigned elapsed=unsigned(sceKernelGetSystemTimeWide()-media_stats.frame_begin);++media_stats.frames;media_stats.draw_us+=elapsed;media_stats.max_draw_us=std::max(media_stats.max_draw_us,elapsed);media_stats.misses+=cache_misses;media_stats.decoded_bytes+=cache_decoded;media_stats.resident_peak=std::max(media_stats.resident_peak,Bitmap::resident_bytes);}
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
            if(media_stats.enabled){if(!b->palette_entries)++media_stats.rgba_binds;else if(b->index_bits==4)++media_stats.t4_binds;else ++media_stats.t8_binds;}
#if DE_CRISP_ART
            // Baked pixel-art tiles keep discrete source pixels when enlarged.
            // Alpha glyphs and computed filter surfaces retain their previous
            // sampling, as does minification. Avoid redundant GE state writes.
            int minimum=image_quality==0?GU_NEAREST:GU_LINEAR;
            int maximum=b->bank?GU_NEAREST:minimum;
            if(minimum!=bound_min_filter||maximum!=bound_mag_filter){
                sceGuTexFilter(minimum,maximum);bound_min_filter=minimum;bound_mag_filter=maximum;
            }
#endif
            sceGuTexMode(b->palette_entries?(b->index_bits==4?GU_PSM_T4:GU_PSM_T8):GU_PSM_8888,0,0,0);
            if(b->palette_entries){sceGuClutMode(GU_PSM_8888,0,b->index_bits==4?15:255,0);sceGuClutLoad(b->palette_entries/8,b->palette());}
            sceGuEnable(GU_TEXTURE_2D);sceGuTexImage(0,b->tw,b->th,b->tw,b->data);
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
        return right>=36-margin && left<=444+margin && bottom>=-margin && top<=272+margin;
    }
    void mesh(const void* coords,int count,int primitive) {
        auto& style=fills[0];if(!style.enabled || count<=0) return;
        auto in=static_cast<const coord_component*>(coords);
        Vertex batch[768];unsigned used=0;bool bound=false;
        auto flush=[&] {
            if(!used)return;
            if(capture){for(unsigned i=0;i<used;i+=3)record_triangle(style.bitmap,style.wrap,batch+i);used=0;return;}
            if(!bound){bind(style.bitmap,style.wrap);bound=true;}
            auto out=static_cast<Vertex*>(vertices(used*sizeof(Vertex)));std::memcpy(out,batch,used*sizeof(Vertex));
            sceGuDrawArray(GU_TRIANGLES,VertexFormat,used,nullptr,out);used=0;
        };
        const int step=primitive==GU_TRIANGLES?3:1;
        for(int i=2;i<count;i+=step) {
            int ids[]={i-2,i-1,i};
            const float ax=in[(i-2)*2],ay=in[(i-2)*2+1],bx=in[(i-1)*2],by=in[(i-1)*2+1],cx=in[i*2],cy=in[i*2+1];
            if((ax==bx && ay==by)||(ax==cx && ay==cy)||(bx==cx && by==cy))continue;
            if(!capture && (std::max({ax,bx,cx})<local_view.m_x_min || std::min({ax,bx,cx})>local_view.m_x_max ||
               std::max({ay,by,cy})<local_view.m_y_min || std::min({ay,by,cy})>local_view.m_y_max))continue;
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
            if(!used)return;
            if(capture){for(unsigned i=0;i<used;i+=3)record_triangle(nullptr,WRAP_CLAMP,batch+i);used=0;return;}
            if(!bound){bind(nullptr,WRAP_CLAMP);bound=true;}
            auto out=static_cast<Vertex*>(vertices(used*sizeof(Vertex)));std::memcpy(out,batch,used*sizeof(Vertex));
            sceGuDrawArray(GU_TRIANGLES,VertexFormat,used,nullptr,out);used=0;
        };
        auto v=[&](float x,float y)->Vertex { return {0,0,color(line.color),x,y,0}; };
        for(int i=0;i<count-1;i++) {
            float ax=in[i*2],ay=in[i*2+1],bx=in[i*2+2],by=in[i*2+3],margin=line_width*2+40;
            if(!capture && (std::max(ax,bx)<local_view.m_x_min-margin || std::min(ax,bx)>local_view.m_x_max+margin ||
               std::max(ay,by)<local_view.m_y_min-margin || std::min(ay,by)>local_view.m_y_max+margin))continue;
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
#if DE_PREPARE_TEXTURES
        if(preparing){auto b=static_cast<Bitmap*>(raw);if(b&&b->data){++prepare_stats.ready_skips;return;}}
#endif
        point corners[]={screen({r.m_x_min,r.m_y_min},m),screen({r.m_x_max,r.m_y_min},m),screen({r.m_x_min,r.m_y_max},m),screen({r.m_x_max,r.m_y_max},m)};
        float left=1e30f,right=-1e30f,top=1e30f,bottom=-1e30f;
        for(auto p:corners){left=std::min(left,p.m_x);right=std::max(right,p.m_x);top=std::min(top,p.m_y);bottom=std::max(bottom,p.m_y);}
        if(!capture && (right<36 || left>444 || bottom<0 || top>272))return;
        auto b=static_cast<Bitmap*>(raw);if(!b || !b->tw || !b->th)return;
#if DE_PREPARE_TEXTURES
        if(preparing){consider(b);return;}
#endif
        Vertex quad[]={vertex({r.m_x_min,r.m_y_min},m,color(c),uv.m_x_min*b->rw,uv.m_y_min*b->rh),
            vertex({r.m_x_max,r.m_y_min},m,color(c),uv.m_x_max*b->rw,uv.m_y_min*b->rh),
            vertex({r.m_x_min,r.m_y_max},m,color(c),uv.m_x_min*b->rw,uv.m_y_max*b->rh),
            vertex({r.m_x_max,r.m_y_max},m,color(c),uv.m_x_max*b->rw,uv.m_y_max*b->rh)};
        submit_quad(b,WRAP_CLAMP,quad);
    }
    void begin_submit_mask() override {
        if(capture){FilterOp op;op.kind=1;capture->ops.push_back(op);return;}
        submitting_mask=true;
        // A previous mask leaves alpha writes protected. Stencil occupies
        // alpha in the8888framebuffer, so allow it before clearing a sibling
        // mask; otherwise old coverage remains active for later siblings.
        sceGuPixelMask(0x00ffffff);
        if(mask_level==0) { sceGuEnable(GU_STENCIL_TEST);sceGuClearStencil(0);sceGuClear(GU_STENCIL_BUFFER_BIT); }
        sceGuStencilFunc(GU_EQUAL,mask_level++,0xff);
        sceGuDisable(GU_ALPHA_TEST);
        sceGuStencilOp(GU_KEEP,GU_KEEP,GU_INCR);
    }
    void end_submit_mask() override {
        if(capture){FilterOp op;op.kind=2;capture->ops.push_back(op);return;}
        submitting_mask=false;
        sceGuPixelMask(0xff000000);sceGuStencilFunc(GU_EQUAL,mask_level,0xff);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);
        sceGuEnable(GU_ALPHA_TEST);
    }
    void disable_mask() override {
        if(capture){FilterOp op;op.kind=3;capture->ops.push_back(op);return;}
        if(mask_level<=0)return;
        if(--mask_level==0) { sceGuDisable(GU_STENCIL_TEST);return; }
        sceGuPixelMask(0x00ffffff);sceGuStencilFunc(GU_EQUAL,mask_level+1,0xff);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_DECR);
        bind(nullptr,WRAP_CLAMP);auto out=static_cast<Vertex*>(vertices(2*sizeof(Vertex)));
        out[0]={0,0,0xffffffff,0,0,0};out[1]={0,0,0xffffffff,Width,Height,0};
        sceGuDrawArray(GU_SPRITES,VertexFormat,2,nullptr,out);end_submit_mask();
    }
    bool test_stencil_buffer(const rect&,Uint8) override { return false; }
    bool is_visible(const rect& b) override {
        return b.m_x_max>=0 && b.m_x_min<=movie_width && b.m_y_max>=0 && b.m_y_min<=movie_height;
    }
};
}
gameswf::render_handler* make_psp_renderer() { return new Renderer; }
bool release_psp_texture() {return Bitmap::evict_oldest(true);}
void prepare_psp_tick(unsigned tick) {
    // Intro-only textures must not compete with the initial actor pool.
    if(tick==2)while(Bitmap::evict_oldest(true)){}
    // malloc grows its arena on demand. fordblks is free space in that arena,
    // not unused PSP heap capacity: keeping it above2MiB evicts live textures
    // before any allocation actually needs memory. Real failed allocations
    // already evict through the requested-size allocation-pressure path.
#ifdef DE_LEGACY_HEAP_RESERVE
    while(mallinfo().fordblks<2*1024*1024 && Bitmap::evict_oldest(true)){}
#endif
}
void set_psp_pointer(float x,float y,bool visible){pointer_x=x;pointer_y=y;pointer_visible=visible;}
void set_psp_quality(const char* quality){image_quality=std::strcmp(quality,"LOW")==0?0:std::strcmp(quality,"HIGH")==0?2:1;}
bool set_psp_texture_pool_mib(unsigned mib){if(mib>GLORTON_TEXTURE_POOL_MB)return false;pool_limit_bytes=mib*1024*1024;return true;}
void set_psp_bank_scratch(bool enabled){use_bank_scratch=enabled;}
void set_psp_codec_reuse(bool enabled){use_persistent_codecs=enabled;}
bool fast_color_filters(){return fast_color_mode.load(std::memory_order_relaxed);}
void set_fast_color_filters(bool enabled){fast_color_mode.store(enabled,std::memory_order_relaxed);g_double_edged_picture_mode=enabled?1:0;}
void notify_psp_picture_mode(){picture_notice_frames=90;}
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
void prepare_psp_capture(const psp60::Frame& frame,void(*lock)(),void(*unlock)()){
#if DE_PREPARE_TEXTURES
    if(port_renderer)port_renderer->prepare(frame,lock,unlock);
#endif
}
void flush_psp_prepare_stats(){
#if DE_PREPARE_TEXTURES
    if(port_renderer)port_renderer->write_prepare_stats();
#endif
}
namespace {
unsigned long long last_present_time=0,last_latch_time=0;unsigned last_present_vcount=0;
bool latch_probe=false,latch_confirmed=false,has_submitted=false;
void observe_latch(){
    latch_confirmed=false;last_latch_time=sceKernelGetSystemTimeWide();
    if(!latch_probe||!has_submitted)return;
    void* shown=nullptr;int stride=0,format=0;
    auto expected=reinterpret_cast<std::uintptr_t>(sceGeEdramGetAddr())+reinterpret_cast<std::uintptr_t>(displayed_buffer);
    latch_confirmed=sceDisplayGetFrameBuf(&shown,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)>=0&&stride==Stride&&
        (reinterpret_cast<std::uintptr_t>(shown)&0x0fffffffu)==(expected&0x0fffffffu);
}
}
void set_psp_latch_probe(bool value){latch_probe=value;}
bool psp_latch_confirmed(){return latch_confirmed;}
unsigned long long psp_latch_time(){return last_latch_time;}
unsigned long long psp_present_time(){return last_present_time;}
unsigned psp_present_vcount(){return last_present_vcount;}
void submit_psp_frame(const char* overlay,bool footer) {
    if(picture_notice_frames){
        --picture_notice_frames;sceGuPixelMask(0);sceGuDisable(GU_STENCIL_TEST);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_TEXTURE_2D);
        sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        Vertex box[]={{0,0,0xe0202020,145,251,0},{0,0,0xe0202020,335,251,0},{0,0,0xe0202020,145,271,0},{0,0,0xe0202020,335,271,0}};
        auto output=static_cast<Vertex*>(vertices(sizeof(box)));std::memcpy(output,box,sizeof(box));sceGuDrawArray(GU_TRIANGLE_STRIP,VertexFormat,4,nullptr,output);
        draw_native_label(fast_color_filters()?"PICTURE: SMOOTH":"PICTURE: ORIGINAL",154,266,12);
    }
    if(pointer_visible){
        sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_STENCIL_TEST);
        auto out=static_cast<Vertex*>(vertices(3*sizeof(Vertex)));
        out[0]={0,0,0xff101010,pointer_x,pointer_y,0};out[1]={0,0,0xffffffff,pointer_x+9,pointer_y+14,0};out[2]={0,0,0xffffffff,pointer_x+1,pointer_y+11,0};
        sceGuDrawArray(GU_TRIANGLES,VertexFormat,3,nullptr,out);
    }
    auto finish_begin=media_stats.enabled?sceKernelGetSystemTimeWide():0;
    sceGuFinish();sceGuSync(0,0);active_list=false;
    if(media_stats.enabled){unsigned elapsed=unsigned(sceKernelGetSystemTimeWide()-finish_begin);media_stats.finish_us+=elapsed;media_stats.max_finish_us=std::max(media_stats.max_finish_us,elapsed);}
    release_retired_textures();
    if(overlay) {
        pspDebugScreenSetOffset(reinterpret_cast<unsigned int>(draw_buffer));
        pspDebugScreenSetTextColor(0xffffffff);pspDebugScreenSetBackColor(0xff181818);
        pspDebugScreenSetXY(footer?20:3,footer?31:10);pspDebugScreenPrintf("%s",overlay);
    }
#if GLORTON_WLAN
    if(link_role>0){
        pspDebugScreenSetOffset(reinterpret_cast<unsigned int>(draw_buffer));
        pspDebugScreenSetTextColor(link_role==1?0xffa0a0ff:0xffffc080);pspDebugScreenSetBackColor(0xff000000);
        pspDebugScreenSetXY(1,16);pspDebugScreenPrintf("P%d",link_role);
    }
#endif
}
void present_psp_frame(void(*before_swap)(void*),void* context){
    int priority=sceKernelGetThreadCurrentPriority();sceKernelChangeThreadPriority(0,0x10);
    sceDisplayWaitVblankStart();observe_latch();if(before_swap)before_swap(context);displayed_buffer=draw_buffer;draw_buffer=sceGuSwapBuffers();has_submitted=true;
    last_present_time=sceKernelGetSystemTimeWide();last_present_vcount=sceDisplayGetVcount();
    sceKernelChangeThreadPriority(0,priority);
}
void hold_psp_frame(){
    int priority=sceKernelGetThreadCurrentPriority();sceKernelChangeThreadPriority(0,0x10);
    sceDisplayWaitVblankStart();observe_latch();last_present_time=sceKernelGetSystemTimeWide();last_present_vcount=sceDisplayGetVcount();
    sceKernelChangeThreadPriority(0,priority);
}

void flush_psp_media_stats(){
    if(!media_stats.enabled)return;
    const auto& s=media_stats;if(FILE* f=std::fopen("RENDER_MEDIA_STATS.JSON","w")){
        std::fprintf(f,"{\"scope\":\"Whole-run elapsed guest draw generation and final GU sync; waits may overlap/preempt, never add to source time; optional SOURCE_PROFILE only\",\"art_cache_bytes\":%u,\"art_page_hits\":%u,\"art_page_misses\":%u,\"art_physical_reads\":%u,\"art_physical_bytes\":%llu,\"art_bypasses\":%u,\"native_art_io\":%u,\"frames\":%u,\"draw_us\":%llu,\"max_draw_us\":%u,\"finish_us\":%llu,\"max_finish_us\":%u,\"bank_read_us\":%llu,\"max_bank_read_us\":%u,\"decode_us\":%llu,\"max_decode_us\":%u,\"cache_misses\":%u,\"decoded_bytes\":%u,\"resident_peak\":%u,\"t4_binds\":%u,\"t8_binds\":%u,\"rgba_binds\":%u}\n",art_bank_cache.bytes(),art_bank_cache.stats.hits,art_bank_cache.stats.misses,art_bank_cache.stats.physical_reads,(unsigned long long)art_bank_cache.stats.physical_bytes,art_bank_cache.stats.bypasses,unsigned(s.native_bank_io),s.frames,(unsigned long long)s.draw_us,s.max_draw_us,(unsigned long long)s.finish_us,s.max_finish_us,(unsigned long long)s.bank_read_us,s.max_bank_read_us,(unsigned long long)s.decode_us,s.max_decode_us,s.misses,s.decoded_bytes,s.resident_peak,s.t4_binds,s.t8_binds,s.rgba_binds);std::fclose(f);
    }
}

void submit_psp_loading(){
    // Original load_message_570 glyphs are embedded so this screen is available
    // before any script, art bank or font initialization. No animated elements.
    if(!active_list){sceGuStart(GU_DIRECT,commands);active_list=true;list_bytes=0;}
    pointer_visible=false;
    sceGuScissor(0,0,Width,Height);sceGuPixelMask(0);sceGuDisable(GU_STENCIL_TEST);
    sceGuClearColor(0xff000000);sceGuClearStencil(0);sceGuClear(GU_COLOR_BUFFER_BIT|GU_STENCIL_BUFFER_BIT);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    static bool uploaded=false;if(!uploaded){sceKernelDcacheWritebackRange(const_cast<std::uint32_t*>(deoriginal::Pixels),sizeof(deoriginal::Pixels));uploaded=true;}
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexImage(0,deoriginal::TextureWidth,deoriginal::TextureHeight,deoriginal::TextureWidth,deoriginal::Pixels);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    constexpr float x=deoriginal::Left,y=deoriginal::Top,w=deoriginal::Width,h=deoriginal::Height;
    Vertex quad[]={{0,0,0xffffffff,x,y,0},{w,0,0xffffffff,x+w,y,0},{0,h,0xffffffff,x,y+h,0},{w,h,0xffffffff,x+w,y+h,0}};
    auto out=static_cast<Vertex*>(vertices(sizeof(quad)));std::memcpy(out,quad,sizeof(quad));sceGuDrawArray(GU_TRIANGLE_STRIP,VertexFormat,4,nullptr,out);
    auto notice=picture_notice_frames;picture_notice_frames=0;submit_psp_frame();picture_notice_frames=notice;
}
void draw_psp_loading(){submit_psp_loading();present_psp_frame();}
void draw_psp_boot_loading(){
    // Same original pixels before GU construction. Use the SDK's framebuffer
    // setup and uncached VRAM mapping; no heap, bank I/O or font provider needed.
    auto pixels=reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(sceGeEdramGetAddr())|0x40000000u);
    pspDebugScreenInitEx(pixels,PSP_DISPLAY_PIXEL_FORMAT_8888,1);
    std::fill_n(pixels,Stride*Height,0xff000000u);
    for(int y=0;y<deoriginal::Height;++y)std::copy_n(deoriginal::Pixels+y*deoriginal::TextureWidth,deoriginal::Width,pixels+(deoriginal::Top+y)*Stride+deoriginal::Left);
    sceKernelDcacheWritebackAll();
}
bool confirm_psp_frame_visible(bool wait_vblank){
    // This renderer selects NEXTHSYNC (IMMEDIATE). Loading must scan out before
    // initialization, so its default waits a refresh. Resume can inspect the
    // newly configured CURRENT buffer directly without an extra input window.
    if(wait_vblank)sceDisplayWaitVblankStart();void* shown=nullptr;int stride=0,format=0;
    if(sceDisplayGetFrameBuf(&shown,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)<0)return false;
    auto expected=reinterpret_cast<std::uintptr_t>(sceGeEdramGetAddr())+reinterpret_cast<std::uintptr_t>(displayed_buffer);
    return (reinterpret_cast<std::uintptr_t>(shown)&0x0fffffffu)==(expected&0x0fffffffu)&&stride==Stride;
}

void finish_psp_frame(const char* overlay,bool footer){submit_psp_frame(overlay,footer);present_psp_frame();}

#if GLORTON_WLAN
void set_psp_link_role(int role){link_role=role;}
#endif
void draw_psp_message(const char* message,void(*before_swap)(void*),void* context){
    sceGuStart(GU_DIRECT,commands);active_list=true;list_bytes=0;pointer_visible=false;
    sceGuScissor(0,0,Width,Height);sceGuPixelMask(0);sceGuDisable(GU_STENCIL_TEST);
    sceGuClearColor(0xff000000);sceGuClearStencil(0);sceGuClear(GU_COLOR_BUFFER_BIT|GU_STENCIL_BUFFER_BIT);
    // Submit menu text through GE, like game text. CPU framebuffer writes from
    // pspDebugScreenPrintf were not visible in the emulator's captured display.
    draw_native_label(message,24,26,14);
    submit_psp_frame();present_psp_frame(before_swap,context);
}
bool dump_psp_frame(const char* path) {
    // Isolated graphical fixtures request a real GE transfer from VRAM to RAM.
    // This is never enabled in ordinary gameplay or speed measurements.
    // A full 512x272 allocation can fail in a fragmented late-game heap even
    // when rendering is healthy. Read bounded bands without evicting textures.
    constexpr int Rows=8,Bytes=Stride*Rows*4;
    auto pixels=static_cast<std::uint32_t*>(memalign(16,Bytes));
    if(!pixels)return false;
    auto source=static_cast<unsigned char*>(sceGeEdramGetAddr())+reinterpret_cast<std::uintptr_t>(displayed_buffer);
    FILE* file=std::fopen(path,"wb");bool ok=file!=nullptr;
    if(file){
        for(int y=0;y<Height&&ok;y+=Rows){
            const int rows=std::min(Rows,Height-y);
            sceKernelDcacheWritebackInvalidateRange(pixels,Bytes);
            sceGuStart(GU_DIRECT,commands);
            sceGuCopyImage(GU_PSM_8888,0,y,Width,rows,Stride,source,0,0,Stride,pixels);sceGuTexSync();
            sceGuFinish();sceGuSync(0,0);sceKernelDcacheInvalidateRange(pixels,Bytes);
            for(int row=0;row<rows;++row)if(std::fwrite(pixels+row*Stride,4,Width,file)!=Width)ok=false;
        }
        if(std::fclose(file))ok=false;
    }
    std::free(pixels);return ok;
}
