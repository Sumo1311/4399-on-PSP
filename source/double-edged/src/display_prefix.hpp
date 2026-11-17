#pragma once
#include "quickjs.h"
#include "object_hit.hpp"
#include "random_step.hpp"
#include "stage_getter.hpp"
#include <cstdint>
#include <climits>
namespace dedisplayprefix {
enum Key {Uid,Name,Parent,Placed,Symbol,Definition,Matrix,MatrixVersion,Color,ColorVersion,
          Counter,Movie,Defs,PendingSymbol,PendingMatrix,PendingColor,KeyCount};
enum {Runtime=2*KeyCount,DataCount};
static unsigned calls=0,completed=0,fallbacks=0,registered_prototypes=0;
// Called only after the complete original classes file has been authenticated.
// These constructors/prototypes are independently retained by the original
// DoubleEdgedEntry.classes and global flash.display roots for the VM lifetime.
// Synthesized Vector prototypes and arbitrary caller prototypes are excluded.
static bool retain_source_roots(JSContext* c,bool authenticated){
    if(!authenticated)return true;
    JSValue global=JS_GetGlobalObject(c),entry=JS_GetPropertyStr(c,global,"DoubleEdgedEntry"),classes=JS_GetPropertyStr(c,entry,"classes");
    auto mark=[&](JSValueConst ctor){
        if(!JS_IsFunction(c,ctor))return;
        JSAtom atom=JS_NewAtom(c,"prototype");JSValue proto;
        if(atom&&JS_TryDataPropertyBorrowed(ctor,atom,&proto)==1&&JS_IsObject(proto)){JS_RegisterRetainedInitializerPrototype(proto);++registered_prototypes;}
        JS_FreeAtom(c,atom);
    };
    bool valid=!JS_IsException(entry)&&!JS_IsException(classes);uint32_t length=0;
    if(valid){JSValue v=JS_GetPropertyStr(c,classes,"length");valid=!JS_IsException(v)&&!JS_ToUint32(c,&length,v);JS_FreeValue(c,v);}
    for(unsigned i=0;valid&&i<length;i++){
        JSValue row=JS_GetPropertyUint32(c,classes,i),ctor=JS_GetPropertyUint32(c,row,1);
        valid=!JS_IsException(row)&&!JS_IsException(ctor);if(valid)mark(ctor);JS_FreeValue(c,ctor);JS_FreeValue(c,row);
    }
    JSValue flash=JS_GetPropertyStr(c,global,"flash"),display=JS_GetPropertyStr(c,flash,"display");
    const char* names[]={"DisplayObject","DisplayObjectContainer","Sprite","MovieClip","Stage","TextField"};
    for(const char* name:names){JSValue ctor=JS_GetPropertyStr(c,display,name);if(JS_IsException(ctor))valid=false;else mark(ctor);JS_FreeValue(c,ctor);}
    // TextField is exported under flash.text in the compatibility runtime.
    JSValue text=JS_GetPropertyStr(c,flash,"text"),field=JS_GetPropertyStr(c,text,"TextField");if(!JS_IsException(field))mark(field);else valid=false;
    JS_FreeValue(c,field);JS_FreeValue(c,text);JS_FreeValue(c,display);JS_FreeValue(c,flash);JS_FreeValue(c,classes);JS_FreeValue(c,entry);JS_FreeValue(c,global);return valid;
}
static JSAtom key(JSValue* data,unsigned i){return JSAtom(JS_VALUE_GET_INT(data[KeyCount+i]));}
static bool own(JSContext* c,JSValue* data,JSValueConst o,unsigned i,JSValue* out){
    return JS_TryOwnDataPropertyCachedBorrowed(c,o,key(data,i),out)==1;
}
static JSValue run(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* data){
    ++calls;auto no=[&](){++fallbacks;return JS_FALSE;};
    if(argc!=2||!JS_IsObject(argv[0])||!JS_IsObject(argv[1])||JS_IsStrictEqual(c,argv[0],argv[1]))return no();
    JSValue uid,movie,defs,name,parent,symbol,matrix,color,definition=JS_UNDEFINED;double symbol_id=0;
    if(!own(c,data,data[Runtime],Counter,&uid)||JS_VALUE_GET_TAG(uid)!=JS_TAG_INT||JS_VALUE_GET_INT(uid)<0||JS_VALUE_GET_INT(uid)==INT_MAX||
       !own(c,data,argv[1],Name,&name)||!own(c,data,argv[1],Parent,&parent)||
       !own(c,data,argv[1],PendingSymbol,&symbol)||!JS_IsNumber(symbol)||JS_ToFloat64(c,&symbol_id,symbol)||!(symbol_id>=0&&symbol_id<=4294967295.)||symbol_id!=uint32_t(symbol_id)||
       !own(c,data,argv[1],PendingMatrix,&matrix)||!own(c,data,argv[1],PendingColor,&color)||
       !own(c,data,data[Runtime],Movie,&movie)||!JS_IsObject(movie)||!own(c,data,movie,Defs,&defs))return no();
    JSAtom id=JS_NewAtomUInt32(c,uint32_t(symbol_id));if(!id)return JS_EXCEPTION;
    int found=JS_TryOwnDataPropertyCachedBorrowed(c,defs,id,&definition);JS_FreeAtom(c,id);
    if(!found)return no();
    if(!JS_ToBool(c,definition))definition=JS_NULL;
    JSValue next=JS_NewInt32(c,JS_VALUE_GET_INT(uid)+1);
    JSValue values[]={next,name,parent,JS_FALSE,symbol,definition,matrix,JS_NewInt32(c,1),color,JS_NewInt32(c,1)};
    JSAtom keys[10];for(unsigned i=0;i<10;i++)keys[i]=key(data,i);
    int result=JS_TryInitializeOwnFields(c,argv[0],keys,values,10,data[Runtime],key(data,Counter),next);
    if(result<0)return JS_EXCEPTION;if(!result)return no();++completed;return JS_TRUE;
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    if(argc!=1||!JS_IsObject(argv[0]))return JS_NULL;
    const char* names[]={"_uid","name","parent","_placedByScript","_symbol","_definition","_matrix","_matrixVersion","_color","_colorVersion","uid","movie","definitions","symbol","matrix","color"};
    JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;bool failed=false;
    for(unsigned i=0;i<KeyCount;i++){JSAtom a=JS_NewAtom(c,names[i]);if(!a){failed=true;break;}data[i]=JS_AtomToValue(c,a);data[KeyCount+i]=JS_NewInt32(c,int32_t(a));JS_FreeAtom(c,a);if(JS_IsException(data[i])){failed=true;break;}}
    data[Runtime]=JS_DupValue(c,argv[0]);JSValue result=failed?JS_EXCEPTION:JS_NewCFunctionData(c,run,2,0,DataCount,data);
    for(auto v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
    unsigned shapes=0,bytes=JS_GetInitializerLayoutBytes(JS_GetRuntime(c),&shapes);
    JSValue out=JS_NewObject(c);JS_SetPropertyStr(c,out,"calls",JS_NewUint32(c,calls));JS_SetPropertyStr(c,out,"completed",JS_NewUint32(c,completed));JS_SetPropertyStr(c,out,"fallbacks",JS_NewUint32(c,fallbacks));JS_SetPropertyStr(c,out,"layout_hits",JS_NewUint32(c,JS_GetInitializerLayoutHits(JS_GetRuntime(c))));JS_SetPropertyStr(c,out,"layout_builds",JS_NewUint32(c,JS_GetInitializerLayoutBuilds(JS_GetRuntime(c))));JS_SetPropertyStr(c,out,"registered_prototypes",JS_NewUint32(c,registered_prototypes));JS_SetPropertyStr(c,out,"retained_shapes",JS_NewUint32(c,shapes));JS_SetPropertyStr(c,out,"retained_shape_requested_bytes",JS_NewUint32(c,bytes));JS_SetPropertyStr(c,out,"cache_metadata_bytes",JS_NewUint32(c,JS_GetInitializerLayoutMetadataBytes(JS_GetRuntime(c))));JS_SetPropertyStr(c,out,"random_queries",JS_NewFloat64(c,double(derandomstep::queries)));JS_SetPropertyStr(c,out,"random_completed",JS_NewFloat64(c,double(derandomstep::completed)));JS_SetPropertyStr(c,out,"random_fallbacks",JS_NewFloat64(c,double(derandomstep::fallbacks)));JS_SetPropertyStr(c,out,"hit_queries",JS_NewFloat64(c,double(deobjecthit::queries)));JS_SetPropertyStr(c,out,"hit_numeric",JS_NewFloat64(c,double(deobjecthit::numeric)));JS_SetPropertyStr(c,out,"hit_empty",JS_NewFloat64(c,double(deobjecthit::empty)));JS_SetPropertyStr(c,out,"hit_fallbacks",JS_NewFloat64(c,double(deobjecthit::fallbacks)));JS_SetPropertyStr(c,out,"hit_errors",JS_NewFloat64(c,double(deobjecthit::errors)));JS_SetPropertyStr(c,out,"stage_queries",JS_NewFloat64(c,double(destagegetter::queries)));JS_SetPropertyStr(c,out,"stage_hops",JS_NewFloat64(c,double(destagegetter::hops)));JS_SetPropertyStr(c,out,"stage_interrupt_polls",JS_NewFloat64(c,double(destagegetter::polls)));JS_SetPropertyStr(c,out,"stage_errors",JS_NewFloat64(c,double(destagegetter::errors)));JS_SetPropertyStr(c,out,"stage_max_hops",JS_NewUint32(c,destagegetter::max_hops));return out;
}
static void bind(JSContext* c){
    calls=completed=fallbacks=registered_prototypes=0;JSValue g=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,g,"nativeDisplayPrefixFactory",JS_NewCFunction(c,factory,"nativeDisplayPrefixFactory",1));
    JS_SetPropertyStr(c,g,"nativeDisplayPrefixStats",JS_NewCFunction(c,stats,"nativeDisplayPrefixStats",0));JS_FreeValue(c,g);
}
}
