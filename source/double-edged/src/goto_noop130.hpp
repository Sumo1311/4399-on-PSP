#pragma once
#include "quickjs.h"
#include "source_rounding.hpp"
namespace degoto130 {
enum Key{Frame,Playing,Queued,Skip,MotionFlag,Running,Definition,Frames,Length,Total,Number,Finite,Math,Floor,Min,Max,KeyCount};
enum Data{Original,Resolve,Integer,Motion,Global,TotalGetter,Names,Atoms=Names+KeyCount,Count=Atoms+KeyCount};
static unsigned calls=0,fast=0,fallbacks=0,factories=0,rejected=0;
static JSAtom key(JSValue* d,unsigned i){return JSAtom(uint32_t(JS_VALUE_GET_INT(d[Atoms+i])));}
static double number(JSValueConst value){return JS_VALUE_GET_TAG(value)==JS_TAG_INT?JS_VALUE_GET_INT(value):JS_VALUE_GET_FLOAT64(value);}
static bool positive_frame(JSValueConst value,uint32_t& frame){if(JS_VALUE_GET_TAG(value)==JS_TAG_INT){int32_t n=JS_VALUE_GET_INT(value);if(n<1)return false;frame=uint32_t(n);return true;}if(!JS_IsNumber(value))return false;double n=number(value);if(!(n>=1&&n<=4294967295.&&double(uint32_t(n))==n))return false;frame=uint32_t(n);return true;}
static bool same_frame(JSValueConst value,uint32_t frame){if(JS_VALUE_GET_TAG(value)==JS_TAG_INT){int32_t n=JS_VALUE_GET_INT(value);return n>0&&uint32_t(n)==frame;}return JS_IsNumber(value)&&number(value)==double(frame);}
static bool total_at_least(JSValueConst value,uint32_t frame){if(JS_VALUE_GET_TAG(value)==JS_TAG_INT){int32_t n=JS_VALUE_GET_INT(value);return n>0&&uint32_t(n)>=frame;}return JS_IsNumber(value)&&number(value)>=double(frame);}
static bool own(JSValueConst o,JSValue* d,unsigned k,JSValue& value){return JS_TryOwnDataPropertyBorrowed(o,key(d,k),&value)==1;}
static bool intrinsics(JSContext* c,JSValue* d){
 JSValue n,f,m,floor,min,max;
 return own(d[Global],d,Number,n)&&JS_IsSourceRoundingIntrinsic(c,n,0)&&own(d[Global],d,Finite,f)&&JS_IsSourceRoundingIntrinsic(c,f,1)&&own(d[Global],d,Math,m)&&own(m,d,Floor,floor)&&JS_IsSourceRoundingIntrinsic(c,floor,2)&&own(m,d,Min,min)&&JS_IsSourceRoundingIntrinsic(c,min,4)&&own(m,d,Max,max)&&JS_IsSourceRoundingIntrinsic(c,max,5);
}
static bool range(JSContext* c,JSValueConst o,uint32_t frame,JSValue* d){
 JSValue value,getter;int status=JS_TryResolvedProperty130(o,key(d,Total),&value,&getter);
 if(status==1)return total_at_least(value,frame);
 if(status!=2||!JS_IsStrictEqual(c,getter,d[TotalGetter]))return false;
 JSValue definition,frames,length;if(!own(o,d,Definition,definition))return false;
 if(!JS_ToBool(c,definition))return frame==1;
 status=JS_TryDataPropertyBorrowed(definition,key(d,Frames),&frames);if(!status)return false;
 if(!JS_ToBool(c,frames))return frame==1;
 uint32_t count=0;if(JS_TryPlainArrayLength(frames,&count))return count>=frame;
 return JS_TryDataPropertyBorrowed(frames,key(d,Length),&length)==1&&total_at_least(length,frame);
}
static JSValue run(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int,JSValue* d){
 ++calls;auto original=[&](){++fallbacks;return JS_Call(c,d[Original],self,argc,argv);};
 uint32_t frame=0;if(argc<3||!positive_frame(argv[1],frame)||!JS_IsBool(argv[2]))return original();
 JSValue current,playing,queued,skip,motion,running;
 if(!own(argv[0],d,Frame,current)||!same_frame(current,frame)||!own(argv[0],d,Playing,playing)||!JS_IsBool(playing)||JS_VALUE_GET_BOOL(playing)!=JS_VALUE_GET_BOOL(argv[2])||!own(argv[0],d,Queued,queued)||!JS_IsNull(queued)||!own(argv[0],d,Skip,skip)||!JS_IsBool(skip)||!own(argv[0],d,MotionFlag,motion)||!JS_IsBool(motion)||JS_VALUE_GET_BOOL(motion))return original();
 if(!JS_TryDataPropertyBorrowed(argv[0],key(d,Running),&running)||(!JS_IsBool(running)&&!JS_IsUndefined(running)))return original();
 int resolver=JS_CheckClosureBinding(c,d[Original],"resolveFrame",d[Resolve]);if(resolver<0)return JS_EXCEPTION;if(!resolver)return original();
 int invalidator=JS_CheckClosureBinding(c,d[Original],"invalidateMotion",d[Motion]);if(invalidator<0)return JS_EXCEPTION;if(!invalidator||!JS_CheckSingleClosureValue(c,d[Resolve],d[Integer])||!intrinsics(c,d)||!range(c,argv[0],frame,d))return original();
 // Both ordinary data writes are preflighted together. No callback, allocation,
 // reference destruction or partial write can occur on this path.
 JSAtom keys[]={key(d,Queued),key(d,Skip)};JSValue values[]={JS_NULL,JS_TRUE};
 if(!JS_TryWriteOwnPrimitives(argv[0],keys,values,2))return original();++fast;return JS_UNDEFINED;
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
 ++factories;auto original=[&](){++rejected;return argc?JS_DupValue(c,argv[0]):JS_NULL;};
 if(argc!=6||!JS_CheckSourceFunction(c,argv[0],576,1923388326u)||!JS_CheckSourceFunction(c,argv[1],216,3205764634u)||!JS_CheckSourceClosureCount(argv[1],1)||!JS_CheckSingleClosureValue(c,argv[1],argv[2])||!JS_CheckSourceFunction(c,argv[3],96,839924162u)||!JS_CheckSourceClosureCount(argv[3],0))return original();
 JSValue global=JS_GetGlobalObject(c);bool same=JS_IsStrictEqual(c,global,argv[4]);JS_FreeValue(c,global);
 if(!same||!JS_CheckCFunctionDataValue(c,argv[2],desourcerounding::run,0,desourcerounding::Global,argv[4]))return original();
 int yes=JS_CheckClosureBinding(c,argv[0],"resolveFrame",argv[1]);if(yes<0)return JS_EXCEPTION;if(!yes)return original();yes=JS_CheckClosureBinding(c,argv[0],"invalidateMotion",argv[3]);if(yes<0)return JS_EXCEPTION;if(!yes)return original();
 JSAtom total=JS_NewAtom(c,"totalFrames");if(!total)return JS_EXCEPTION;JSValue value,getter;int status=JS_TryResolvedProperty130(argv[5],total,&value,&getter);JS_FreeAtom(c,total);
 if(status!=2||!JS_CheckSourceFunction(c,getter,94,3558338223u)||!JS_CheckSourceClosureCount(getter,0))return original();
 JSValue data[Count];for(auto& v:data)v=JS_UNDEFINED;for(unsigned i=0;i<5;++i)data[i]=JS_DupValue(c,argv[i]);data[TotalGetter]=JS_DupValue(c,getter);
 const char* names[]={"_frame","_playing","_queuedGoto","_skipAdvance","_motionValid","_runningScript","_definition","frames","length","totalFrames","Number","isFinite","Math","floor","min","max"};bool ready=true;
 for(unsigned i=0;i<KeyCount;++i){JSAtom a=JS_NewAtom(c,names[i]);if(!a){ready=false;break;}data[Names+i]=JS_AtomToValue(c,a);data[Atoms+i]=JS_NewInt32(c,int32_t(a));JS_FreeAtom(c,a);if(JS_IsException(data[Names+i])){ready=false;break;}}
 JSValue result=ready?JS_NewCFunctionData(c,run,3,0,Count,data):JS_EXCEPTION;for(auto v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){JSValue out=JS_NewObject(c);const char* names[]={"calls","fast","fallbacks","factories","rejected"};unsigned values[]={calls,fast,fallbacks,factories,rejected};for(unsigned i=0;i<5;++i)JS_SetPropertyStr(c,out,names[i],JS_NewUint32(c,values[i]));return out;}
static void bind(JSContext* c){calls=fast=fallbacks=factories=rejected=0;JSValue g=JS_GetGlobalObject(c);JS_SetPropertyStr(c,g,"nativeGotoNoopFactory",JS_NewCFunction(c,factory,"nativeGotoNoopFactory",6));JS_SetPropertyStr(c,g,"nativeGotoNoopStats",JS_NewCFunction(c,stats,"nativeGotoNoopStats",0));JS_FreeValue(c,g);}
}
