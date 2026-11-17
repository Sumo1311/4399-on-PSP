#ifndef DE_TYPED_SLOTS_H
#define DE_TYPED_SLOTS_H
#include "quickjs.h"
/* The generated AS3 fields retain their original JS fallback closures. Only
 * ordinary own-data dictionaries take the native path; unusual receivers,
 * inherited dictionaries and accessors use the original getter/setter. */
static JSValue de_slot_get(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int magic,JSValue* data){
    (void)argc;(void)argv;(void)magic;
    JSAtom field=JS_ValueToAtom(c,data[0]),storage=JS_ValueToAtom(c,data[1]);
    if(!field||!storage){JS_FreeAtom(c,field);JS_FreeAtom(c,storage);return JS_EXCEPTION;}
    JSValue dictionary=JS_UNDEFINED,result=JS_UNDEFINED;
    int status=JS_TryOwnDataProperty(self,storage,&dictionary);
    if(status==1)status=JS_TryOwnDataProperty(dictionary,field,&result);else status=0;
    JS_FreeValue(c,dictionary);JS_FreeAtom(c,field);JS_FreeAtom(c,storage);
    if(status==1)return result;
    if(status==2)return JS_DupValue(c,data[2]);
    return JS_Call(c,data[3],self,0,NULL);
}
static JSValue de_slot_set(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int magic,JSValue* data){
    // Preserve original coercion callbacks and exact BigInt/Symbol errors.
    if(argc&&(JS_IsObject(argv[0])||JS_IsBigInt(argv[0])||JS_IsSymbol(argv[0])))return JS_Call(c,data[4],self,argc,argv);
    JSAtom field=JS_ValueToAtom(c,data[0]),storage=JS_ValueToAtom(c,data[1]);
    if(!field||!storage){JS_FreeAtom(c,field);JS_FreeAtom(c,storage);return JS_EXCEPTION;}
    JSValue dictionary=JS_UNDEFINED,old=JS_UNDEFINED;
    int status=JS_TryOwnDataProperty(self,storage,&dictionary);
    if(status==1)status=JS_TryOwnDataProperty(dictionary,field,&old);else status=0;
    JS_FreeValue(c,old);JS_FreeAtom(c,field);JS_FreeAtom(c,storage);
    if(!status){JS_FreeValue(c,dictionary);return JS_Call(c,data[4],self,argc,argv);}
    JSValue value=argc?argv[0]:JS_UNDEFINED,number;
    if(magic){uint32_t n;if(JS_ToUint32(c,&n,value)){JS_FreeValue(c,dictionary);return JS_EXCEPTION;}number=JS_NewUint32(c,n);}
    else{int32_t n;if(JS_ToInt32(c,&n,value)){JS_FreeValue(c,dictionary);return JS_EXCEPTION;}number=JS_NewInt32(c,n);}
    // Keep non-strict assignment behavior, including read-only dictionaries.
    int error=JS_SetPropertyValue(c,dictionary,JS_DupValue(c,data[0]),number,0);
    JS_FreeValue(c,dictionary);return error<0?JS_EXCEPTION:JS_UNDEFINED;
}
static JSValue de_slot_pair(JSContext* c,JSValueConst self,int argc,JSValueConst* argv){
    (void)self;
    if(argc!=5||!JS_IsFunction(c,argv[3])||!JS_IsFunction(c,argv[4]))return JS_ThrowTypeError(c,"Generated typed accessors required");
    JSAtom key=JS_ValueToAtom(c,argv[0]),storage=JS_NewAtom(c,"_typedSlotValues");
    if(!key||!storage){JS_FreeAtom(c,key);JS_FreeAtom(c,storage);return JS_EXCEPTION;}
    JSValue data[5]={JS_AtomToValue(c,key),JS_AtomToValue(c,storage),JS_DupValue(c,argv[1]),JS_DupValue(c,argv[3]),JS_DupValue(c,argv[4])};
    JS_FreeAtom(c,key);JS_FreeAtom(c,storage);
    JSValue get=JS_NewCFunctionData(c,de_slot_get,0,0,5,data);
    JSValue set=JS_NewCFunctionData(c,de_slot_set,1,JS_ToBool(c,argv[2]),5,data);
    for(unsigned i=0;i<5;i++)JS_FreeValue(c,data[i]);
    if(JS_IsException(get)||JS_IsException(set)){JS_FreeValue(c,get);JS_FreeValue(c,set);return JS_EXCEPTION;}
    JSValue result=JS_NewArray(c);
    if(JS_IsException(result)){JS_FreeValue(c,get);JS_FreeValue(c,set);return result;}
    if(JS_SetPropertyUint32(c,result,0,get)<0){JS_FreeValue(c,set);JS_FreeValue(c,result);return JS_EXCEPTION;}
    if(JS_SetPropertyUint32(c,result,1,set)<0){JS_FreeValue(c,result);return JS_EXCEPTION;}
    return result;
}
static void de_typed_slots_bind(JSContext* c){
    JSValue global=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,global,"nativeTypedSlotPair",JS_NewCFunction(c,de_slot_pair,"nativeTypedSlotPair",5));
    JS_FreeValue(c,global);
}
#endif
