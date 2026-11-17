#ifndef DE_DEPTH_SORT_H
#define DE_DEPTH_SORT_H
#include "quickjs.h"
#include "typed_slots.h"
static JSValue de_sort_depth(JSContext *c,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;
    if(argc!=1)return JS_FALSE;
    int result=JS_TrySortDepthArray(c,argv[0],de_slot_get);
    return result<0?JS_EXCEPTION:JS_NewBool(c,result);
}
static void de_depth_sort_bind(JSContext *c){
    JSValue global=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,global,"nativeSortDepth",JS_NewCFunction(c,de_sort_depth,"nativeSortDepth",1));
    JS_FreeValue(c,global);
}
#endif
