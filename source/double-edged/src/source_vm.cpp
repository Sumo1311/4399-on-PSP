#include "source_vm.hpp"
#include "script_diagnostics.h"
#include "flash_math_bindings.h"
#include "typed_slots.h"
#include "array_ops.h"
#include "depth_sort.h"
#include "timeline_walk.hpp"
#include "cached_world_bounds.hpp"
#include "cpu_collision.hpp"
#include "world_builder.hpp"
#include "rock_collision.hpp"
#include "display_prefix.hpp"
#include "display_suffix111.hpp"
#include "world_invalidation.hpp"
#include "source_rounding.hpp"
#include "goto_noop130.hpp"
#include "object_hit.hpp"
#include "random_step.hpp"
#include "stage_getter.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#ifdef __PSP__
#include <pspkernel.h>
#endif
namespace {
union Header{size_t size;double alignment;void* pointer;};
size_t allocated=0,peak=0;uint64_t clock_origin=0;
void* allocate(void*,size_t n){if(!n||n>std::numeric_limits<size_t>::max()-sizeof(Header))return nullptr;auto p=static_cast<Header*>(std::malloc(sizeof(Header)+n));if(!p)return nullptr;p->size=n;allocated+=n;if(allocated>peak)peak=allocated;return p+1;}
void release(void*,void* p){if(!p)return;auto h=static_cast<Header*>(p)-1;allocated-=h->size;std::free(h);}
void* resize(void* opaque,void* p,size_t n){if(!p)return allocate(opaque,n);if(!n){release(opaque,p);return nullptr;}if(n>std::numeric_limits<size_t>::max()-sizeof(Header))return nullptr;auto h=static_cast<Header*>(p)-1;size_t old=h->size;auto q=static_cast<Header*>(std::realloc(h,sizeof(Header)+n));if(!q)return nullptr;q->size=n;allocated=allocated-old+n;if(allocated>peak)peak=allocated;return q+1;}
void* zero(void* opaque,size_t count,size_t size){if(size&&count>std::numeric_limits<size_t>::max()/size)return nullptr;size*=count;void* p=allocate(opaque,size);if(p)std::memset(p,0,size);return p;}
size_t usable(const void* p){return p?(static_cast<const Header*>(p)-1)->size:0;}
uint64_t now(){
#ifdef __PSP__
    return sceKernelGetSystemTimeWide();
#else
    timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000+t.tv_nsec/1000;
#endif
}
bool read_file(const char* path,std::string& out){FILE* f=std::fopen(path,"rb");if(!f)return false;if(std::fseek(f,0,SEEK_END)){std::fclose(f);return false;}long n=std::ftell(f);if(n<0||n>16*1024*1024){std::fclose(f);return false;}std::rewind(f);out.resize(n);bool valid=std::fread(out.data(),1,n,f)==size_t(n);std::fclose(f);return valid;}
JSValue read_text(JSContext* c,JSValueConst,int argc,JSValueConst* argv){const char* name=JS_ToCString(c,argc?argv[0]:JS_UNDEFINED);if(!name)return JS_EXCEPTION;std::string data;bool valid=!std::strncmp(name,"levels/",7)&&!std::strstr(name,"..")&&!std::strchr(name,'\\')&&!std::strchr(name+7,'/')&&read_file(name,data);JS_FreeCString(c,name);if(!valid)return JS_ThrowInternalError(c,"Cannot read local level");return JS_NewStringLen(c,data.data(),data.size());}
JSValue float32(JSContext* c,JSValueConst,int argc,JSValueConst* argv){double value=0;if(JS_ToFloat64(c,&value,argc?argv[0]:JS_UNDEFINED))return JS_EXCEPTION;return JS_NewFloat64(c,float(value));}
JSValue clock_ms(JSContext* c,JSValueConst,int,JSValueConst*){return JS_NewUint32(c,unsigned((now()-clock_origin)/1000));}
}
SourceVM::SourceVM(){JSMallocFunctions functions={zero,allocate,release,resize,usable};runtime_=JS_NewRuntime2(&functions,nullptr);if(runtime_){JS_SetMaxStackSize(runtime_,512*1024);JS_SetMemoryLimit(runtime_,40*1024*1024);context_=JS_NewContext(runtime_);}if(!context_)error_="Cannot create source runtime";}
SourceVM::~SourceVM(){if(runtime_){JS_ClearInitializerLayouts(runtime_);JS_ReleaseConstJSON(runtime_);}if(context_)JS_FreeContext(context_);if(runtime_)JS_FreeRuntime(runtime_);}
void SourceVM::capture_exception(){JSValue e=JS_GetException(context_);const char* message=JS_ToCString(context_,e);error_=message?message:"Script exception";JS_FreeCString(context_,message);if(JS_IsObject(e)){JSValue stack=JS_GetPropertyStr(context_,e,"stack");const char* s=JS_ToCString(context_,stack);if(s){error_+='\n';error_+=s;}JS_FreeCString(context_,s);JS_FreeValue(context_,stack);}JS_FreeValue(context_,e);}
bool SourceVM::eval(const char* source,const char* label){if(!context_)return false;JSValue value=JS_Eval(context_,source,std::strlen(source),label,JS_EVAL_TYPE_GLOBAL);bool valid=!JS_IsException(value);if(!valid)capture_exception();JS_FreeValue(context_,value);return valid;}
bool SourceVM::eval_file(const char* path){std::string data;if(!read_file(path,data)){error_=std::string("Cannot read ")+path;return false;}if(!std::strcmp(path,"classes.js"))derock::authorize(context_,data.data(),data.size());return eval(data.c_str(),path);}
bool SourceVM::initialize(uint32_t seed,bool realtime_clock){
    if(!context_)return false;clock_origin=now();de_script_diagnostics_bind(context_);de_flash_math_bind(context_);de_typed_slots_bind(context_);de_array_ops_bind(context_);de_depth_sort_bind(context_);deworld::bind(context_);decpu::bind(context_);debuild::bind(context_);derock::bind(context_);dedisplayprefix::bind(context_);desuffix111::bind(context_);deworldinvalidate::bind(context_);desourcerounding::bind(context_);degoto130::bind(context_);deobjecthit::bind(context_);derandomstep::bind(context_);destagegetter::bind(context_);
#if DE_NATIVE_TIMELINE
    dewalk::bind(context_);
#endif
    JSValue g=JS_GetGlobalObject(context_);
    JS_SetPropertyStr(context_,g,"nativeReadText",JS_NewCFunction(context_,read_text,"nativeReadText",1));JS_SetPropertyStr(context_,g,"nativeFloat32",JS_NewCFunction(context_,float32,"nativeFloat32",1));if(realtime_clock)JS_SetPropertyStr(context_,g,"nativeClockMS",JS_NewCFunction(context_,clock_ms,"nativeClockMS",0));JS_FreeValue(context_,g);
    if(!eval_file("runtime.js"))return false;char settings[96];std::snprintf(settings,sizeof(settings),"FlashRuntime.seed=%lu;",static_cast<unsigned long>(seed?seed:42));if(!eval(settings,"seed")||!eval_file("classes.js"))return false;
    if(!eval_file("visual_policy.js"))return false;
    if(!dedisplayprefix::retain_source_roots(context_,derock::program_context==context_)){capture_exception();return false;}
    // Restore before releasing the natively owned realm in the destructor.
    parked_bytecodes_=JS_ParkScalarBytecode(context_);
    std::string data;if(!read_file("movie.json",data)){error_="Cannot read movie.json";return false;}JSValue movie;
#if DE_CONST_JSON
    movie=JS_ParseConstJSON(context_,data.data(),data.size(),"movie.json");
#else
    movie=JS_ParseJSON(context_,data.data(),data.size(),"movie.json");
#endif
    if(JS_IsException(movie)){capture_exception();return false;}
    // Parsed JSON owns its strings and numeric storage. Release the input copy
    // before constructing scene objects, rather than holding both at the peak.
    std::string().swap(data);
    g=JS_GetGlobalObject(context_);JS_SetPropertyStr(context_,g,"movie",movie);JS_FreeValue(context_,g);return eval("FlashRuntime.install(movie,DoubleEdgedEntry.classes);","install");
}
bool SourceVM::call(JSValueConst function,int argc,JSValueConst* argv){JSValue value=JS_Call(context_,function,JS_UNDEFINED,argc,argv);bool valid=!JS_IsException(value);if(!valid)capture_exception();JS_FreeValue(context_,value);return valid;}
// A full explicit scan must also refresh the allocation trigger. Otherwise
// a stale startup threshold can force another scan inside a battle callback.
// Automatic GC and the VM memory limit remain active.
void SourceVM::collect(){if(runtime_){JS_RunGC(runtime_);JS_RebaseGCThreshold(runtime_);}}
size_t SourceVM::heap_bytes(){return allocated;}
size_t SourceVM::peak_bytes(){return peak;}
