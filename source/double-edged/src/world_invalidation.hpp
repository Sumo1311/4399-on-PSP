#pragma once
#include "quickjs.h"
#include <array>
#include <cstdint>
namespace deworldinvalidate {
enum Root {Source,Marks,GetName,SetName,ChildrenName,GetAtom,SetAtom,ChildrenAtom,DataCount};
struct Scratch {
    JSContext* owner=nullptr;bool busy=false;uint32_t generation=0;
    std::array<JSValue,1024> pending;
    std::array<JSValue*,512> slots;
    std::array<JSValue*,1024> seen;
    std::array<uint32_t,1024> tags{};
};
// The script worker owns the VM. Borrowed values are never roots/refcounted.
// A busy fallback keeps recursive/custom calls on the original JS path and
// prevents nested calls from reusing this fixed scratch area.
static Scratch scratch;
static unsigned calls=0,completed=0,empty=0,fallbacks=0,cleared=0,max_slots=0,max_visits=0;
static unsigned factories=0,source_rejects=0,binding_rejects=0,method_rejects=0;
static JSAtom atom(JSValue* d,unsigned k){return JSAtom(JS_VALUE_GET_INT(d[k]));}
static JSValue run(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int,JSValue* d){
    ++calls;auto original=[&](){++fallbacks;return JS_Call(c,d[Source],self,argc,argv);};
    if(argc!=1||scratch.owner!=c||scratch.busy)return original();
    struct Busy{bool& value;Busy(bool& v):value(v){value=true;}~Busy(){value=false;}} busy(scratch.busy);
    JSValue get,set=JS_UNDEFINED;
    // Intrinsics may still be lazy AUTOINIT properties during runtime setup.
    // Never instantiate them speculatively: the first original call does so.
    if(JS_TryDataPropertyBorrowed(d[Marks],atom(d,GetAtom),&get)!=1){++method_rejects;return original();}
    if(JS_TryDataPropertyBorrowed(d[Marks],atom(d,SetAtom),&set)!=1)set=JS_UNDEFINED;
    if(++scratch.generation==0){scratch.tags.fill(0);scratch.generation=1;}
    unsigned pending=1,used=0,visits=0;scratch.pending[0]=argv[0];
    while(pending){
        JSValue node=scratch.pending[--pending];JSValue* slot=nullptr;
        if(++visits>4096)return original();
        int found=JS_TryWeakMapNumericFlagSlot(c,d[Marks],get,set,node,&slot);
        if(!found)return original();if(found==2)continue;
        unsigned hash=(uintptr_t(slot)>>3)&1023;
        while(scratch.tags[hash]==scratch.generation&&scratch.seen[hash]!=slot)hash=(hash+1)&1023;
        if(scratch.tags[hash]==scratch.generation)continue;
        if(used==scratch.slots.size())return original();
        scratch.tags[hash]=scratch.generation;scratch.seen[hash]=slot;scratch.slots[used++]=slot;
        JSValue children;int data=JS_TryOwnDataPropertyBorrowed(node,atom(d,ChildrenAtom),&children);
        if(data==2)data=JS_TryDataPropertyBorrowed(node,atom(d,ChildrenAtom),&children);
        if(!data)return original();if(!JS_ToBool(c,children))continue;
        uint32_t count=0;if(!JS_TryPlainArrayLength(children,&count)||count>scratch.pending.size()-pending)return original();
        for(uint32_t i=count;i;i--){JSValue child;if(!JS_TryPlainArrayValueBorrowed(children,i-1,&child))return original();scratch.pending[pending++]=child;}
    }
    // Every inspected read is callback-free and allocation-free. Commit only
    // after the complete traversal is supported, in source preorder. Repeated
    // keys/cycles are visited once,as after the original first set(key,0).
    for(unsigned i=0;i<used;i++)*scratch.slots[i]=JS_NewInt32(c,0);
    ++completed;if(!used)++empty;cleared+=used;if(used>max_slots)max_slots=used;if(visits>max_visits)max_visits=visits;
    return JS_UNDEFINED;
}
static JSValue factory(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
    ++factories;
    auto original=[&](){return argc?JS_DupValue(c,argv[0]):JS_NULL;};
    if(argc!=2||!JS_IsObject(argv[1])||!JS_CheckSourceFunction(c,argv[0],233,4228710180u)){++source_rejects;return original();}
    int binding=JS_CheckClosureBinding(c,argv[0],"worldCacheMarks",argv[1]);if(binding<0)return JS_EXCEPTION;if(!binding){++binding_rejects;return original();}
    JSValue data[DataCount];for(auto& v:data)v=JS_UNDEFINED;
    data[Source]=JS_DupValue(c,argv[0]);data[Marks]=JS_DupValue(c,argv[1]);bool ready=true;
    const char* names[]={"get","set","_children"};
    for(unsigned i=0;i<3;i++){JSAtom a=JS_NewAtom(c,names[i]);if(!a){ready=false;break;}data[GetName+i]=JS_AtomToValue(c,a);data[GetAtom+i]=JS_NewInt32(c,int32_t(a));JS_FreeAtom(c,a);if(JS_IsException(data[GetName+i])){ready=false;break;}}
    JSValue result=ready?JS_NewCFunctionData(c,run,1,0,DataCount,data):JS_EXCEPTION;
    for(auto v:data)JS_FreeValue(c,v);return result;
}
static JSValue stats(JSContext* c,JSValueConst,int,JSValueConst*){
    JSValue out=JS_NewObject(c);const char* names[]={"calls","completed","empty","fallbacks","cleared","max_slots","max_visits","scratch_bytes"};unsigned values[]={calls,completed,empty,fallbacks,cleared,max_slots,max_visits,sizeof(Scratch)};
    for(unsigned i=0;i<8;i++)JS_SetPropertyStr(c,out,names[i],JS_NewUint32(c,values[i]));JS_SetPropertyStr(c,out,"factories",JS_NewUint32(c,factories));JS_SetPropertyStr(c,out,"source_rejects",JS_NewUint32(c,source_rejects));JS_SetPropertyStr(c,out,"binding_rejects",JS_NewUint32(c,binding_rejects));JS_SetPropertyStr(c,out,"method_rejects",JS_NewUint32(c,method_rejects));return out;
}
static void bind(JSContext* c){
    scratch.owner=c;scratch.busy=false;scratch.generation=0;scratch.tags.fill(0);calls=completed=empty=fallbacks=cleared=max_slots=max_visits=0;factories=source_rejects=binding_rejects=method_rejects=0;
    JSValue g=JS_GetGlobalObject(c);JS_SetPropertyStr(c,g,"nativeWorldInvalidationFactory",JS_NewCFunction(c,factory,"nativeWorldInvalidationFactory",2));JS_SetPropertyStr(c,g,"nativeWorldInvalidationStats",JS_NewCFunction(c,stats,"nativeWorldInvalidationStats",0));JS_FreeValue(c,g);
}
}
