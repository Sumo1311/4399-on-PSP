#pragma once
#include "quickjs.h"
#include <cmath>
#include <cstdint>
namespace decpu {
enum Key {Active,Z,Mode,Kill,Down,Hit,Clip,Collided,Object,Math,Abs,KeyCount};
enum Root {Original=2*KeyCount,Tail,Character,Cyclopse,Global,SourceObject,SourceMath,SourceAbs,DataCount};
static uint64_t calls=0,completed=0,prefix_fallbacks=0,tail_fallbacks=0,pairs=0;
static uint64_t tail_reasons[6]{};
struct Value {
    JSContext* c;JSValue v;
    Value(JSContext* context,JSValue value):c(context),v(value){}
    ~Value(){JS_FreeValue(c,v);}
    Value(const Value&)=delete;Value& operator=(const Value&)=delete;
    JSValue take(){JSValue result=v;v=JS_UNDEFINED;return result;}
};
static JSAtom key(JSValue* d,unsigned k){return JSAtom(JS_VALUE_GET_INT(d[KeyCount+k]));}
static bool plain(JSValue* d,JSValueConst o,unsigned k,JSValue* out){return JS_TryDataPropertyBorrowed(o,key(d,k),out)==1;}
static bool own(JSValue* d,JSValueConst o,unsigned k,JSValue* out){return JS_TryOwnDataPropertyBorrowed(o,key(d,k),out)==1;}
static bool numeric(JSContext* c,JSValue v,double* number){return JS_IsNumber(v)&&JS_ToFloat64(c,number,v)==0;}
static bool math_current(JSContext* c,JSValue* d){
    JSValue math,abs;return own(d,d[Global],Math,&math)&&JS_IsStrictEqual(c,math,d[SourceMath])&&
        own(d,math,Abs,&abs)&&JS_IsStrictEqual(c,abs,d[SourceAbs]);
}
static JSValue assign(JSContext* c,JSValue* d,JSValueConst result,unsigned k,JSValue value){
    // Source assignment is non-strict,including inherited setters/read-only data.
    return JS_SetPropertyValue(c,result,JS_DupValue(c,d[k]),value,0)<0?JS_EXCEPTION:JS_UNDEFINED;
}
static JSValue run(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int,JSValue* d){
    ++calls;JSValue object,z,mode,kill,down;double z_number,mode_number,down_number;
    if(argc!=1||!own(d,d[Global],Object,&object)||!JS_IsStrictEqual(c,object,d[SourceObject])||
       !plain(d,argv[0],Z,&z)||!numeric(c,z,&z_number)||
       !plain(d,argv[0],Mode,&mode)||!numeric(c,mode,&mode_number)||
       !plain(d,argv[0],Kill,&kill)||!JS_IsObject(kill)||
       !plain(d,d[Character],Down,&down)||!numeric(c,down,&down_number)){
        ++prefix_fallbacks;return JS_Call(c,d[Original],self,argc,argv);
    }
    // These are the same hoisted values as the compiled source. Hold all
    // references across arbitrary hit-test/prototype callbacks and collection.
    Value held_z(c,JS_DupValue(c,z)),held_mode(c,JS_DupValue(c,mode));
    Value held_kill(c,JS_DupValue(c,kill)),held_down(c,JS_DupValue(c,down));
    Value result(c,JS_NewObject(c));if(JS_IsException(result.v))return result.take();
    int cyclopse=JS_IsInstanceOf(c,argv[0],d[Cyclopse]);if(cyclopse<0)return JS_EXCEPTION;
    bool collided=false;uint32_t index=0;
    if(!cyclopse)for(;;){
        JSValue active,other,z_other,mode_other;uint32_t length=0;double other_z,other_mode;
        if(!own(d,self,Active,&active)){++tail_reasons[0];goto source_tail;}
        if(!JS_TryPlainArrayLength(active,&length)){++tail_reasons[1];goto source_tail;}
        if(index>=length)break;
        if(!math_current(c,d)){++tail_reasons[2];goto source_tail;}
        if(!JS_TryPlainArrayValueBorrowed(active,index,&other)||!JS_IsObject(other)){++tail_reasons[3];goto source_tail;}
        if(!plain(d,other,Z,&z_other)||!numeric(c,z_other,&other_z)){++tail_reasons[4];goto source_tail;}
        if(!plain(d,other,Mode,&mode_other)||!numeric(c,mode_other,&other_mode)){++tail_reasons[5];goto source_tail;}
        if(std::fabs(z_number-other_z)<15&&other_mode!=down_number&&mode_number!=down_number&&
           !JS_IsStrictEqual(c,argv[0],other)){
            Value held_other(c,JS_DupValue(c,other));
            int excluded=JS_IsInstanceOf(c,held_other.v,d[Cyclopse]);if(excluded<0)return JS_EXCEPTION;
            if(!excluded){
                // Keep the source's method lookup -> argument lookup -> call
                // sequence. Callbacks can mutate the list and future fields;
                // the next iteration reads them again instead of using a snapshot.
                Value method(c,JS_GetProperty(c,held_kill.v,key(d,Hit)));if(JS_IsException(method.v))return JS_EXCEPTION;
                Value other_kill(c,JS_GetProperty(c,held_other.v,key(d,Kill)));if(JS_IsException(other_kill.v))return JS_EXCEPTION;
                Value hit(c,JS_Call(c,method.v,held_kill.v,1,&other_kill.v));if(JS_IsException(hit.v))return JS_EXCEPTION;
                ++pairs;
                if(JS_ToBool(c,hit.v)){
                    collided=true;JSValue assigned=assign(c,d,result.v,Clip,JS_DupValue(c,held_other.v));
                    if(JS_IsException(assigned))return assigned;
                }
            }
        }
        ++index;
        continue;
    source_tail:
        ++tail_fallbacks;
        {JSValue args[]={argv[0],result.v,JS_NewBool(c,collided),JS_NewUint32(c,index),held_z.v,held_mode.v,held_kill.v,held_down.v,d[Cyclopse]};
        return JS_Call(c,d[Tail],self,9,args);}
    }
    JSValue assigned=assign(c,d,result.v,Collided,JS_NewBool(c,collided));if(JS_IsException(assigned))return assigned;
    ++completed;return result.take();
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* trusted){
    if(argc!=4||!JS_IsFunction(c,argv[0])||!JS_IsFunction(c,argv[1])||!JS_IsObject(argv[2])||!JS_IsObject(argv[3]))
        return JS_ThrowTypeError(c,"Original CPU collision function,source continuation and classes required");
    const char* names[]={"activeBaddies","zPos","mode","killArea","DOWN","hitTestObject","clip","collided","Object","Math","abs"};
    JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;bool failed=false;
    for(unsigned i=0;i<KeyCount&&!failed;i++){
        JSAtom atom=JS_NewAtom(c,names[i]);if(!atom){failed=true;break;}
        data[i]=JS_AtomToValue(c,atom);data[KeyCount+i]=JS_NewInt32(c,int32_t(atom));JS_FreeAtom(c,atom);failed=JS_IsException(data[i]);
    }
    for(unsigned i=0;i<4;i++)data[Original+i]=JS_DupValue(c,argv[i]);
    for(unsigned i=0;i<4;i++)data[Global+i]=JS_DupValue(c,trusted[i]);
    JSValue result=failed?JS_EXCEPTION:JS_NewCFunctionData(c,run,1,0,DataCount,data);
    for(auto& v:data)JS_FreeValue(c,v);
    if(!JS_IsException(result)&&JS_DefinePropertyValueStr(c,result,"name",JS_GetPropertyStr(c,argv[0],"name"),JS_PROP_CONFIGURABLE)<0){JS_FreeValue(c,result);return JS_EXCEPTION;}
    return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
    JSValue out=JS_NewObject(c);if(JS_IsException(out))return out;
    const char* keys[]={"calls","completed","prefix_fallbacks","tail_fallbacks","pairs"};
    uint64_t values[]={calls,completed,prefix_fallbacks,tail_fallbacks,pairs};
    for(unsigned i=0;i<5;i++)JS_SetPropertyStr(c,out,keys[i],JS_NewFloat64(c,double(values[i])));
    JSValue reasons=JS_NewArray(c);for(unsigned i=0;i<6;i++)JS_SetPropertyUint32(c,reasons,i,JS_NewFloat64(c,double(tail_reasons[i])));JS_SetPropertyStr(c,out,"tail_reasons",reasons);return out;
}
static void bind(JSContext* c){
    calls=completed=prefix_fallbacks=tail_fallbacks=pairs=0;for(auto& n:tail_reasons)n=0;
    JSValue g=JS_GetGlobalObject(c),obj=JS_GetPropertyStr(c,g,"Object"),math=JS_GetPropertyStr(c,g,"Math"),abs=JS_GetPropertyStr(c,math,"abs");
    JSValue trusted[]={g,obj,math,abs};
    JS_SetPropertyStr(c,g,"nativeCpuCollisionFactory",JS_NewCFunctionData(c,factory,4,0,4,trusted));
    JS_SetPropertyStr(c,g,"nativeCpuCollisionStats",JS_NewCFunction(c,stats,"nativeCpuCollisionStats",0));
    JS_FreeValue(c,abs);JS_FreeValue(c,math);JS_FreeValue(c,obj);JS_FreeValue(c,g);
}
}
