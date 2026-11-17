#ifndef DE_ARRAY_OPS_H
#define DE_ARRAY_OPS_H
#include "quickjs.h"
#include <math.h>
static JSValue de_move_array(JSContext* c,JSValueConst self,int argc,JSValueConst* argv){
    (void)self;double from,to;
    if(argc==3&&JS_VALUE_GET_TAG(argv[1])==JS_TAG_INT&&JS_VALUE_GET_TAG(argv[2])==JS_TAG_INT){
        int32_t a=JS_VALUE_GET_INT(argv[1]),b=JS_VALUE_GET_INT(argv[2]);
        return a<0||b<0?JS_FALSE:JS_NewBool(c,JS_TryMoveArrayElement(c,argv[0],(uint32_t)a,(uint32_t)b));
    }
    if(argc!=3||!JS_IsNumber(argv[1])||!JS_IsNumber(argv[2]))return JS_FALSE;
    if(JS_ToFloat64(c,&from,argv[1])||JS_ToFloat64(c,&to,argv[2]))return JS_EXCEPTION;
    if(!isfinite(from)||!isfinite(to)||from<0||to<0||from>2147483647||to>2147483647||from!=floor(from)||to!=floor(to))return JS_FALSE;
    return JS_NewBool(c,JS_TryMoveArrayElement(c,argv[0],(uint32_t)from,(uint32_t)to));
}
static void de_array_ops_bind(JSContext* c){
    JSValue global=JS_GetGlobalObject(c);
    JS_SetPropertyStr(c,global,"nativeMoveArrayElement",JS_NewCFunction(c,de_move_array,"nativeMoveArrayElement",3));
    JS_FreeValue(c,global);
}
#endif
