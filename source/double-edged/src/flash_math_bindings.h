#ifndef DOUBLE_EDGED_FLASH_MATH_BINDINGS_H
#define DOUBLE_EDGED_FLASH_MATH_BINDINGS_H
#include "quickjs.h"
#include "flash_math.h"
static int de_math_read(JSContext* context,JSValueConst value,unsigned count,double* result) {
    if(JS_ReadNumericArray(value,result,count))return 0;
    for(unsigned i=0;i<count;i++){
        JSValue field=JS_GetPropertyUint32(context,value,i);
        int error=JS_IsException(field)||JS_ToFloat64(context,result+i,field);
        JS_FreeValue(context,field);if(error)return -1;
    }
    return 0;
}
static JSValue de_math_array(JSContext* context,unsigned count,const double* values) {
    JSValue result=JS_NewArray(context);if(JS_IsException(result))return result;
    for(unsigned i=0;i<count;i++)if(JS_SetPropertyUint32(context,result,i,JS_NewFloat64(context,values[i]))<0){JS_FreeValue(context,result);return JS_EXCEPTION;}
    return result;
}
static JSValue de_native_matrix(JSContext* context,JSValueConst self,int argc,JSValueConst* argv) {
    (void)self;if(argc<2)return JS_ThrowTypeError(context,"Two matrices required");
    double a[6],b[6],result[6];if(de_math_read(context,argv[0],6,a)||de_math_read(context,argv[1],6,b))return JS_EXCEPTION;
    de_matrix_values(a,b,result);return de_math_array(context,6,result);
}
static JSValue de_native_bounds(JSContext* context,JSValueConst self,int argc,JSValueConst* argv) {
    (void)self;if(argc<2)return JS_ThrowTypeError(context,"Bounds and matrix required");
    double bounds[4],matrix[6],result[4];if(de_math_read(context,argv[0],4,bounds)||de_math_read(context,argv[1],6,matrix))return JS_EXCEPTION;
    de_bounds_values(bounds,matrix,result);return de_math_array(context,4,result);
}
static JSValue de_native_matrix_chain(JSContext* context,JSValueConst self,int argc,JSValueConst* argv) {
    (void)self;if(!argc)return JS_ThrowTypeError(context,"Matrix chain required");
    JSValue length=JS_GetPropertyStr(context,argv[0],"length");uint32_t count=0;int error=JS_IsException(length)||JS_ToUint32(context,&count,length);JS_FreeValue(context,length);if(error)return JS_EXCEPTION;
    if(!count||count>256)return JS_ThrowRangeError(context,"Invalid matrix chain length");
    double result[6];JSValue first=JS_GetPropertyUint32(context,argv[0],0);error=de_math_read(context,first,6,result);JS_FreeValue(context,first);if(error)return JS_EXCEPTION;
    for(uint32_t i=1;i<count;i++){double parent[6],next[6];JSValue value=JS_GetPropertyUint32(context,argv[0],i);error=de_math_read(context,value,6,parent);JS_FreeValue(context,value);if(error)return JS_EXCEPTION;de_matrix_values(parent,result,next);for(unsigned j=0;j<6;j++)result[j]=next[j];}
    return de_math_array(context,6,result);
}
// Validate an already computed ancestry stamp. No getters, proxy traps,
// coercion callbacks or cache writes occur on this optional fast path.
// The source walker remains responsible for every changed/unusual chain.
static JSValue de_native_world_path(JSContext* context,JSValueConst self,int argc,JSValueConst* argv,int magic,JSValue* data) {
    (void)self;(void)magic;
    uint32_t count=0;
    if(argc!=4||!JS_IsNumber(argv[2])||JS_ToUint32(context,&count,argv[2])||!count||count>512||(count&1))return JS_FALSE;
    double path[512];
    if(!JS_ReadNumericArray(argv[1],path,count))return JS_FALSE;
    // data[0..3] owns the interned strings; data[4..7] holds their atom IDs.
    JSAtom fields[4];
    for(unsigned i=0;i<4;i++)fields[i]=(JSAtom)JS_VALUE_GET_INT(data[4+i]);
    // The call's rooted first argument owns this unchanged strong parent chain.
    // Only ordinary data fields are read. No allocation, getters, proxy traps,
    // user callbacks or GC can occur while these local values are borrowed.
    JSValue node=argv[0];
    for(unsigned i=0;i<count;i+=2){
        JSValue uid,version,parent,cached_parent;
        if(JS_TryOwnDataPropertyBorrowed(node,fields[0],&uid)!=1||
           JS_TryOwnDataPropertyBorrowed(node,fields[1],&version)!=1||
           JS_TryOwnDataPropertyBorrowed(node,fields[2],&parent)!=1||
           JS_TryOwnDataPropertyBorrowed(node,fields[3],&cached_parent)!=1)return JS_FALSE;
        double identity=0,revision=0;
        if(!JS_IsNumber(uid)||!JS_IsNumber(version)||
           JS_ToFloat64(context,&identity,uid)||JS_ToFloat64(context,&revision,version)||
           identity!=path[i]||revision!=path[i+1]||!JS_IsStrictEqual(context,parent,cached_parent))return JS_FALSE;
        node=parent;
        if(i+2<count&&(JS_IsNull(node)||JS_IsStrictEqual(context,node,argv[3])))return JS_FALSE;
    }
    return JS_NewBool(context,JS_IsNull(node)||JS_IsStrictEqual(context,node,argv[3]));
}
static void de_flash_math_bind(JSContext* context) {
    JSValue global=JS_GetGlobalObject(context);
    JS_SetPropertyStr(context,global,"nativeMatrixMultiply",JS_NewCFunction(context,de_native_matrix,"nativeMatrixMultiply",2));
    JS_SetPropertyStr(context,global,"nativeBoundsTransform",JS_NewCFunction(context,de_native_bounds,"nativeBoundsTransform",2));
    JS_SetPropertyStr(context,global,"nativeMatrixChain",JS_NewCFunction(context,de_native_matrix_chain,"nativeMatrixChain",1));
    const char* fields[]={"_uid","_matrixVersion","parent","_worldParent"};JSValue data[8];
    for(unsigned i=0;i<8;i++)data[i]=JS_UNDEFINED;
    int keys_ready=1;
    for(unsigned i=0;i<4&&keys_ready;i++){
        JSAtom atom=JS_NewAtom(context,fields[i]);
        if(!atom){keys_ready=0;break;}
        data[i]=JS_AtomToString(context,atom);
        data[4+i]=JS_NewInt32(context,(int32_t)atom);
        JS_FreeAtom(context,atom);
        if(JS_IsException(data[i]))keys_ready=0;
    }
    if(keys_ready){
        JSValue function=JS_NewCFunctionData(context,de_native_world_path,4,0,8,data);
        if(!JS_IsException(function))JS_SetPropertyStr(context,global,"nativeWorldPathCurrent",function);
    }
    for(unsigned i=0;i<8;i++)JS_FreeValue(context,data[i]);
    JS_FreeValue(context,global);
}
#endif
