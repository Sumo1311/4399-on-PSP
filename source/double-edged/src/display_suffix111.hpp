#pragma once
#include "quickjs.h"
#include <cstdint>
namespace desuffix111 {
enum Key{ScriptFilters,FiltersOverride,FiltersVersion,CacheAsBitmap,BlendMode,Mask,Counter,KeyCount};
enum {Strings=0,Atoms=KeyCount,Runtime=2*KeyCount,Normal,DataCount};
static uint32_t calls=0,completed=0,array_fallbacks=0,declines=0;
static JSAtom key(JSValue* data,unsigned i){return JSAtom(JS_VALUE_GET_INT(data[Atoms+i]));}
static JSValue run(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* data){
 ++calls;JSValue uid;
 if(argc!=1||!JS_IsObject(argv[0])){++declines;return JS_FALSE;}
 // Allocate once at the same point as the source [] literal. On a pure guard
 // decline return this exact Array to the JS continuation;never allocate twice.
 JSValue filters=JS_NewArray(c);if(JS_IsException(filters))return filters;
 if(JS_TryOwnDataPropertyCachedBorrowed(c,data[Runtime],key(data,Counter),&uid)!=1||JS_VALUE_GET_TAG(uid)!=JS_TAG_INT){++array_fallbacks;return filters;}
 const JSValue values[]={filters,JS_FALSE,JS_NewInt32(c,1),JS_FALSE,data[Normal],JS_NULL};
 JSAtom keys[6];for(unsigned i=0;i<6;i++)keys[i]=key(data,i);
 // The existing initializer writes the same current uid back;no increment.
 // Getter/setter/proxy/existing-property guards finish before any field write.
 int result=JS_TryInitializeOwnFields(c,argv[0],keys,values,6,data[Runtime],key(data,Counter),uid);
 if(result<0){JS_FreeValue(c,filters);return JS_EXCEPTION;}
 if(!result){++array_fallbacks;return filters;}
 JS_FreeValue(c,filters);++completed;return JS_TRUE;
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
 if(argc!=1||!JS_IsObject(argv[0]))return JS_NULL;
 const char* names[]={"_scriptFilters","_filtersOverride","_filtersVersion","cacheAsBitmap","blendMode","mask","uid"};JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;bool failed=false;
 for(unsigned i=0;i<KeyCount;i++){JSAtom a=JS_NewAtom(c,names[i]);if(!a){failed=true;break;}data[Strings+i]=JS_AtomToValue(c,a);data[Atoms+i]=JS_NewInt32(c,int32_t(a));JS_FreeAtom(c,a);if(JS_IsException(data[Strings+i])){failed=true;break;}}
 data[Runtime]=JS_DupValue(c,argv[0]);data[Normal]=JS_NewString(c,"normal");if(JS_IsException(data[Normal]))failed=true;
 JSValue result=failed?JS_EXCEPTION:JS_NewCFunctionData(c,run,1,0,DataCount,data);for(auto v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
 JSValue o=JS_NewObject(c);JS_SetPropertyStr(c,o,"calls",JS_NewUint32(c,calls));JS_SetPropertyStr(c,o,"completed",JS_NewUint32(c,completed));JS_SetPropertyStr(c,o,"array_fallbacks",JS_NewUint32(c,array_fallbacks));JS_SetPropertyStr(c,o,"pure_declines",JS_NewUint32(c,declines));return o;
}
static void bind(JSContext* c){calls=completed=array_fallbacks=declines=0;JSValue g=JS_GetGlobalObject(c);JS_SetPropertyStr(c,g,"nativeDisplaySuffixFactory",JS_NewCFunction(c,factory,"nativeDisplaySuffixFactory",1));JS_SetPropertyStr(c,g,"nativeDisplaySuffixStats",JS_NewCFunction(c,stats,"nativeDisplaySuffixStats",0));JS_FreeValue(c,g);}
}
