#pragma once
#include "quickjs.h"
#include <cmath>
namespace desourcerounding {
enum {Source,Global,Integer,NumberName,FiniteName,MathName,FloorName,CeilName,
      NumberAtom,FiniteAtom,MathAtom,FloorAtom,CeilAtom,DataCount};
static unsigned calls=0,integers=0,twips=0,fallbacks=0,factories=0,rejected=0;
static JSAtom atom(JSValue* data,unsigned key){return JSAtom(JS_VALUE_GET_INT(data[key]));}
static JSValue run(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int twip,JSValue* d){
    ++calls;
    auto original=[&](){++fallbacks;return JS_Call(c,d[Source],self,argc,argv);};
    if(!argc||!JS_IsNumber(argv[0])||(twip&&!JS_CheckSingleClosureValue(c,d[Source],d[Integer])))return original();
    JSValue number,finite;
    if(!JS_TryOwnDataPropertyCachedBorrowed(c,d[Global],atom(d,NumberAtom),&number)||
       !JS_IsSourceRoundingIntrinsic(c,number,0)||
       !JS_TryOwnDataPropertyCachedBorrowed(c,d[Global],atom(d,FiniteAtom),&finite)||
       !JS_IsSourceRoundingIntrinsic(c,finite,1))return original();
    double value=0;JS_ToFloat64(c,&value,argv[0]);if(twip)value=value*20.0;
    double result=0;
    if(std::isfinite(value)){
        JSValue math,method;bool negative=value<0;
        if(!JS_TryOwnDataPropertyCachedBorrowed(c,d[Global],atom(d,MathAtom),&math)||
           !JS_TryOwnDataPropertyCachedBorrowed(c,math,atom(d,negative?CeilAtom:FloorAtom),&method)||
           !JS_IsSourceRoundingIntrinsic(c,method,negative?3:2))return original();
        result=negative?std::ceil(value):std::floor(value);
    }
    if(twip){result=result/20.0;++twips;}else ++integers;
    return JS_NewFloat64(c,result);
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    ++factories;
    auto original=[&](){++rejected;return argc?JS_DupValue(c,argv[0]):JS_NULL;};
    if(argc!=4)return original();
    bool twip=JS_ToBool(c,argv[3]);
    if(!JS_CheckSourceFunction(c,argv[0],twip?50:96,twip?3019472459u:1201033887u)||
       !JS_CheckSourceClosureCount(argv[0],twip?1:0))return original();
    JSValue global=JS_GetGlobalObject(c);bool same=JS_IsStrictEqual(c,global,argv[1]);JS_FreeValue(c,global);
    if(!same)return original();
    if(twip){
        if(!JS_CheckCFunctionDataValue(c,argv[2],run,0,Global,argv[1]))return original();
        int yes=JS_CheckClosureBinding(c,argv[0],"integer",argv[2]);if(yes<0)return JS_EXCEPTION;if(!yes)return original();
    }
    JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;
    data[Source]=JS_DupValue(c,argv[0]);data[Global]=JS_DupValue(c,argv[1]);
    if(twip)data[Integer]=JS_DupValue(c,argv[2]);
    const char* names[]={"Number","isFinite","Math","floor","ceil"};bool ready=true;
    for(unsigned i=0;i<5;i++){
        JSAtom a=JS_NewAtom(c,names[i]);if(!a){ready=false;break;}
        data[NumberName+i]=JS_AtomToValue(c,a);data[NumberAtom+i]=JS_NewInt32(c,int32_t(a));JS_FreeAtom(c,a);
        if(JS_IsException(data[NumberName+i])){ready=false;break;}
    }
    JSValue result=ready?JS_NewCFunctionData(c,run,1,twip,DataCount,data):JS_EXCEPTION;
    for(auto v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
    JSValue out=JS_NewObject(c);const char* names[]={"calls","integers","twips","fallbacks","factories","rejected"};unsigned values[]={calls,integers,twips,fallbacks,factories,rejected};
    for(unsigned i=0;i<6;i++)JS_SetPropertyStr(c,out,names[i],JS_NewUint32(c,values[i]));return out;
}
static void bind(JSContext* c){
    calls=integers=twips=fallbacks=factories=rejected=0;
    JSValue g=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,g,"nativeSourceRoundingFactory",JS_NewCFunction(c,factory,"nativeSourceRoundingFactory",4));
    JS_SetPropertyStr(c,g,"nativeSourceRoundingStats",JS_NewCFunction(c,stats,"nativeSourceRoundingStats",0));JS_FreeValue(c,g);
}
}
