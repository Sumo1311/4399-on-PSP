#pragma once
#include "quickjs.h"
#include "flash_math.h"
#include "matrix_chain117.hpp"
#include <cstdint>
namespace debuild {
enum Key {Uid,Revision,Parent,CachedParent,Matrix,Chain,Prototype,Instance,KeyCount};
enum Root {Global=2*KeyCount,HasInstance,Stage,ChainFunction,DataCount};
static uint64_t calls=0,built=0,unchanged=0,fallbacks=0,chain117_calls=0,chain117_pairs=0,chain117_zero_translations=0;
static JSAtom key(JSValue* d,unsigned k){return JSAtom(JS_VALUE_GET_INT(d[KeyCount+k]));}
static bool own(JSValue* d,JSValueConst o,unsigned k,JSValue* v){return JS_TryOwnDataPropertyBorrowed(o,key(d,k),v)==1;}
static bool same(JSContext* c,JSValue* d,JSValueConst o,unsigned k,JSValueConst expected){
    JSValue v;return JS_TryDataPropertyBorrowed(o,key(d,k),&v)==1&&JS_IsStrictEqual(c,v,expected);
}
static bool writableParent(JSContext* c,JSValue* d,JSValueConst o){
    JSValue current;if(!own(d,o,CachedParent,&current))return false;
    JSPropertyDescriptor desc{};int found=JS_GetOwnProperty(c,&desc,o,key(d,CachedParent));
    if(found!=1)return false;
    bool yes=(desc.flags&JS_PROP_WRITABLE)&&!(desc.flags&(JS_PROP_GETSET|JS_PROP_VARREF|JS_PROP_AUTOINIT));
    JS_FreeValue(c,desc.value);JS_FreeValue(c,desc.getter);JS_FreeValue(c,desc.setter);return yes;
}
static JSValue build(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* d){
    ++calls;
    auto fallback=[](){++fallbacks;return JS_FALSE;};
    JSValue prototype;uint32_t pathLength=0;
    if(argc!=3||!JS_IsBool(argv[2])||
       !same(c,d,d[Global],Chain,d[ChainFunction])||
       !same(c,d,d[Stage],Instance,d[HasInstance])||
       JS_TryDataPropertyBorrowed(d[Stage],key(d,Prototype),&prototype)!=1||!JS_IsObject(prototype)||
       !JS_TryDenseArrayLength(argv[1],&pathLength)||!pathLength||pathLength>128||(pathLength&1))return fallback();
    double oldPath[128],nextPath[128],matrices[64][6];JSValue nodes[64],parents[64];
    if(!JS_ReadNumericArray(argv[1],oldPath,pathLength))return fallback();
    unsigned count=0;bool changed=JS_ToBool(c,argv[2]);JSValue node=argv[0];
    // Pure preflight: the input roots this strong parent chain. No writes,
    // allocations, coercions, accessors or user callbacks occur in this pass.
    do {
        if(count>=pathLength/2)return fallback();
        JSValue uid,revision,parent,matrix;
        if(!own(d,node,Uid,&uid)||!own(d,node,Revision,&revision)||!own(d,node,Parent,&parent)||
           !own(d,node,Matrix,&matrix)||!writableParent(c,d,node)||
           JS_IsStrictEqual(c,matrix,argv[1])||!JS_IsNumber(uid)||!JS_IsNumber(revision)||
           (!JS_IsObject(parent)&&!JS_IsNull(parent))||!JS_ReadNumericArray(matrix,matrices[count],6))return fallback();
        double identity=0,version=0;JS_ToFloat64(c,&identity,uid);JS_ToFloat64(c,&version,revision);
        nextPath[2*count]=identity;nextPath[2*count+1]=version;
        if(identity!=oldPath[2*count]||version!=oldPath[2*count+1])changed=true;
        nodes[count]=node;parents[count]=parent;++count;node=parent;
        if(JS_IsNull(node))break;
        // Stage's @@hasInstance was verified to be the intrinsic data method;
        // its prototype is a data object. This check cannot run user code.
        int stop=0;if(!JS_TryPrototypeContains(node,prototype,&stop))return fallback();if(stop)break;
    } while(true);
    if(count*2!=pathLength)return fallback();
    // Requiring an already materialized fixed-length path leaves first use,
    // reparenting-depth changes, readonly arrays and unusual metadata to JS.
    // The primitive-array write is atomic and cannot allocate or call JS.
    for(unsigned i=0;i<count;i++){nodes[i]=JS_DupValue(c,nodes[i]);parents[i]=JS_DupValue(c,parents[i]);}
    if(!JS_TryWriteNumericArray(argv[1],nextPath,pathLength)){
        for(unsigned i=0;i<count;i++){JS_FreeValue(c,nodes[i]);JS_FreeValue(c,parents[i]);}return fallback();
    }
    bool error=false;
    for(unsigned i=0;i<count;i++){
        if(!error&&JS_SetPropertyValue(c,nodes[i],JS_DupValue(c,d[CachedParent]),JS_DupValue(c,parents[i]),0)<0)error=true;
        JS_FreeValue(c,nodes[i]);JS_FreeValue(c,parents[i]);
    }
    if(error)return JS_EXCEPTION;
    if(!changed){++unchanged;return JS_NULL;}
    double world[6];unsigned zero_translations=0;de_matrix_chain117(matrices,count,world,&zero_translations);chain117_zero_translations+=zero_translations;++chain117_calls;chain117_pairs+=count-1;
    JSValue result=JS_NewArray(c);if(JS_IsException(result))return result;
    for(unsigned j=0;j<6;j++)if(JS_SetPropertyUint32(c,result,j,JS_NewFloat64(c,world[j]))<0){JS_FreeValue(c,result);return JS_EXCEPTION;}
    ++built;return result;
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* original){
    if(argc!=2||!JS_IsFunction(c,argv[0])||!JS_IsFunction(c,argv[1]))return JS_ThrowTypeError(c,"Stage and native matrix chain required");
    if(!JS_IsStrictEqual(c,argv[1],original[ChainFunction]))return JS_NULL;
    JSValue data[DataCount];for(unsigned i=0;i<DataCount;i++)data[i]=JS_DupValue(c,original[i]);
    JS_FreeValue(c,data[Stage]);JS_FreeValue(c,data[ChainFunction]);data[Stage]=JS_DupValue(c,argv[0]);data[ChainFunction]=JS_DupValue(c,argv[1]);
    JSValue result=JS_NewCFunctionData(c,build,3,0,DataCount,data);for(auto v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
    JSValue result=JS_NewObject(c);if(JS_IsException(result))return result;
    const char* names[]={"calls","built","unchanged","fallbacks"};uint64_t values[]={calls,built,unchanged,fallbacks};
    for(unsigned i=0;i<4;i++)JS_SetPropertyStr(c,result,names[i],JS_NewFloat64(c,double(values[i])));JS_SetPropertyStr(c,result,"chain117_calls",JS_NewFloat64(c,double(chain117_calls)));JS_SetPropertyStr(c,result,"chain117_pairs",JS_NewFloat64(c,double(chain117_pairs)));JS_SetPropertyStr(c,result,"chain117_zero_translations",JS_NewFloat64(c,double(chain117_zero_translations)));return result;
}
static void bind(JSContext* c){
    calls=built=unchanged=fallbacks=chain117_calls=chain117_pairs=0,chain117_zero_translations=0;JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;
    const char* names[]={"_uid","_matrixVersion","parent","_worldParent","_matrix","nativeMatrixChain","prototype"};bool failed=false;
    for(unsigned i=0;i<Instance;i++){
        JSAtom a=JS_NewAtom(c,names[i]);if(!a){failed=true;break;}data[i]=JS_AtomToValue(c,a);data[KeyCount+i]=JS_NewInt32(c,int32_t(a));JS_FreeAtom(c,a);if(JS_IsException(data[i])){failed=true;break;}
    }
    data[Global]=JS_GetGlobalObject(c);data[ChainFunction]=JS_GetPropertyStr(c,data[Global],"nativeMatrixChain");JSValue symbol=JS_GetPropertyStr(c,data[Global],"Symbol");data[Instance]=JS_GetPropertyStr(c,symbol,"hasInstance");JS_FreeValue(c,symbol);
    JSAtom a=JS_ValueToAtom(c,data[Instance]);data[KeyCount+Instance]=JS_NewInt32(c,int32_t(a));
    JSValue fn=JS_GetPropertyStr(c,data[Global],"Function"),proto=JS_GetPropertyStr(c,fn,"prototype");data[HasInstance]=JS_GetProperty(c,proto,a);JS_FreeAtom(c,a);JS_FreeValue(c,proto);JS_FreeValue(c,fn);
    if(!failed){JSValue f=JS_NewCFunctionData(c,factory,2,0,DataCount,data);JS_SetPropertyStr(c,data[Global],"nativeWorldPathBuilderFactory",f);JS_SetPropertyStr(c,data[Global],"nativeWorldBuildStats",JS_NewCFunction(c,stats,"nativeWorldBuildStats",0));}
    for(auto v:data)JS_FreeValue(c,v);
}
}
