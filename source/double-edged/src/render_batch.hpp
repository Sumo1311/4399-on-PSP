#pragma once
#include "quickjs.h"
namespace derenderbatch {
enum ChangedRoot {ChangedSource,ChangedNative,ChangedDirty,ChangedFlags,ChangedCount};
static JSValue changed(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int,JSValue* data){
    if(argc!=2||JS_VALUE_GET_TAG(argv[1])!=JS_TAG_INT)return JS_Call(c,data[ChangedSource],self,argc,argv);
    JSAtom atom=JS_ValueToAtom(c,data[ChangedFlags]);if(!atom)return JS_EXCEPTION;
    JSValue old;int found=JS_TryOwnDataPropertyCachedBorrowed(c,argv[0],atom,&old);
    if(!found)found=JS_TryDataPropertyBorrowed(argv[0],atom,&old);
    bool primitive=JS_IsNumber(old)||JS_IsBool(old)||JS_IsNull(old)||JS_IsUndefined(old);
    if(!found||!primitive){JS_FreeAtom(c,atom);return JS_Call(c,data[ChangedSource],self,argc,argv);}
    int32_t prior=0,flags=JS_VALUE_GET_INT(argv[1]);JS_ToInt32(c,&prior,old);
    if((prior&flags)==flags){JS_FreeAtom(c,atom);return JS_UNDEFINED;}
    if(!JS_ToBool(c,old)){
        JSValue push=JS_GetPropertyStr(c,data[ChangedDirty],"push");
        if(JS_IsException(push)){JS_FreeAtom(c,atom);return JS_EXCEPTION;}
        JSValue result=JS_Call(c,push,data[ChangedDirty],1,argv);JS_FreeValue(c,push);
        if(JS_IsException(result)){JS_FreeAtom(c,atom);return JS_EXCEPTION;}JS_FreeValue(c,result);
    }
    int result=JS_SetPropertyValue(c,argv[0],JS_DupValue(c,data[ChangedFlags]),JS_NewInt32(c,prior|flags),0);
    JS_FreeAtom(c,atom);return result<0?JS_EXCEPTION:JS_UNDEFINED;
}
static JSValue changedFactory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    uint32_t n=0;
    if(argc!=ChangedCount||!JS_IsFunction(c,argv[0])||!JS_ToBool(c,argv[1])||!JS_TryPlainArrayLength(argv[2],&n)||!JS_IsSymbol(argv[3]))return argc?JS_DupValue(c,argv[0]):JS_UNDEFINED;
    return JS_NewCFunctionData(c,changed,2,0,ChangedCount,argv);
}
enum Root {Source,Runtime,Dirty,Flags,NativeUpdate,Count};
template<JSCFunction* Update>
static JSValue flush(JSContext* c,JSValueConst,int,JSValueConst*,int,JSValue* data){
    uint32_t count=0;
    if(!JS_TryPlainArrayLength(data[Dirty],&count))return JS_Call(c,data[Source],JS_UNDEFINED,0,nullptr);
    JSAtom flag=JS_ValueToAtom(c,data[Flags]);if(!flag)return JS_EXCEPTION;
    JSValue last=JS_UNDEFINED,result=JS_UNDEFINED;bool error=false;
    for(uint32_t i=0;;++i){
        // Array.length is its nonconfigurable own data property. Reload it
        // after every callback, as the source for-loop does.
        if(!JS_TryPlainArrayLength(data[Dirty],&count)){error=true;JS_ThrowInternalError(c,"Private render array changed kind");break;}
        if(i>=count)break;
        JSValue node=JS_GetPropertyUint32(c,data[Dirty],i);
        if(JS_IsException(node)){error=true;break;}
        // Keep the previous loop variable alive while evaluating the next
        // indexed getter, including during user-triggered garbage collection.
        JS_FreeValue(c,last);last=node;
        JSValue flags=JS_GetProperty(c,last,flag);
        if(JS_IsException(flags)){error=true;break;}
        JSValue args[]={last,flags};JSValue update=Update(c,JS_UNDEFINED,2,args);JS_FreeValue(c,flags);
        if(JS_IsException(update)){error=true;break;}JS_FreeValue(c,update);
        if(JS_SetPropertyValue(c,last,JS_DupValue(c,data[Flags]),JS_NewInt32(c,0),0)<0){error=true;break;}
    }
    if(!error&&JS_SetPropertyValue(c,data[Dirty],JS_NewString(c,"length"),JS_NewInt32(c,0),0)<0)error=true;
    if(!error){
        // The source keeps its last `o` local alive through return-expression
        // getters. Do the same before releasing the native loop variable.
        JSValue root=JS_GetPropertyStr(c,data[Runtime],"root");
        if(JS_IsException(root))error=true;
        else{result=JS_GetPropertyStr(c,root,"_uid");JS_FreeValue(c,root);error=JS_IsException(result);}
    }
    JS_FreeValue(c,last);JS_FreeAtom(c,flag);return error?JS_EXCEPTION:result;
}
template<JSCFunction* Update>
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* expected){
    uint32_t n=0;
    if(argc!=Count||!JS_IsFunction(c,argv[Source])||!JS_IsObject(argv[Runtime])||
       !JS_TryPlainArrayLength(argv[Dirty],&n)||!JS_IsSymbol(argv[Flags])||
       !JS_IsStrictEqual(c,argv[NativeUpdate],expected[0]))return argc?JS_DupValue(c,argv[0]):JS_UNDEFINED;
    return JS_NewCFunctionData(c,flush<Update>,0,0,Count,argv);
}
template<JSCFunction* Update>
static void bind(JSContext* c,JSValueConst expected){
    JSValue data[]={expected};JSValue fn=JS_NewCFunctionData(c,factory<Update>,Count,0,1,data),global=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,global,"nativeRenderBatchFactory",fn);JS_FreeValue(c,global);
    global=JS_GetGlobalObject(c);JS_SetPropertyStr(c,global,"nativeRenderChangedFactory",JS_NewCFunction(c,changedFactory,"nativeRenderChangedFactory",ChangedCount));JS_FreeValue(c,global);
}
}
