#pragma once
#include "quickjs.h"
#include <cstdint>
namespace deworld {
enum Key {Pending,Get,MatrixMethod,BoundsMethod,Valid,World,Local,HitMatrix,HitLocal,Hit,KeyCount};
enum Root {Runtime=2*KeyCount,Marks,SourceWorld,SourceMatrix,SourceBounds,DataCount};
static uint64_t queries=0,hits=0,fallbacks=0;
static JSAtom key(JSValue* data,unsigned k){return JSAtom(JS_VALUE_GET_INT(data[KeyCount+k]));}
static bool own(JSValue* data,JSValueConst object,unsigned k,JSValue* out){return JS_TryOwnDataPropertyBorrowed(object,key(data,k),out)==1;}
static bool method(JSContext* c,JSValue* data,JSValueConst object,unsigned k,JSValueConst expected){
    JSValue value;return JS_TryDataPropertyBorrowed(object,key(data,k),&value)==1&&JS_IsStrictEqual(c,value,expected);
}
static bool cached(JSContext* c,JSValue* data,JSValueConst self,JSValue* out){
    JSValue pending,get,flags,valid,world,local,hit_matrix,hit_local,hit;uint32_t count=0;
    // Construction can temporarily have parent links outside the child list.
    // The source ancestry fallback remains mandatory for this whole interval.
    if(!own(data,data[Runtime],Pending,&pending)||!JS_TryDenseArrayLength(pending,&count)||count)return false;
    if(JS_TryDataPropertyBorrowed(data[Marks],key(data,Get),&get)!=1||
       !JS_TryWeakMapGetBorrowed(c,data[Marks],get,self,&flags)||
       JS_VALUE_GET_TAG(flags)!=JS_TAG_INT||(JS_VALUE_GET_INT(flags)&1)==0)return false;
    if(!own(data,self,Valid,&valid)||!JS_IsBool(valid)||!JS_ToBool(c,valid))return false;
    // Borrowed descriptor inspection never calls user accessors or proxies.
    if(!method(c,data,self,MatrixMethod,data[SourceMatrix])||!method(c,data,self,BoundsMethod,data[SourceBounds]))return false;
    if(!own(data,self,World,&world)||!own(data,self,Local,&local)||
       !own(data,self,HitMatrix,&hit_matrix)||!own(data,self,HitLocal,&hit_local)||
       !own(data,self,Hit,&hit)||!JS_IsStrictEqual(c,world,hit_matrix)||!JS_IsStrictEqual(c,local,hit_local))return false;
    *out=hit;return true;
}
static JSValue bounds(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int,JSValue* data){
    ++queries;JSValue value;
    if(cached(c,data,self,&value)){++hits;return JS_DupValue(c,value);}
    ++fallbacks;return JS_Call(c,data[SourceWorld],self,argc,argv);
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    if(argc!=5||!JS_IsObject(argv[0])||!JS_IsObject(argv[1])||
       !JS_IsFunction(c,argv[2])||!JS_IsFunction(c,argv[3])||!JS_IsFunction(c,argv[4]))
        return JS_ThrowTypeError(c,"Runtime,dependency marks and source bounds methods required");
    const char* names[]={"pending","get","_globalMatrix","_bounds","_boundsValid","_worldMatrix","_boundsCache","_hitBoundsMatrix","_hitBoundsLocal","_hitBounds"};
    JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;bool failed=false;
    for(unsigned i=0;i<KeyCount&&!failed;i++){
        JSAtom atom=JS_NewAtom(c,names[i]);if(!atom){failed=true;break;}
        data[i]=JS_AtomToString(c,atom);data[KeyCount+i]=JS_NewInt32(c,int32_t(atom));JS_FreeAtom(c,atom);
        failed=JS_IsException(data[i]);
    }
    for(unsigned i=0;i<5;i++)data[Runtime+i]=JS_DupValue(c,argv[i]);
    JSValue result=failed?JS_EXCEPTION:JS_NewCFunctionData(c,bounds,0,0,DataCount,data);
    for(auto& v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
    JSValue result=JS_NewObject(c);if(JS_IsException(result))return result;
    JS_SetPropertyStr(c,result,"queries",JS_NewFloat64(c,double(queries)));
    JS_SetPropertyStr(c,result,"hits",JS_NewFloat64(c,double(hits)));
    JS_SetPropertyStr(c,result,"fallbacks",JS_NewFloat64(c,double(fallbacks)));return result;
}
static void bind(JSContext* c){
    queries=hits=fallbacks=0;JSValue global=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,global,"nativeCachedWorldBoundsFactory",JS_NewCFunction(c,factory,"nativeCachedWorldBoundsFactory",5));
    JS_SetPropertyStr(c,global,"nativeWorldBoundsStats",JS_NewCFunction(c,stats,"nativeWorldBoundsStats",0));JS_FreeValue(c,global);
}
}
