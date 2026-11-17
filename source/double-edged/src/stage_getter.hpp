#pragma once
#include "quickjs.h"
#include <cstdint>
namespace destagegetter {
enum{Source,Stage,ParentName,ParentAtom,Count};
static uint64_t queries=0,hops=0,polls=0,errors=0;
static unsigned max_hops=0;
static JSValue run(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* data){
    ++queries;
    if(argc!=1)return JS_ThrowTypeError(c,"Stage operand required");
    JSValue current=JS_DupValue(c,argv[0]);
    JSAtom parent=JSAtom(JS_VALUE_GET_INT(data[ParentAtom]));
    unsigned depth=0;
    // Preserve the source test/get order and the owned local's lifetime.
    // Poll the same engine interrupt mechanism at the source backedge.
    // No source continuation or extra live intermediate node is retained.
    for(;;){
        if(!JS_ToBool(c,current)){JS_FreeValue(c,current);return JS_NULL;}
        ++hops;++depth;if(depth>max_hops)max_hops=depth;
        int hit=JS_IsInstanceOf(c,current,data[Stage]);
        if(hit<0){++errors;JS_FreeValue(c,current);return JS_EXCEPTION;}
        if(hit)return current;
        JSValue next;
        if(JS_TryDataPropertyBorrowed(current,parent,&next)==1)next=JS_DupValue(c,next);
        else next=JS_GetProperty(c,current,parent);
        JS_FreeValue(c,current);current=next;
        if(JS_IsException(current)){++errors;return current;}
        ++polls;if(JS_PollStageInterrupt(c)){++errors;JS_FreeValue(c,current);return JS_EXCEPTION;}
    }
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    constexpr const char source[]="function(p){while(p&&!(p instanceof Stage))p=p.parent;return p||null;}";
    uint32_t hash=2166136261u;for(unsigned char byte:source){if(byte)hash=(hash^byte)*16777619u;}
    if(argc!=2||!JS_CheckSourceFunction(c,argv[0],sizeof(source)-1,hash)||
       !JS_CheckSourceClosureCount(argv[0],1)||!JS_IsFunction(c,argv[1]))return JS_NULL;
    JSAtom key=JS_NewAtom(c,"parent");if(!key)return JS_EXCEPTION;
    JSValue data[]={JS_DupValue(c,argv[0]),JS_DupValue(c,argv[1]),JS_AtomToValue(c,key),JS_NewInt32(c,int32_t(key))};JS_FreeAtom(c,key);
    JSValue result=JS_IsException(data[ParentName])?JS_EXCEPTION:JS_NewCFunctionData(c,run,1,0,Count,data);
    for(auto value:data)JS_FreeValue(c,value);return result;
}
static void bind(JSContext* c){
    queries=hops=polls=errors=0;max_hops=0;
    JSValue global=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,global,"nativeStageGetterFactory",JS_NewCFunction(c,factory,"nativeStageGetterFactory",2));
    JS_FreeValue(c,global);
}
}
