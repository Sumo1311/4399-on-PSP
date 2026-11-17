#pragma once
#include "quickjs.h"
#include <cstdint>
#include <cstddef>
#include <array>
namespace derock {
enum Key {Z,Y,Kill,Midas,List,Definition,Type,Layers,Front,Back,Heights,Middle,Hit,Object,Prototype,Clip,Frame,KeyCount};
enum Root {Source=2*KeyCount,Tail,Post,Defs,Talos,Global,ObjectConstructor,ObjectPrototype,Runtime,GamePrototype,DataCount};
static uint64_t calls=0,completed=0,prefix_fallbacks=0,tail_fallbacks=0,pairs=0;
static JSContext* program_context=nullptr;
static uint64_t cache_builds=0,cache_hits=0,scanned_elements=0,selected_elements=0;
struct Candidate{uint32_t index,type;double front,back,height;};
struct CandidateCache{JSContext* context=nullptr;void* list=nullptr;uint32_t frame=0,count=0,used=0;uint64_t epoch=0;bool ready=false;std::array<Candidate,512> items;};
static CandidateCache candidate_cache;
static void authorize(JSContext* c,const char* source,std::size_t size){uint32_t hash=2166136261u;for(std::size_t i=0;i<size;i++)hash=(hash^uint8_t(source[i]))*16777619u;program_context=size==1827379u&&hash==1357414036u?c:nullptr;candidate_cache.ready=false;}

static JSAtom key(JSValue* d,unsigned k){return JSAtom(JS_VALUE_GET_INT(d[KeyCount+k]));}
static bool get(JSContext* c,JSValue* d,JSValueConst o,unsigned k,JSValue* value){
    int found=JS_TryOwnDataPropertyCachedBorrowed(c,o,key(d,k),value);
    if(!found)found=JS_TryDataPropertyBorrowed(o,key(d,k),value);return found!=0;
}
static bool number(JSContext* c,JSValue* d,JSValueConst o,unsigned k,double* out){JSValue v;return get(c,d,o,k,&v)&&JS_IsNumber(v)&&!JS_ToFloat64(c,out,v);}
static bool indexed(JSContext* c,JSValueConst array,uint32_t i,double* out){JSValue v;return JS_TryPlainArrayValueBorrowed(array,i,&v)&&JS_IsNumber(v)&&!JS_ToFloat64(c,out,v);}
static bool spec(JSContext* c,JSValue* d,unsigned k,uint32_t i,double* out){JSValue a;return get(c,d,d[Defs],k,&a)&&indexed(c,a,i,out);}
static bool candidates(JSContext* c,JSValue* d,JSValueConst list,uint32_t count,uint32_t frame,uint64_t epoch){
    auto& cache=candidate_cache;
    if(cache.ready&&cache.context==c&&cache.list==JS_VALUE_GET_PTR(list)&&cache.frame==frame&&cache.count==count&&cache.epoch==epoch){++cache_hits;return true;}
    cache.ready=false;if(count>cache.items.size())return false;
    uint32_t used=0;
    for(uint32_t i=0;i<count;i++){
        JSValue element,definition,type;double t=0,layer=0,middle=0,front=0,back=0,height=0;
        if(!JS_TryPlainArrayValueBorrowed(list,i,&element)||!get(c,d,element,Definition,&definition)||!get(c,d,definition,Type,&type)||!JS_IsNumber(type))return false;
        JS_ToFloat64(c,&t,type);if(!(t>=0&&t<2147483647.)||uint32_t(t)!=t)return false;
        if(!spec(c,d,Layers,uint32_t(t),&layer)||!number(c,d,d[Defs],Middle,&middle))return false;
        if(layer!=middle)continue;
        if(!spec(c,d,Front,uint32_t(t),&front)||!spec(c,d,Back,uint32_t(t),&back))return false;
        if(!(front>0||back>0))continue;
        if(!spec(c,d,Heights,uint32_t(t),&height))return false;
        cache.items[used++]={i,uint32_t(t),front,back,height};
    }
    ++cache_builds;scanned_elements+=count;selected_elements+=used;
    cache.context=c;cache.list=JS_VALUE_GET_PTR(list);cache.frame=frame;cache.count=count;cache.used=used;cache.epoch=epoch;cache.ready=true;return true;
}
static JSValue run(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int,JSValue* d){
    ++calls;JSValue ctor,prototype,midas,z,y,kill,list,frame;uint32_t count=0;int instance=0;
    auto original=[&](){++prefix_fallbacks;return JS_Call(c,d[Source],self,argc,argv);};
    if(c!=program_context||argc!=1||!JS_TryPrototypeContains(self,d[GamePrototype],&instance)||!instance||
       !get(c,d,d[Global],Object,&ctor)||!JS_IsStrictEqual(c,ctor,d[ObjectConstructor])||
       !get(c,d,ctor,Prototype,&prototype)||!JS_IsStrictEqual(c,prototype,d[ObjectPrototype])||
       !get(c,d,argv[0],Z,&z)||!JS_IsNumber(z)||!get(c,d,argv[0],Y,&y)||!JS_IsNumber(y)||
       !get(c,d,argv[0],Kill,&kill)||!JS_IsObject(kill)||!get(c,d,argv[0],Midas,&midas)||JS_ToBool(c,midas)||
       !get(c,d,self,List,&list)||!JS_TryPlainArrayLength(list,&count)||
       !get(c,d,d[Runtime],Frame,&frame)||JS_VALUE_GET_TAG(frame)!=JS_TAG_INT||JS_VALUE_GET_INT(frame)<0)return original();
    uint32_t source_frame=uint32_t(JS_VALUE_GET_INT(frame));uint64_t epoch=JS_GetSourceMutationEpoch(JS_GetRuntime(c));
    if(!candidates(c,d,list,count,source_frame,epoch))return original();
    double callerZ=0,callerY=0;JS_ToFloat64(c,&callerZ,z);JS_ToFloat64(c,&callerY,y);
    list=JS_DupValue(c,list);kill=JS_DupValue(c,kill);JSValue result=JS_NewObject(c);
    if(JS_IsException(result)){JS_FreeValue(c,list);JS_FreeValue(c,kill);return result;}
    bool collided=false;uint32_t index=0;JSValue lastElement=JS_UNDEFINED;
    auto finish=[&](bool fallback){
        if(fallback)++tail_fallbacks;else ++completed;
        JSValue args[]={argv[0],result,JS_NewBool(c,collided),JS_NewUint32(c,index),z,y,kill,d[Defs],d[Talos],lastElement};
        JSValue out=JS_Call(c,d[Tail],self,10,args);JS_FreeValue(c,lastElement);JS_FreeValue(c,result);JS_FreeValue(c,kill);JS_FreeValue(c,list);return out;
    };
    auto failure=[&](){JS_FreeValue(c,lastElement);JS_FreeValue(c,result);JS_FreeValue(c,kill);JS_FreeValue(c,list);return JS_EXCEPTION;};
    const uint32_t used=candidate_cache.used;
    for(uint32_t cursor=0;cursor<used;cursor++){
        Candidate item=candidate_cache.items[cursor];index=item.index;JSValue element;double otherZ=0;
        if(!JS_TryPlainArrayValueBorrowed(list,index,&element))return finish(true);
        JSValue next=JS_DupValue(c,element);JS_FreeValue(c,lastElement);lastElement=next;
        if(!number(c,d,element,Z,&otherZ))return finish(true);
        if(!(callerZ>=otherZ-item.front&&callerZ<=otherZ+item.back&&callerY<=item.height))continue;
        ++pairs;JSValue method=JS_GetProperty(c,kill,key(d,Hit));if(JS_IsException(method))return failure();
        JSValue otherKill=JS_GetProperty(c,lastElement,key(d,Kill));if(JS_IsException(otherKill)){JS_FreeValue(c,method);return failure();}
        JSValue hit=JS_Call(c,method,kill,1,&otherKill);JS_FreeValue(c,method);JS_FreeValue(c,otherKill);if(JS_IsException(hit))return failure();
        bool yes=JS_ToBool(c,hit);JS_FreeValue(c,hit);
        if(yes){
            collided=true;if(JS_SetPropertyValue(c,result,JS_DupValue(c,d[Clip]),JS_DupValue(c,lastElement),0)<0)return failure();
            JSValue args[]={argv[0],result,JS_NewUint32(c,item.type),d[Defs]};JSValue after=JS_Call(c,d[Post],self,4,args);
            if(JS_IsException(after))return failure();collided=JS_ToBool(c,after);JS_FreeValue(c,after);
        }
        uint32_t fresh=0;JSValue current_list;
        if(JS_GetSourceMutationEpoch(JS_GetRuntime(c))!=epoch||!candidate_cache.ready||candidate_cache.context!=c||candidate_cache.list!=JS_VALUE_GET_PTR(list)||candidate_cache.frame!=source_frame||!get(c,d,self,List,&current_list)||!JS_IsStrictEqual(c,current_list,list)||!JS_TryPlainArrayLength(list,&fresh)||fresh!=count){++index;return finish(true);}
    }
    // The source for-loop's last local is the actual final list element,
    // including a noncollider. Preserve its lifetime through the source suffix.
    if(count){JSValue last;if(!JS_TryPlainArrayValueBorrowed(list,count-1,&last)){index=count;return finish(true);}JSValue next=JS_DupValue(c,last);JS_FreeValue(c,lastElement);lastElement=next;}
    index=count;return finish(false);
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv,int,JSValue* expected){
    auto source=[&](){return argc&&JS_IsFunction(c,argv[0])?JS_DupValue(c,argv[0]):JS_NULL;};
    if(c!=program_context||argc!=7||!JS_IsFunction(c,argv[0])||!JS_IsFunction(c,argv[1])||!JS_IsFunction(c,argv[2])||!JS_IsObject(argv[3])||!JS_IsObject(argv[5])||!JS_IsFunction(c,argv[6])||!JS_CheckSourceFunction(c,argv[0],4601,3051984479u))return source();
    JSAtom prototype_key=JS_NewAtom(c,"prototype");JSValue game_prototype;
    bool qualified=prototype_key&&JS_TryDataPropertyBorrowed(argv[6],prototype_key,&game_prototype)==1&&JS_IsObject(game_prototype);JS_FreeAtom(c,prototype_key);
    if(!qualified)return source();
    struct Watch{const char* name;unsigned length;uint32_t hash;};
    const Watch watches[]={{nullptr,1039u,1432097679u},{"turnThisCharacterToStone",1338u,1414846466u},{"obf_01010504",1419u,3968486996u},{"turnThisCharacterToGold",1764u,446938633u},{"obf_01010b0c",6859u,2970123900u}};
    for(const auto& watch:watches){JSValue function=argv[6];JSAtom atom=JS_ATOM_NULL;if(watch.name){atom=JS_NewAtom(c,watch.name);qualified=atom&&JS_TryDataPropertyBorrowed(game_prototype,atom,&function)==1;JS_FreeAtom(c,atom);}if(!qualified||!JS_WatchSourceMutation(c,function,watch.length,watch.hash))return source();}

    const char* names[]={"zPos","yPos","killArea","isMidas","obf_0101060d","definition","elementType","ELEMENTLAYERS","ELEMENTDEPTHSFRONT","ELEMENTDEPTHSBACK","ELEMENTHEIGHTS","MIDLAYER","hitTestObject","Object","prototype","clip","frame"};
    JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;bool failed=false;
    for(unsigned i=0;i<KeyCount;i++){JSAtom a=JS_NewAtom(c,names[i]);if(!a){failed=true;break;}data[i]=JS_AtomToValue(c,a);data[KeyCount+i]=JS_NewInt32(c,int32_t(a));JS_FreeAtom(c,a);if(JS_IsException(data[i])){failed=true;break;}}
    for(unsigned i=0;i<5;i++)data[Source+i]=JS_DupValue(c,argv[i]);
    data[Runtime]=JS_DupValue(c,argv[5]);data[GamePrototype]=JS_DupValue(c,game_prototype);
    data[Global]=JS_DupValue(c,expected[0]);data[ObjectConstructor]=JS_DupValue(c,expected[1]);data[ObjectPrototype]=JS_DupValue(c,expected[2]);
    JSValue result=failed?JS_EXCEPTION:JS_NewCFunctionData(c,run,1,0,DataCount,data);for(auto v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
    JSValue out=JS_NewObject(c);const char* names[]={"calls","completed","prefix_fallbacks","tail_fallbacks","pairs","cache_builds","cache_hits","scanned_elements","selected_elements","cache_storage_bytes"};uint64_t values[]={calls,completed,prefix_fallbacks,tail_fallbacks,pairs,cache_builds,cache_hits,scanned_elements,selected_elements,sizeof(candidate_cache)};
    for(unsigned i=0;i<10;i++)JS_SetPropertyStr(c,out,names[i],JS_NewFloat64(c,double(values[i])));return out;
}
static void bind(JSContext* c){
    calls=completed=prefix_fallbacks=tail_fallbacks=pairs=0;cache_builds=cache_hits=scanned_elements=selected_elements=0;candidate_cache.ready=false;program_context=nullptr;JSValue global=JS_GetGlobalObject(c);
    JSValue expected[]={global,JS_GetPropertyStr(c,global,"Object"),JS_UNDEFINED};expected[2]=JS_GetPropertyStr(c,expected[1],"prototype");
    JS_SetPropertyStr(c,global,"nativeRockCollisionFactory",JS_NewCFunctionData(c,factory,7,0,3,expected));JS_SetPropertyStr(c,global,"nativeRockCollisionStats",JS_NewCFunction(c,stats,"nativeRockCollisionStats",0));JS_FreeValue(c,expected[2]);JS_FreeValue(c,expected[1]);JS_FreeValue(c,global);
}
}
