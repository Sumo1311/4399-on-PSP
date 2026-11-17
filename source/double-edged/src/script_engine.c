#include "script_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif
typedef union Header{size_t size;double align;void *pointer;}Header;
static size_t allocated,peak;
static void *allocate(void *udata,duk_size_t size){(void)udata;if(!size)return NULL;Header *p=malloc(sizeof(Header)+size);if(!p)return NULL;p->size=size;allocated+=size;if(allocated>peak)peak=allocated;return p+1;}
static void release(void *udata,void *pointer){(void)udata;if(!pointer)return;Header *p=(Header*)pointer-1;allocated-=p->size;free(p);}
static void *resize(void *udata,void *pointer,duk_size_t size){if(!pointer)return allocate(udata,size);if(!size){release(udata,pointer);return NULL;}Header *p=(Header*)pointer-1;size_t old=p->size;Header *q=realloc(p,sizeof(Header)+size);if(!q)return NULL;q->size=size;allocated=allocated-old+size;if(allocated>peak)peak=allocated;return q+1;}
static void fatal(void *udata,const char *message){(void)udata;fprintf(stderr,"Script fatal: %s\n",message);fflush(stderr);abort();}
/* Weak entries contain no Duktape roots. A finalizer invalidates the pointer
 * before collection. Generation tokens prevent an old weak handle from ever
 * resolving to a different object when a slot is reused. */
typedef struct WeakSlot{void *pointer;uint32_t generation,next;}WeakSlot;
static WeakSlot *weak_slots;
static uint32_t weak_count,weak_capacity,weak_free=UINT32_MAX;
static size_t weak_live;
#define TOKEN_BASE 1048576.0
#define WEAK_ID DUK_HIDDEN_SYMBOL("double-edged-weak-id")
static duk_ret_t finalize_weak(duk_context *ctx){
    if(!duk_get_prop_string(ctx,0,WEAK_ID))return 0;
    double token=duk_get_number(ctx,-1);uint32_t index=(uint32_t)fmod(token,TOKEN_BASE),generation=(uint32_t)(token/TOKEN_BASE);duk_pop(ctx);
    if(index<weak_count&&weak_slots[index].generation==generation&&weak_slots[index].pointer){weak_slots[index].pointer=NULL;weak_slots[index].next=weak_free;weak_free=index;--weak_live;}
    return 0;
}
static duk_ret_t register_weak(duk_context *ctx){
    duk_require_object_coercible(ctx,0);
    if(duk_get_prop_string(ctx,0,WEAK_ID))return 1;
    duk_pop(ctx);uint32_t index;
    if(weak_free!=UINT32_MAX){index=weak_free;weak_free=weak_slots[index].next;++weak_slots[index].generation;}
    else{
        if(weak_count==weak_capacity){uint32_t capacity=weak_capacity?weak_capacity*2:512;if(capacity>1048576)return duk_error(ctx,DUK_ERR_RANGE_ERROR,"Weak handle capacity");WeakSlot *p=realloc(weak_slots,capacity*sizeof(WeakSlot));if(!p)return duk_error(ctx,DUK_ERR_ERROR,"Weak handle allocation");weak_slots=p;weak_capacity=capacity;}
        index=weak_count++;weak_slots[index].generation=1;
    }
    weak_slots[index].pointer=duk_get_heapptr(ctx,0);++weak_live;
    double token=weak_slots[index].generation*TOKEN_BASE+index;
    duk_push_number(ctx,token);duk_put_prop_string(ctx,0,WEAK_ID);
    duk_push_c_function(ctx,finalize_weak,1);duk_set_finalizer(ctx,0);
    duk_push_number(ctx,token);return 1;
}
static duk_ret_t get_weak(duk_context *ctx){
    double token=duk_require_number(ctx,0);uint32_t index=(uint32_t)fmod(token,TOKEN_BASE),generation=(uint32_t)(token/TOKEN_BASE);
    if(index>=weak_count||weak_slots[index].generation!=generation||!weak_slots[index].pointer)return 0;
    duk_push_heapptr(ctx,weak_slots[index].pointer);return 1;
}
static char *read_file(const char *path,size_t *length){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);rewind(f);
    if(n<0||n>16*1024*1024){fclose(f);return NULL;}
    char *data=malloc((size_t)n+1);if(!data){fclose(f);return NULL;}
    if(fread(data,1,(size_t)n,f)!=(size_t)n){free(data);fclose(f);return NULL;}
    fclose(f);data[n]=0;*length=(size_t)n;return data;
}
static duk_ret_t read_text(duk_context *ctx){
    const char *name=duk_require_string(ctx,0);
    if(strncmp(name,"levels/",7)||strstr(name,"..")||strchr(name,'\\')||strchr(name+7,'/'))return duk_error(ctx,DUK_ERR_ERROR,"Invalid local level path");
    size_t length=0;char *data=read_file(name,&length);if(!data)return duk_error(ctx,DUK_ERR_ERROR,"Cannot read local level %s",name);
    duk_push_lstring(ctx,data,length);free(data);return 1;
}
static duk_ret_t as_float32(duk_context *ctx){float value=(float)duk_require_number(ctx,0);duk_push_number(ctx,(double)value);return 1;}
static duk_ret_t print_line(duk_context *ctx){for(duk_idx_t i=0;i<duk_get_top(ctx);i++)fprintf(stderr,"%s%s",i?" ":"",duk_safe_to_string(ctx,i));fputc('\n',stderr);return 0;}
duk_context *de_script_create(void){
    allocated=peak=weak_live=0;weak_count=weak_capacity=0;weak_free=UINT32_MAX;weak_slots=NULL;
    duk_context *ctx=duk_create_heap(allocate,resize,release,NULL,fatal);if(!ctx)return NULL;
    duk_push_c_function(ctx,register_weak,1);duk_put_global_string(ctx,"nativeWeakRegister");
    duk_push_c_function(ctx,get_weak,1);duk_put_global_string(ctx,"nativeWeakGet");
    duk_push_c_function(ctx,read_text,1);duk_put_global_string(ctx,"nativeReadText");
    duk_push_c_function(ctx,as_float32,1);duk_put_global_string(ctx,"nativeFloat32");
    duk_push_object(ctx);duk_push_c_function(ctx,print_line,DUK_VARARGS);duk_put_prop_string(ctx,-2,"log");duk_put_global_string(ctx,"console");
    return ctx;
}
static int checked_eval(duk_context *ctx,const char *data,size_t length,const char *name){
    duk_push_string(ctx,name);int error=duk_pcompile_lstring_filename(ctx,DUK_COMPILE_EVAL,data,length);
    if(!error)error=duk_pcall(ctx,0);
    if(error){fprintf(stderr,"Script %s: %s\n",name,duk_safe_to_stacktrace(ctx,-1));fflush(stderr);}
    duk_pop(ctx);return error;
}
int de_script_eval_file(duk_context *ctx,const char *path){size_t length=0;char *data=read_file(path,&length);if(!data){fprintf(stderr,"Cannot read script %s\n",path);return 1;}int error=checked_eval(ctx,data,length,path);free(data);return error;}
static duk_ret_t decode_json(duk_context *ctx,void *udata){(void)udata;duk_json_decode(ctx,-1);return 1;}
int de_script_json_file(duk_context *ctx,const char *path,const char *global_name){
    size_t length=0;char *data=read_file(path,&length);if(!data)return 1;
    duk_push_lstring(ctx,data,length);free(data);
    if(duk_safe_call(ctx,decode_json,NULL,1,1)){fprintf(stderr,"JSON %s: %s\n",path,duk_safe_to_stacktrace(ctx,-1));duk_pop(ctx);return 1;}
    duk_put_global_string(ctx,global_name);return 0;
}
int de_script_eval(duk_context *ctx,const char *source){return checked_eval(ctx,source,strlen(source),"native-call");}
size_t de_script_allocated(void){return allocated;}size_t de_script_peak(void){return peak;}size_t de_script_weak_live(void){return weak_live;}
uint64_t de_clock_us(void){
#ifdef __PSP__
    return sceKernelGetSystemTimeWide();
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
#endif
}
void de_script_destroy(duk_context *ctx){duk_destroy_heap(ctx);free(weak_slots);weak_slots=NULL;weak_count=weak_capacity=0;}
