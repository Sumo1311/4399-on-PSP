#pragma once
#include "quickjs.h"
#include <cstdint>
namespace deobjecthit {
enum {Compare,WorldName,WorldAtom,DataCount};
static uint64_t queries=0,numeric=0,empty=0,fallbacks=0,errors=0;
static JSValue method(JSContext* c,JSValueConst object,JSAtom atom){
    JSValue value;if(JS_TryDataPropertyBorrowed(object,atom,&value)==1)return JS_DupValue(c,value);
    return JS_GetProperty(c,object,atom);
}
static JSValue run(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* d){
    ++queries;if(argc!=2)return JS_ThrowTypeError(c,"Hit-test operands required");
    JSValue a=JS_UNDEFINED,b=JS_UNDEFINED,result=JS_EXCEPTION;
    JSAtom key=JSAtom(JS_VALUE_GET_INT(d[WorldAtom]));
    // Keep the exact source lookup/call order, including mutations to the
    // second operand during the first callback. Keep both results owned until
    // after comparison and release a before b, as in the source local slots.
    JSValue fn=method(c,argv[0],key);
    if(JS_IsException(fn))goto done;
    a=JS_Call(c,fn,argv[0],0,nullptr);JS_FreeValue(c,fn);if(JS_IsException(a))goto done;
    fn=method(c,argv[1],key);
    if(JS_IsException(fn))goto done;
    b=JS_Call(c,fn,argv[1],0,nullptr);JS_FreeValue(c,fn);if(JS_IsException(b))goto done;
    if(!JS_ToBool(c,a)||!JS_ToBool(c,b)){++empty;result=JS_FALSE;goto done;}
    {
        double first[4],second[4];
        if(JS_ReadNumericArray(a,first,4)&&JS_ReadNumericArray(b,second,4)){
            ++numeric;result=JS_NewBool(c,first[0]<=second[1]&&first[1]>=second[0]&&first[2]<=second[3]&&first[3]>=second[2]);
        }else{
            ++fallbacks;JSValueConst args[]={a,b};result=JS_Call(c,d[Compare],JS_UNDEFINED,2,args);
        }
    }
done:
    if(JS_IsException(result))++errors;
    JS_FreeValue(c,a);JS_FreeValue(c,b);return result;
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    if(argc!=1||!JS_CheckSourceFunction(c,argv[0],79,2064896338u)||!JS_CheckSourceClosureCount(argv[0],0))return JS_NULL;
    JSAtom key=JS_NewAtom(c,"_worldBounds");if(!key)return JS_EXCEPTION;
    JSValue data[]={JS_DupValue(c,argv[0]),JS_AtomToValue(c,key),JS_NewInt32(c,int32_t(key))};JS_FreeAtom(c,key);
    JSValue result=JS_IsException(data[WorldName])?JS_EXCEPTION:JS_NewCFunctionData(c,run,2,0,DataCount,data);
    for(auto v:data)JS_FreeValue(c,v);return result;
}
static void bind(JSContext* c){
    queries=numeric=empty=fallbacks=errors=0;JSValue global=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,global,"nativeObjectHitFactory",JS_NewCFunction(c,factory,"nativeObjectHitFactory",1));JS_FreeValue(c,global);
}
}
