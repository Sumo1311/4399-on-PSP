// WIP graphical probe: original resources and source-driven drawing tree.
#include "quickjs.h"
#include "render_capture.hpp"
#include "render_batch.hpp"
#include "flash_math.h"
#include "render_psp.hpp"
#include "device_font.hpp"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_sprite.h"
#include "gameswf/gameswf_character.h"
#include "gameswf/gameswf_movie_def.h"
#include "gameswf/gameswf_text.h"
#include "gameswf/gameswf_stream.h"
#include "gameswf/gameswf_filters.h"
#include "base/tu_file.h"
#include "base/tu_timer.h"
#include <cstdio>
#include <map>
#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cmath>
#include <array>
#include <stdexcept>
#include <functional>
#include <malloc.h>
#include <new>
#include <pspkernel.h>
#include "corpse_visibility.hpp"
#ifndef DE_BATCH_RENDER_SYNC
#define DE_BATCH_RENDER_SYNC 0
#endif
#ifndef DE_DIRECT_BAKED_CAPTURE
#define DE_DIRECT_BAKED_CAPTURE 0
#endif
using namespace gameswf;
static gc_ptr<player> drawing_player;
static gc_ptr<root> drawing_root;
static gameswf::render_handler* drawing_renderer;
static psp60::Recorder* capture_recorder=nullptr;
// Capture belongs to the source worker; presentation may still use an older
// immutable scene. Color-filter recording must use that scene's chosen mode.
static bool capture_picture_active=false,capture_smooth_picture=true;
struct CapturePictureScope {
    bool active=capture_picture_active,smooth=capture_smooth_picture;
    explicit CapturePictureScope(bool value){capture_picture_active=true;capture_smooth_picture=value;}
    ~CapturePictureScope(){capture_picture_active=active;capture_smooth_picture=smooth;}
};
static void no_lock(){}
static void (*resource_lock)()=no_lock;
static void (*resource_unlock)()=no_lock;
static gameswf::render_handler* paint_renderer(){return capture_recorder?static_cast<gameswf::render_handler*>(capture_recorder):drawing_renderer;}
static std::map<unsigned,gc_ptr<character>> shape_cache;
struct TextState {gc_ptr<character> object;std::string text;bool html=false,initialized=false;unsigned used=0;};
static std::map<unsigned,TextState> text_cache;
static unsigned drawing_frame=0;
static FILE* graphics_log;
static FILE* paint_timing;
static bool effect_census=false;
extern "C" void glorton_graphics_trace(unsigned f,unsigned draws,unsigned largest,unsigned bytes){if(graphics_log){std::fprintf(graphics_log,"DRAW %u %u %u %u\n",f,draws,largest,bytes);std::fflush(graphics_log);}}
extern "C" void glorton_cache_trace(unsigned frame,unsigned hits,unsigned misses,unsigned decoded,unsigned resident){if(graphics_log)std::fprintf(graphics_log,"CACHE %u hits=%u misses=%u decoded=%u resident=%u\n",frame,hits,misses,decoded,resident);}
extern "C" void de_art_bank_trace(unsigned resident,unsigned budget,unsigned packed){if(graphics_log)std::fprintf(graphics_log,"ART_BANK resident=%u budget=%u packed=%u\n",resident,budget,packed);}
extern "C" void de_texture_arena_trace(unsigned frame,unsigned capacity,unsigned used,unsigned resident){if(graphics_log)std::fprintf(graphics_log,"ARENA %u capacity=%u used=%u resident_raw=%u\n",frame,capacity,used,resident);}
extern "C" void glorton_gpu_trace(unsigned,unsigned,unsigned,unsigned){}
extern "C" void glorton_filter_trace(unsigned f,unsigned hits,unsigned misses,unsigned cache){if(graphics_log){auto m=mallinfo();std::fprintf(graphics_log,"FILTER %u hits=%u misses=%u cache=%u heap_used=%d heap_free=%d\n",f,hits,misses,cache,m.uordblks,m.fordblks);}}
extern "C" void glorton_filter_detail(unsigned id,const char* name,int w,int h,unsigned ops){if(graphics_log){auto m=mallinfo();std::fprintf(graphics_log,"FILTER_BUILD %u id=%u name=%s size=%dx%d ops=%u heap_used=%d heap_free=%d\n",drawing_frame,id,name,w,h,ops,m.uordblks,m.fordblks);std::fflush(graphics_log);}}
extern "C" void glorton_prefix_trace(unsigned reused,unsigned total){if(graphics_log&&drawing_frame%60==0)std::fprintf(graphics_log,"PREFIX %u reused=%u operations=%u\n",drawing_frame,reused,total);}
extern "C" void glorton_pool_trace(unsigned,unsigned,unsigned,unsigned,unsigned){}
extern "C" void glorton_graphics_failure(const char* why){if(graphics_log){std::fprintf(graphics_log,"FAIL %s\n",why);std::fflush(graphics_log);}}
extern "C" void glorton_codec_recovery(const char*,int){}
static tu_file* open_drawing_file(const char* path){return new tu_file(path,"rb");}
static void drawing_log(bool error,const char* text){if(graphics_log){std::fprintf(graphics_log,"%s%s",error?"ERROR ":"",text);std::fflush(graphics_log);}}
static unsigned get_uint(JSContext* ctx,JSValueConst o,const char* key){JSValue v=JS_GetPropertyStr(ctx,o,key);uint32_t n=0;JS_ToUint32(ctx,&n,v);JS_FreeValue(ctx,v);return n;}
static std::string get_string(JSContext* ctx,JSValueConst o,const char* key){JSValue v=JS_GetPropertyStr(ctx,o,key);const char* s=JS_ToCString(ctx,v);std::string result=s?s:"";JS_FreeCString(ctx,s);JS_FreeValue(ctx,v);return result;}
#ifdef DE_NATIVE_PAINT
namespace {
enum Prop {Visible,Children,Symbol,Definition,Kind,Matrix,Color,Uid,Text,Html,Blend,BlendMode,Filters,RawFilters,Graphics,Records,ClipDepth,TimelineDepth,Length,MatrixVersion,ColorVersion,FiltersVersion,FiltersOverride,Placement,Ratio,Game,Baddies,Health,Alive,Landed,DeadCount,Shadow,PropCount};
const char* property_names[]={"_visible","_children","_symbol","_definition","kind","_matrix","_color","_uid","_text","_html","_blend","blendMode","_scriptFilters","_filters","graphics","records","_clipDepth","_timelineDepth","length","_matrixVersion","_colorVersion","_filtersVersion","_filtersOverride","_placement","_ratio","myFightingGame","activeBaddies","health","alive","landed","deadCount","shadow"};
std::array<JSAtom,PropCount> property_atoms{};
bool atoms_ready=false;
JSContext* drawing_context=nullptr;
struct Value {
    JSContext* ctx;JSValue value;
    Value(JSContext* c,JSValue v):ctx(c),value(v){}
    ~Value(){JS_FreeValue(ctx,value);}
    Value(const Value&)=delete;Value& operator=(const Value&)=delete;
    operator JSValueConst()const{return value;}
};
Value prop(JSContext* c,JSValueConst o,Prop p){return Value(c,JS_GetProperty(c,o,property_atoms[p]));}
int number(JSContext* c,JSValueConst o,Prop p){JSValue borrowed;if(JS_TryOwnDataPropertyCachedBorrowed(c,o,property_atoms[p],&borrowed)&&JS_IsNumber(borrowed)){int32_t n=0;JS_ToInt32(c,&n,borrowed);return n;}auto v=prop(c,o,p);int32_t n=0;JS_ToInt32(c,&n,v);return n;}
unsigned length(JSContext* c,JSValueConst o){return unsigned(number(c,o,Length));}
std::string string_value(JSContext* c,JSValueConst value){const char* p=JS_ToCString(c,value);std::string text=p?p:"";JS_FreeCString(c,p);return text;}
struct World {double previous_m[6]={1,0,0,1,0,0};double m[6]={1,0,0,1,0,0};double c[8]={256,256,256,256,0,0,0,0};unsigned matrix_serial=0,color_serial=0;};
struct NodeCache {
    World world;
    double local_matrix[6]{},local_color[8]{},previous_local[6]{};
    unsigned history_frame=~0u;
    bool preserve_death_alpha=false,composed_death_alpha=false;
    unsigned local_matrix_version=0,local_color_version=0,parent_matrix_serial=~0u,parent_color_serial=~0u,used=0;
    int symbol=-1,kind=-1;
    unsigned filter_version=~0u,placement=~0u;
    std::shared_ptr<const filter_list> filters;
};
std::map<unsigned,NodeCache> node_cache;
unsigned world_serial=0;
unsigned native_nodes,native_leaves,native_filters,native_blends,native_graphics,native_fast_colors;
#ifdef DE_NATIVE_EFFECTS
struct PaintGroup final:character {
    std::function<void()> paint;
    PaintGroup(character* parent,int id):character(drawing_player,parent,id){}
    void display() override {paint();}
};
std::map<std::string,std::shared_ptr<const filter_list>> authored_filters;
const std::shared_ptr<const filter_list>& node_filters(JSContext* ctx,JSValueConst node,NodeCache& cache){
    unsigned version=unsigned(number(ctx,node,FiltersVersion)),placement=unsigned(number(ctx,node,Placement));
    if(cache.filter_version==version&&cache.placement==placement)return cache.filters;
    cache.filter_version=version;cache.placement=placement;
    auto override_value=prop(ctx,node,FiltersOverride);
    if(JS_ToBool(ctx,override_value)){
        auto values=prop(ctx,node,Filters);unsigned count=length(ctx,values);auto result=std::make_shared<filter_list>();
        for(unsigned i=0;i<count;i++){
            Value f(ctx,JS_GetPropertyUint32(ctx,values,i));Value m(ctx,JS_GetPropertyStr(ctx,f,"matrix"));
            if(JS_IsUndefined(m)||length(ctx,m)!=20)throw std::runtime_error("Unsupported script filter");
            filter_descriptor descriptor;descriptor.type=6;
            for(unsigned j=0;j<20;j++){Value value(ctx,JS_GetPropertyUint32(ctx,m,j));double number=0;JS_ToFloat64(ctx,&number,value);descriptor.matrix[j]=float(number);}
            result->push_back(descriptor);
        }
        cache.filters=result;
    }else{
        auto raw=prop(ctx,node,RawFilters);std::string hex=JS_IsUndefined(raw)||JS_IsNull(raw)?"":string_value(ctx,raw);
        auto found=authored_filters.find(hex);
        if(found==authored_filters.end()){
            std::shared_ptr<const filter_list> result=std::make_shared<filter_list>();
            if(!hex.empty()){
                std::vector<unsigned char> bytes;bytes.reserve(hex.size()/2);
                for(unsigned i=0;i<hex.size();i+=2)bytes.push_back(unsigned(std::strtoul(hex.substr(i,2).c_str(),nullptr,16)));
                tu_file file(tu_file::memory_buffer,int(bytes.size()),bytes.data());stream reader(&file);result=read_filter_list(&reader);
            }
            found=authored_filters.emplace(hex,result).first;
        }
        cache.filters=found->second;
    }
    return cache.filters;
}
#endif
NodeCache& cached_node(JSContext* ctx,JSValueConst node){
    unsigned uid=unsigned(number(ctx,node,Uid));auto found=node_cache.find(uid);
    if(found!=node_cache.end()){found->second.used=drawing_frame;return found->second;}
    auto& result=node_cache[uid];result.used=drawing_frame;result.symbol=number(ctx,node,Symbol);
    auto definition=prop(ctx,node,Definition);if(!JS_IsNull(definition)&&!JS_IsUndefined(definition))result.kind=number(ctx,definition,Kind);
    return result;
}
const World& compose(JSContext* ctx,JSValueConst node,NodeCache& cache,const World& parent){
    unsigned matrix_version=unsigned(number(ctx,node,MatrixVersion)),color_version=unsigned(number(ctx,node,ColorVersion));
    bool prior=cache.history_frame!=~0u&&cache.history_frame+1==drawing_frame;
    if(cache.history_frame!=drawing_frame){std::copy_n(cache.local_matrix,6,cache.previous_local);cache.history_frame=drawing_frame;}
    bool matrix_changed=matrix_version!=cache.local_matrix_version,color_changed=color_version!=cache.local_color_version||cache.preserve_death_alpha!=cache.composed_death_alpha;
    if(matrix_changed){auto values=prop(ctx,node,Matrix);for(unsigned i=0;i<6;i++){Value v(ctx,JS_GetPropertyUint32(ctx,values,i));JS_ToFloat64(ctx,&cache.local_matrix[i],v);}cache.local_matrix_version=matrix_version;}
    if(color_changed){auto values=prop(ctx,node,Color);for(unsigned i=0;i<8;i++){Value v(ctx,JS_GetPropertyUint32(ctx,values,i));JS_ToFloat64(ctx,&cache.local_color[i],v);}cache.local_color_version=color_version;}
    auto& w=cache.world;auto m=cache.local_matrix,c=cache.local_color;
    if(!prior)std::copy_n(cache.local_matrix,6,cache.previous_local);
    if(std::fabs(m[4]-cache.previous_local[4])>200||std::fabs(m[5]-cache.previous_local[5])>200)std::copy_n(m,6,cache.previous_local);
    double determinant=m[0]*m[3]-m[1]*m[2],old_determinant=cache.previous_local[0]*cache.previous_local[3]-cache.previous_local[1]*cache.previous_local[2];
    double alignment=0;for(unsigned i=0;i<4;++i)alignment+=m[i]*cache.previous_local[i];
    if(determinant*old_determinant<=0||alignment<=0)std::copy_n(m,4,cache.previous_local);
    de_matrix_values(parent.previous_m,cache.previous_local,w.previous_m);
    if(matrix_changed||cache.parent_matrix_serial!=parent.matrix_serial){
        de_matrix_values(parent.m,m,w.m);
        cache.parent_matrix_serial=parent.matrix_serial;w.matrix_serial=++world_serial;
    }
    if(color_changed||cache.parent_color_serial!=parent.color_serial){
        for(unsigned i=0;i<4;i++){w.c[i]=parent.c[i]*c[i]/256;w.c[i+4]=parent.c[i]*c[i+4]/256+parent.c[i+4];}
        if(cache.preserve_death_alpha){w.c[3]=parent.c[3];w.c[7]=parent.c[7];}
        cache.composed_death_alpha=cache.preserve_death_alpha;
        cache.parent_color_serial=parent.color_serial;w.color_serial=++world_serial;
    }
    return w;
}
void display_native_leaf(const gc_ptr<character>& object,unsigned ratio,const World& world){
    matrix m;m.m_[0][0]=world.m[0];m.m_[1][0]=world.m[1];m.m_[0][1]=world.m[2];m.m_[1][1]=world.m[3];
    m.m_[0][2]=float(world.m[4])*20;m.m_[1][2]=float(world.m[5])*20;
    cxform c;for(unsigned i=0;i<4;i++){c.m_[i][0]=float(world.c[i])/256;c.m_[i][1]=float(world.c[i+4]);}
    if(capture_recorder){
        matrix previous;previous.m_[0][0]=world.previous_m[0];previous.m_[1][0]=world.previous_m[1];previous.m_[0][1]=world.previous_m[2];previous.m_[1][1]=world.previous_m[3];previous.m_[0][2]=float(world.previous_m[4])*20;previous.m_[1][2]=float(world.previous_m[5])*20;
        capture_recorder->set_motion(m,previous);
    }
    object->set_matrix(m);object->set_cxform(c);object->set_ratio(float(ratio)/65535);object->display();++native_leaves;
}
void draw_native_leaf(JSContext* ctx,JSValueConst node,unsigned id,const World& world,movie_definition_sub* definitions,character* parent){
    gc_ptr<character> object;auto definition_value=prop(ctx,node,Definition);int kind=number(ctx,definition_value,Kind);
    if(kind==37){
        unsigned uid=unsigned(number(ctx,node,Uid));auto& item=text_cache[uid];
        if(!item.object){auto d=definitions->get_character_def(id);if(!d)throw std::runtime_error("Missing native text definition");item.object=d->create_character_instance(parent,id);}
        auto html_value=prop(ctx,node,Html);bool html=!JS_IsNull(html_value)&&!JS_IsUndefined(html_value);
        auto value=prop(ctx,node,html?Html:Text);std::string text=string_value(ctx,value);
        if(!item.initialized||text!=item.text||html!=item.html){item.object->set_member(html?"htmlText":"text",as_value(text.c_str()));item.text=text;item.html=html;item.initialized=true;}
        item.used=drawing_frame;object=item.object;
    }else{
        auto& cached=shape_cache[id];if(!cached){auto d=definitions->get_character_def(id);if(!d)throw std::runtime_error("Missing native drawing definition");cached=d->create_character_instance(parent,id);}object=cached;
    }
    display_native_leaf(object,get_uint(ctx,node,"_ratio"),world);
}
#include "render_mirror.inc"
void native_tree(JSContext* ctx,JSValueConst node,const World& parent_world,bool as_mask,movie_definition_sub* definitions,character* parent);
void native_contents(JSContext* ctx,JSValueConst node,JSValueConst children,unsigned count,bool container,bool native_button,int symbol,const World& world,bool as_mask,movie_definition_sub* definitions,character* parent){
    if(container&&!native_button){
        std::vector<unsigned> ends;
        for(unsigned i=0;i<count;i++){
            Value child(ctx,JS_GetPropertyUint32(ctx,children,i));unsigned depth=unsigned(number(ctx,child,TimelineDepth));
            while(!ends.empty()&&depth>ends.back()){paint_renderer()->disable_mask();ends.pop_back();}
            unsigned clip=unsigned(number(ctx,child,ClipDepth));
            if(clip){paint_renderer()->begin_submit_mask();native_tree(ctx,child,world,true,definitions,parent);paint_renderer()->end_submit_mask();ends.push_back(clip);}
            else native_tree(ctx,child,world,as_mask,definitions,parent);
        }
        while(!ends.empty()){paint_renderer()->disable_mask();ends.pop_back();}
    }else if(symbol>=0)draw_native_leaf(ctx,node,unsigned(symbol),world,definitions,parent);
}
void native_tree(JSContext* ctx,JSValueConst node,const World& parent_world,bool as_mask,movie_definition_sub* definitions,character* parent){
    ++native_nodes;auto visible=prop(ctx,node,Visible);if(!as_mask&&JS_IsBool(visible)&&!JS_ToBool(ctx,visible))return;
    auto children=prop(ctx,node,Children);bool container=!JS_IsUndefined(children)&&!JS_IsNull(children);unsigned count=container?length(ctx,children):0;
    auto& cached=cached_node(ctx,node);int symbol=cached.symbol;bool native_button=cached.kind==34;
    unsigned visual_uid=unsigned(number(ctx,node,Uid));
    if(!as_mask&&corpse_hidden(visual_uid))return;
    cached.preserve_death_alpha=!as_mask&&corpse_kept_opaque(visual_uid);
    if(container&&!count&&!native_button)return;
    const World& world=compose(ctx,node,cached,parent_world);if(world.c[3]<=0&&!as_mask)return;
    if(effect_census){
    if(number(ctx,node,Blend)>1)++native_blends;
    auto graphics=prop(ctx,node,Graphics);if(!JS_IsUndefined(graphics)&&!JS_IsNull(graphics)){auto records=prop(ctx,graphics,Records);if(length(ctx,records))++native_graphics;}
    }
#ifdef DE_NATIVE_EFFECTS
    const auto& filters=node_filters(ctx,node,cached);
    if(filters&&!filters->empty()&&!as_mask){
        // Optional hardware-color experiment. Affine colors are applied to
        // leaves before compositing; translucent/clamped edges can differ from
        // the original group filter. Keep the original CPU path selectable.
        if((capture_picture_active?capture_smooth_picture:fast_color_filters())&&filters->size()==1&&(*filters)[0].type==6){
            const auto& f=(*filters)[0];bool diagonal=f.matrix[15]==0&&f.matrix[16]==0&&f.matrix[17]==0&&f.matrix[18]==1&&f.matrix[19]==0;
            for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
                if((row!=col&&f.matrix[row*5+col]!=0)||!std::isfinite(f.matrix[row*5+col]))diagonal=false;
            for(unsigned row=0;row<3;++row)if(!std::isfinite(f.matrix[row*5+4]))diagonal=false;
            if(diagonal){
                World direct=world;
                for(unsigned row=0;row<3;++row){direct.c[row]=world.c[row]*f.matrix[row*5+row];direct.c[row+4]=world.c[row+4]+world.c[row]*f.matrix[row*5+4]/256.;}
                direct.color_serial=++world_serial;++native_fast_colors;
                native_contents(ctx,node,children,count,container,native_button,symbol,direct,as_mask,definitions,parent);return;
            }
        }
        gc_ptr<PaintGroup> group=new PaintGroup(parent,symbol);matrix m;cxform c;
        m.m_[0][0]=world.m[0];m.m_[1][0]=world.m[1];m.m_[0][1]=world.m[2];m.m_[1][1]=world.m[3];m.m_[0][2]=float(world.m[4])*20;m.m_[1][2]=float(world.m[5])*20;
        for(unsigned i=0;i<4;i++){c.m_[i][0]=float(world.c[i])/256;c.m_[i][1]=float(world.c[i+4]);}
        group->set_matrix(m);group->set_cxform(c);group->m_filters=filters;
        group->paint=[&](){World neutral=world;for(unsigned i=0;i<4;i++){neutral.c[i]=256;neutral.c[i+4]=0;}neutral.color_serial=0;native_contents(ctx,node,children,count,container,native_button,symbol,neutral,as_mask,definitions,parent);};
        if(paint_renderer()->draw_filtered(group))return;
        ++native_filters;
    }
#else
    if(effect_census){
    auto raw_filters=prop(ctx,node,RawFilters);if(!JS_IsUndefined(raw_filters)&&!JS_IsNull(raw_filters)){size_t n=0;const char* s=JS_ToCStringLen(ctx,&n,raw_filters);if(n)++native_filters;JS_FreeCString(ctx,s);}
    auto filters=prop(ctx,node,Filters);if(!JS_IsUndefined(filters)&&!JS_IsNull(filters)&&length(ctx,filters))++native_filters;
    }
#endif
    native_contents(ctx,node,children,count,container,native_button,symbol,world,as_mask,definitions,parent);
}
int native_paint_frame(JSContext* ctx){
    auto started=sceKernelGetSystemTimeWide();
    drawing_context=ctx;
    if(!atoms_ready){for(unsigned i=0;i<PropCount;i++)property_atoms[i]=JS_NewAtom(ctx,property_names[i]);atoms_ready=true;}
    Value global(ctx,JS_GetGlobalObject(ctx));Value runtime(ctx,JS_GetPropertyStr(ctx,global,"FlashRuntime"));Value root_node(ctx,JS_GetPropertyStr(ctx,runtime,"root"));
    uint32_t mirror_root=0;
    if(mirror_enabled){Value flush(ctx,JS_GetPropertyStr(ctx,runtime,"flushRenderMirror"));Value result(ctx,JS_Call(ctx,flush,runtime,0,nullptr));if(JS_IsException(result)||JS_ToUint32(ctx,&mirror_root,result))return 5;}
    update_corpse_visibility(ctx,root_node);
    auto built=sceKernelGetSystemTimeWide();resource_lock();auto acquired=paint_timing?sceKernelGetSystemTimeWide():0;prepare_psp_tick(drawing_frame);auto prepared=paint_timing?sceKernelGetSystemTimeWide():0;resource_unlock();
    paint_renderer()->begin_display(rgba(255,255,255,255),53,0,374,272,0,11000,0,8000);
    auto root=drawing_root->get_root_movie();auto definitions=static_cast<movie_definition_sub*>(root->get_movie_definition());
    native_nodes=native_leaves=native_filters=native_blends=native_graphics=native_fast_colors=0;
    auto walking=paint_timing?sceKernelGetSystemTimeWide():0;
    try{if(mirror_enabled)mirror_tree(mirror_root,FastWorld{},false,definitions,root);else native_tree(ctx,root_node,World{},false,definitions,root);}catch(const std::exception& e){if(graphics_log){std::fprintf(graphics_log,"NATIVE PAINT %s\n",e.what());std::fflush(graphics_log);}return 5;}
    auto drawn=sceKernelGetSystemTimeWide();paint_renderer()->end_display();auto ended=sceKernelGetSystemTimeWide();if(capture_recorder)capture_recorder->finish();else finish_psp_frame();auto swapped=sceKernelGetSystemTimeWide();
    if(paint_timing){std::fprintf(paint_timing,"%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",drawing_frame+1,native_leaves,built-started,drawn-built,ended-drawn,swapped-ended,acquired-built,prepared-acquired,walking-prepared,drawn-walking);if(drawing_frame%120==0)std::fflush(paint_timing);}
    if(!capture_recorder&&graphics_log&&(drawing_frame==1099||drawing_frame==1699))dump_psp_frame(drawing_frame==1099?"STAGE.RGBA":"ACTIONS.RGBA");
    if(drawing_frame%120==0&&graphics_log){std::fprintf(graphics_log,"NATIVE %u nodes=%u leaves=%u counted_filters=%u blends=%u graphics=%u fast_color_groups=%u census=%u\n",drawing_frame,native_nodes,native_leaves,native_filters,native_blends,native_graphics,native_fast_colors,unsigned(effect_census));std::fflush(graphics_log);}
    for(auto it=text_cache.begin();it!=text_cache.end();)if(drawing_frame>it->second.used+120)it=text_cache.erase(it);else ++it;
    if(drawing_frame%120==0)for(auto it=node_cache.begin();it!=node_cache.end();)if(drawing_frame>it->second.used+120)it=node_cache.erase(it);else ++it;
    if(mirror_enabled&&drawing_frame%120==0){mark_mirror_tree(mirror_root);if(++mirror_pointer_epoch==0)++mirror_pointer_epoch;for(auto it=mirror_nodes.begin();it!=mirror_nodes.end();)if(drawing_frame>it->second.used+120)it=mirror_nodes.erase(it);else ++it;}
    ++drawing_frame;return 0;
}
}
#endif
static bool startup_loading=false;
extern "C" void de_render_startup_loading(){startup_loading=true;}
extern "C" int de_render_init(){
    std::set_new_handler([]{resource_lock();bool released=release_psp_texture();resource_unlock();if(!released)throw std::bad_alloc();});
    bool diagnostics=true;
#ifndef DE_RENDER_PROBE
    diagnostics=false;
    for(const char* name:{"GRAPHICS.QA","MIRROR_CHECK.QA"})if(FILE* marker=std::fopen(name,"rb")){diagnostics=true;std::fclose(marker);}
#endif
    if(FILE* marker=std::fopen("GRAPHICS.QA","rb")){effect_census=true;std::fclose(marker);}
    graphics_log=diagnostics?std::fopen("GRAPHICS.LOG","w"):nullptr;paint_timing=diagnostics?std::fopen("PAINT.CSV","w"):nullptr;if(paint_timing)std::fprintf(paint_timing,"frame,packets,js_build_us,native_draw_us,renderer_end_us,sync_swap_us,resource_wait_us,prepare_us,begin_us,mirror_walk_us\n");tu_timer::init_timer();
    drawing_renderer=make_psp_renderer();drawing_renderer->open();set_psp_quality("HIGH");set_render_handler(drawing_renderer);
    set_glyph_provider(make_device_font_provider());set_psp_port_font(make_device_font_provider(drawing_renderer),drawing_renderer);
    // Only the current single-player frontend enables this. Legacy/WLAN
    // frontends retain their initialization contract and synchronization.
    if(startup_loading){draw_psp_loading();if(!confirm_psp_frame_visible())return 1;if(FILE* marker=std::fopen("LOAD_CAPTURE.QA","rb")){std::fclose(marker);dump_psp_frame("BOOT_GE_LOADING.RGBA");}}
    register_file_opener_callback(open_drawing_file);register_log_callback(drawing_log);
    drawing_player=new player;drawing_player->set_separate_thread(false);drawing_root=drawing_player->load_file("graphics.swf");
    return drawing_root?0:1;
}
extern "C" int de_render_frame(JSContext* ctx){
#ifdef DE_NATIVE_PAINT
    return native_paint_frame(ctx);
#else
    auto started=sceKernelGetSystemTimeWide();
    JSValue global=JS_GetGlobalObject(ctx),paint=JS_GetPropertyStr(ctx,global,"PaintRuntime"),build=JS_GetPropertyStr(ctx,paint,"build");
    JSValue countValue=JS_Call(ctx,build,paint,0,nullptr);JS_FreeValue(ctx,build);
    if(JS_IsException(countValue)){JS_FreeValue(ctx,countValue);JS_FreeValue(ctx,paint);JS_FreeValue(ctx,global);return 1;}
    uint32_t count=0;JS_ToUint32(ctx,&count,countValue);JS_FreeValue(ctx,countValue);
    JSValue buffer=JS_GetPropertyStr(ctx,paint,"buffer"),texts=JS_GetPropertyStr(ctx,paint,"textNodes");size_t size=0;
    auto packets=reinterpret_cast<const float*>(JS_GetArrayBuffer(ctx,&size,buffer));
    if(!packets||count>size/(24*sizeof(float)))return 2;
    auto built=sceKernelGetSystemTimeWide();
    prepare_psp_tick(drawing_frame);
    drawing_renderer->begin_display(rgba(255,255,255,255),53,0,374,272,0,11000,0,8000);
    auto parent=drawing_root->get_root_movie();auto definition=static_cast<movie_definition_sub*>(parent->get_movie_definition());
    for(unsigned i=0;i<count;i++){
        const float* p=packets+i*24;unsigned op=unsigned(p[0]);
        if(op==2){drawing_renderer->begin_submit_mask();continue;}if(op==3){drawing_renderer->end_submit_mask();continue;}if(op==4){drawing_renderer->disable_mask();continue;}
        if(op!=1)continue;
        unsigned id=unsigned(p[1]);gc_ptr<character> object;
        if(p[3]>=0){
            JSValue node=JS_GetPropertyUint32(ctx,texts,unsigned(p[3]));unsigned uid=get_uint(ctx,node,"_uid");auto& item=text_cache[uid];
            if(!item.object){auto d=definition->get_character_def(id);if(!d){JS_FreeValue(ctx,node);return 3;}item.object=d->create_character_instance(parent,id);}
            JSValue htmlValue=JS_GetPropertyStr(ctx,node,"_html");bool html=!JS_IsNull(htmlValue)&&!JS_IsUndefined(htmlValue);JS_FreeValue(ctx,htmlValue);
            std::string text=get_string(ctx,node,html?"_html":"_text");
            if(!item.initialized||text!=item.text||html!=item.html){item.object->set_member(html?"htmlText":"text",as_value(text.c_str()));item.text=text;item.html=html;item.initialized=true;}
            item.used=drawing_frame;object=item.object;JS_FreeValue(ctx,node);
        }else{
            auto& cached=shape_cache[id];if(!cached){auto d=definition->get_character_def(id);if(!d)return 4;cached=d->create_character_instance(parent,id);}object=cached;
        }
        matrix m;m.m_[0][0]=p[4];m.m_[1][0]=p[5];m.m_[0][1]=p[6];m.m_[1][1]=p[7];m.m_[0][2]=p[8]*20;m.m_[1][2]=p[9]*20;
        cxform color;for(unsigned j=0;j<4;j++){color.m_[j][0]=p[10+j]/256;color.m_[j][1]=p[14+j];}
        object->set_matrix(m);object->set_cxform(color);object->set_ratio(p[2]/65535);object->display();
    }
    auto drawn=sceKernelGetSystemTimeWide();drawing_renderer->end_display();auto ended=sceKernelGetSystemTimeWide();finish_psp_frame();auto swapped=sceKernelGetSystemTimeWide();
    if(paint_timing){std::fprintf(paint_timing,"%u,%u,%llu,%llu,%llu,%llu\n",drawing_frame+1,count,built-started,drawn-built,ended-drawn,swapped-ended);if(drawing_frame%120==0)std::fflush(paint_timing);}
    if(graphics_log&&(drawing_frame==1099||drawing_frame==1699))dump_psp_frame(drawing_frame==1099?"STAGE.RGBA":"ACTIONS.RGBA");
    if(drawing_frame%120==0&&graphics_log){std::fprintf(graphics_log,"PACKETS %u %u unqualified_filters=%u blends=%u graphics=%u\n",drawing_frame,count,get_uint(ctx,paint,"unqualifiedFilters"),get_uint(ctx,paint,"unqualifiedBlends"),get_uint(ctx,paint,"unqualifiedGraphics"));std::fflush(graphics_log);}
    for(auto it=text_cache.begin();it!=text_cache.end();)if(drawing_frame>it->second.used+120)it=text_cache.erase(it);else ++it;
    ++drawing_frame;JS_FreeValue(ctx,texts);JS_FreeValue(ctx,buffer);JS_FreeValue(ctx,paint);JS_FreeValue(ctx,global);return 0;
#endif
}
extern "C" int de_render_capture(const char* path){return dump_psp_frame(path)?0:1;}
extern "C" void de_render_shutdown(){
#ifdef DE_NATIVE_PAINT
    if(atoms_ready&&drawing_context)for(auto atom:property_atoms)JS_FreeAtom(drawing_context,atom);
    atoms_ready=false;drawing_context=nullptr;node_cache.clear();mirror_nodes.clear();corpse_nodes.clear();
#ifdef DE_NATIVE_EFFECTS
    authored_filters.clear();
#endif
#endif
    text_cache.clear();shape_cache.clear();drawing_root=nullptr;drawing_player=nullptr;if(graphics_log)std::fclose(graphics_log);if(paint_timing)std::fclose(paint_timing);graphics_log=paint_timing=nullptr;
}

void de_enable_capture(void(*lock)(),void(*unlock)()){
    resource_lock=lock;resource_unlock=unlock;
    capture_recorder=new psp60::Recorder(drawing_renderer,lock,unlock);
    gameswf::set_render_handler(capture_recorder);
}
void de_bind_render_mirror(JSContext* ctx){
    if(FILE* f=std::fopen("LEGACY_RENDER.QA","r")){std::fclose(f);return;}
    drawing_context=ctx;if(!atoms_ready){for(unsigned i=0;i<PropCount;i++)property_atoms[i]=JS_NewAtom(ctx,property_names[i]);atoms_ready=true;}
    Value global(ctx,JS_GetGlobalObject(ctx));JSValue update=JS_NewCFunction(ctx,mirror_update,"nativeRenderUpdate",2);
#if DE_BATCH_RENDER_SYNC
    derenderbatch::bind<mirror_update>(ctx,update);
#endif
    JS_SetPropertyStr(ctx,global,"nativeRenderUpdate",update);mirror_enabled=true;
    if(FILE* f=std::fopen("MIRROR_CHECK.QA","r")){mirror_check=true;std::fclose(f);}
}
int de_capture_source(JSContext* ctx,psp60::Frame& frame,bool smooth_picture){
    CapturePictureScope picture(smooth_picture);
    capture_recorder->record(frame);int result=native_paint_frame(ctx);capture_recorder->finish();
    if(!result&&mirror_check){
        psp60::Frame reference;--drawing_frame;mirror_enabled=false;capture_recorder->record(reference);result=native_paint_frame(ctx);capture_recorder->finish();mirror_enabled=true;
        bool equal=!result&&same_commands(frame,reference);
        if(graphics_log){std::fprintf(graphics_log,"MIRROR_CHECK %u %s commands=%zu reference=%zu\n",drawing_frame,equal?"PASS":"FAIL",frame.commands.size(),reference.commands.size());if(!equal)std::fflush(graphics_log);}
        if(!equal)result=6;
    }
    return result;
}
void de_play_capture(const psp60::Frame& frame,float alpha){frame.play(drawing_renderer,alpha);submit_psp_frame();}
void de_flush_render_audit(){
#ifdef DE_NATIVE_PAINT
    if(graphics_log)if(FILE* f=std::fopen("VISUAL_POLICY_STATS.JSON","w")){
        std::fprintf(f,"{\"scope\":\"Render-only settled corpse suppression and steady dying alpha;living actors and source game objects unchanged\",\"actor_samples\":%llu,\"dead_actor_samples\":%llu,\"dying_actor_samples\":%llu,\"unsupported_samples\":%llu,\"corpse_draw_skips\":%llu}\n",corpse_actor_samples,corpse_hidden_samples,corpse_dying_samples,corpse_guard_fallbacks,corpse_draw_skips);std::fclose(f);
    }
#endif
    if(FILE* f=std::fopen("CAPTURE_BACKEND.JSON","w")){std::fprintf(f,"{\"direct_static_tries\":%llu,\"direct_static_hits\":%llu,\"batched_render_sync_compiled\":%s,\"direct_baked_capture_compiled\":%s}\n",direct_baked_tries,direct_baked_hits,DE_BATCH_RENDER_SYNC?"true":"false",DE_DIRECT_BAKED_CAPTURE?"true":"false");std::fclose(f);}
    if(graphics_log)std::fflush(graphics_log);if(paint_timing)std::fflush(paint_timing);
#if DE_PREPARE_TEXTURES
    flush_psp_prepare_stats();
#endif
}
