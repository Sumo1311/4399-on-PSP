#pragma once
#include "quickjs.h"
#include <cstdint>
#include <climits>
namespace derandomstep {
enum Key{Rng,Seed,Calls,KeyCount};
enum {Runtime=KeyCount*2,DataCount};
static uint64_t queries=0,completed=0,fallbacks=0;
static JSAtom key(JSValue* d,unsigned i){return JSAtom(JS_VALUE_GET_INT(d[KeyCount+i]));}
// The AS3-compatible generator uses 32-bit shifts and multiplication modulo
// 2^32. Explicit sign extension avoids implementation-defined signed shifts.
static uint32_t right21(uint32_t n){return (n>>21)|((n&0x80000000u)?0xfffff800u:0u);}
static JSValue step(JSContext* c,JSValueConst,int,JSValueConst*,int,JSValue* d){
    ++queries;auto no=[](){++fallbacks;return JS_FALSE;};
    JSValue state,calls,seed;uint32_t rng=0;
    if(JS_TryOwnDataPropertyCachedBorrowed(c,d[Runtime],key(d,Rng),&state)!=1||!JS_IsNumber(state)||
       JS_TryOwnDataPropertyCachedBorrowed(c,d[Runtime],key(d,Calls),&calls)!=1||
       JS_VALUE_GET_TAG(calls)!=JS_TAG_INT||JS_VALUE_GET_INT(calls)<0||JS_VALUE_GET_INT(calls)==INT_MAX)return no();
    if(JS_ToBool(c,state)){
        if(JS_ToUint32(c,&rng,state))return JS_EXCEPTION;
    }else{
        if(JS_TryOwnDataPropertyCachedBorrowed(c,d[Runtime],key(d,Seed),&seed)!=1||!JS_IsNumber(seed))return no();
        if(JS_ToUint32(c,&rng,seed))return JS_EXCEPTION;
    }
    rng=(rng&1u)?((rng>>1)^0x48000000u):(rng>>1);
    uint32_t s=rng*71u;s=((s<<13)^s)-right21(s);
    uint32_t v=s*s;v=v*15731u+789221u;v=v*s+1376312589u;
    v=(v&0x7fffffffu)+s;v=((v<<13)^v)-right21(v);
    const JSAtom keys[]={key(d,Rng),key(d,Calls)};
    const JSValue values[]={JS_NewUint32(c,rng),JS_NewInt32(c,JS_VALUE_GET_INT(calls)+1)};
    // No allocation, callback or coercion occurs between the pure guards and
    // this atomic own-data commit. Readonly/accessor cases execute the source.
    if(!JS_TryWriteOwnPrimitives(d[Runtime],keys,values,2))return no();
    ++completed;return JS_NewFloat64(c,double(v&0x7fffffffu)/2147483648.0);
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    if(argc!=1||!JS_IsObject(argv[0]))return JS_NULL;
    const char* names[]={"rng","seed","rngCalls"};JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;bool ready=true;
    for(unsigned i=0;i<KeyCount;i++){
        JSAtom a=JS_NewAtom(c,names[i]);if(!a){ready=false;break;}
        data[i]=JS_AtomToValue(c,a);data[KeyCount+i]=JS_NewInt32(c,int32_t(a));JS_FreeAtom(c,a);
        if(JS_IsException(data[i])){ready=false;break;}
    }
    data[Runtime]=JS_DupValue(c,argv[0]);JSValue result=ready?JS_NewCFunctionData(c,step,0,0,DataCount,data):JS_EXCEPTION;
    for(auto v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
    JSValue out=JS_NewObject(c);if(JS_IsException(out))return out;
    JS_SetPropertyStr(c,out,"queries",JS_NewFloat64(c,double(queries)));
    JS_SetPropertyStr(c,out,"completed",JS_NewFloat64(c,double(completed)));
    JS_SetPropertyStr(c,out,"fallbacks",JS_NewFloat64(c,double(fallbacks)));return out;
}
static void bind(JSContext* c){
    queries=completed=fallbacks=0;JSValue g=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,g,"nativeRandomStepFactory",JS_NewCFunction(c,factory,"nativeRandomStepFactory",1));
    JS_SetPropertyStr(c,g,"nativeRandomStepStats",JS_NewCFunction(c,stats,"nativeRandomStepStats",0));JS_FreeValue(c,g);
}
}
