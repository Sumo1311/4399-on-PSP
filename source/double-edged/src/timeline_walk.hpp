#pragma once
#include "quickjs.h"
#include <cstdint>

namespace dewalk {
enum Key {Valid,Motion,Skip,Children,Playing,Definition,Frames,Length,Frame,Pending,LastScript,Needs,Full,Slice,KeyCount};
enum {Original=0,MotionTree=1,Invalidate=2,Construct=3,QueueScript=4,Finish=5,ChildrenFallback=6,SourceSlice=7,Functions=8,Strings=Functions,Atoms=Strings+KeyCount,DataCount=Atoms+KeyCount};
static std::uint32_t nodes=0,pruned=0,leaf_gates=0,snapshots=0,advanced=0,finish_fallbacks=0;
static std::uint32_t inline_snapshots=0,inline_values=0,inline_zero=0,snapshot_fallbacks=0,max_inline_slots=0,inline_dictionary=0;
// Transient owned roots replace an unobservable private Array. Slots remain
// claimed through the parent's construction callback and are cleared in the
// same forward element-release order as the Array finalizer. No persistent
// game-object references, heap allocation or large per-recursion buffer.
struct SnapshotPool {JSContext* owner=nullptr;uint32_t used=0;JSValue values[16][32];};
static SnapshotPool snapshot_pool;
struct InlineSnapshot {
    JSContext* c;int slot=-1;uint32_t count=0;
    explicit InlineSnapshot(JSContext* context):c(context){}
    bool capture(JSValueConst receiver,JSValueConst slice){
        if(snapshot_pool.used&&snapshot_pool.owner!=c)return false;
        if(!snapshot_pool.used)snapshot_pool.owner=c;
        for(unsigned i=0;i<16;i++)if(!(snapshot_pool.used&(1u<<i))){slot=int(i);break;}
        if(slot<0)return false;
        snapshot_pool.used|=1u<<slot;
        int kind=JS_TryPrivateArraySnapshot(c,receiver,slice,snapshot_pool.values[slot],32,&count);
        if(!kind){clear();return false;}if(kind==2)++inline_dictionary;
        unsigned used=0;for(unsigned i=0;i<16;i++)used+=!!(snapshot_pool.used&(1u<<i));
        if(used>max_inline_slots)max_inline_slots=used;
        ++inline_snapshots;inline_values+=count;if(!count)++inline_zero;return true;
    }
    JSValue value(unsigned i)const{return snapshot_pool.values[slot][i];}
    void clear(){
        if(slot<0)return;
        for(unsigned i=0;i<count;i++){
            JSValue v=snapshot_pool.values[slot][i];snapshot_pool.values[slot][i]=JS_UNDEFINED;JS_FreeValue(c,v);
        }
        snapshot_pool.used&=~(1u<<slot);slot=-1;count=0;
    }
    ~InlineSnapshot(){clear();}
    InlineSnapshot(const InlineSnapshot&)=delete;InlineSnapshot& operator=(const InlineSnapshot&)=delete;
};
struct Owned {
    JSContext* c;JSValue v;
    Owned(JSContext* context,JSValue value):c(context),v(value){}
    ~Owned(){JS_FreeValue(c,v);}
    Owned(const Owned&)=delete;Owned& operator=(const Owned&)=delete;
    bool error()const{return JS_IsException(v);}
    void clear(){JS_FreeValue(c,v);v=JS_UNDEFINED;}
    void reset(JSValue value){clear();v=value;}
};
inline JSAtom key(JSValue* data,Key k){return JSAtom(JS_VALUE_GET_INT(data[Atoms+k]));}
inline JSValue get(JSContext* c,JSValueConst o,JSValue* data,Key k){
    JSValue value;JSAtom atom=key(data,k);
    if(JS_TryOwnDataPropertyBorrowed(o,atom,&value)==1)return JS_DupValue(c,value);
    return JS_GetProperty(c,o,atom);
}
inline bool truth(JSContext* c,JSValueConst v){return JS_ToBool(c,v)!=0;}
inline bool set(JSContext* c,JSValueConst o,JSValue* data,Key k,JSValue v){return JS_SetPropertyValue(c,o,JS_DupValue(c,data[Strings+k]),v,0)>=0;}
inline JSValue call1(JSContext* c,JSValueConst fn,JSValueConst o){JSValueConst args[]={o};return JS_Call(c,fn,JS_UNDEFINED,1,args);}
inline JSValue fallback_finish(JSContext* c,JSValueConst o,JSValueConst skip,JSValue* data){
    ++finish_fallbacks;JSValueConst args[]={o,skip};return JS_Call(c,data[Finish],JS_UNDEFINED,2,args);
}
JSValue finish(JSContext* c,JSValueConst o,JSValueConst skip,JSValue* data){
    if(truth(c,skip)){
        if(!set(c,o,data,Skip,JS_FALSE))return JS_EXCEPTION;
        return call1(c,data[Invalidate],o);
    }
    // Callback-free inspection permits a fast finish only for the existing
    // private engine fields. Changed descriptors retain the exact JS body.
    JSValue playing,definition,frames,frame;
    if(JS_TryOwnDataPropertyBorrowed(o,key(data,Playing),&playing)!=1)return fallback_finish(c,o,skip,data);
    if(!truth(c,playing))return JS_UNDEFINED;
    if(JS_TryOwnDataPropertyBorrowed(o,key(data,Definition),&definition)!=1)return fallback_finish(c,o,skip,data);
    if(!truth(c,definition))return JS_UNDEFINED;
    if(JS_TryOwnDataPropertyBorrowed(definition,key(data,Frames),&frames)!=1||!JS_IsArray(frames))return fallback_finish(c,o,skip,data);
    Owned length(c,get(c,frames,data,Length));if(length.error())return JS_EXCEPTION;
    if(JS_VALUE_GET_TAG(length.v)!=JS_TAG_INT)return fallback_finish(c,o,skip,data);
    int n=JS_VALUE_GET_INT(length.v);if(n<=1)return JS_UNDEFINED;
    if(JS_TryOwnDataPropertyBorrowed(o,key(data,Frame),&frame)!=1||JS_VALUE_GET_TAG(frame)!=JS_TAG_INT||JS_VALUE_GET_INT(frame)<1)return fallback_finish(c,o,skip,data);
    int next=JS_VALUE_GET_INT(frame)%n+1;
    const JSAtom keys[]={key(data,Frame),key(data,Pending),key(data,LastScript),key(data,Needs),key(data,Full)};
    const JSValue values[]={JS_NewInt32(c,next),JS_TRUE,JS_NewInt32(c,0),JS_TRUE,JS_NewBool(c,next==1)};
    if(!JS_TryWriteOwnPrimitives(o,keys,values,5))return fallback_finish(c,o,skip,data);
    ++advanced;JSValueConst args[]={o,JS_FALSE};Owned built(c,JS_Call(c,data[Construct],JS_UNDEFINED,2,args));
    if(built.error())return JS_EXCEPTION;
    return call1(c,data[QueueScript],o);
}
JSValue enter(JSContext* c,JSValueConst o,JSValueConst inherited,JSValue* data);
JSValue child(JSContext* c,JSValueConst o,JSValueConst skip,JSValue* data){
    Owned valid(c,get(c,o,data,Valid));if(valid.error())return JS_EXCEPTION;
    if(truth(c,valid.v)){
        Owned motion(c,get(c,o,data,Motion));if(motion.error())return JS_EXCEPTION;
        if(!truth(c,motion.v)){
            Owned old_skip(c,get(c,o,data,Skip));if(old_skip.error())return JS_EXCEPTION;
            if(truth(c,old_skip.v)&&!set(c,o,data,Skip,JS_FALSE))return JS_EXCEPTION;
            ++pruned;return JS_UNDEFINED;
        }
    }
    // These are separate source reads: getters, if any, see the same ordering
    // as the original two tests. No result is reused across a JS callback.
    Owned fresh_valid(c,get(c,o,data,Valid));if(fresh_valid.error())return JS_EXCEPTION;
    if(!truth(c,fresh_valid.v)){
        Owned old_skip(c,get(c,o,data,Skip));if(old_skip.error())return JS_EXCEPTION;
        if(!truth(c,old_skip.v)){
            Owned playing(c,get(c,o,data,Playing));if(playing.error())return JS_EXCEPTION;
            if(!truth(c,playing.v)){
                Owned children(c,get(c,o,data,Children));if(children.error())return JS_EXCEPTION;
                if(!truth(c,children.v)){
                    if(!set(c,o,data,Motion,JS_FALSE)||!set(c,o,data,Valid,JS_TRUE))return JS_EXCEPTION;
                    ++leaf_gates;return JS_UNDEFINED;
                }
            }
        }
    }
    return enter(c,o,skip,data);
}
JSValue enter(JSContext* c,JSValueConst o,JSValueConst inherited,JSValue* data){
    ++nodes;Owned valid(c,get(c,o,data,Valid));if(valid.error())return JS_EXCEPTION;
    bool cached=truth(c,valid.v);valid.clear();
    Owned motion(c,cached?get(c,o,data,Motion):call1(c,data[MotionTree],o));
    if(motion.error())return JS_EXCEPTION;
    bool active=truth(c,motion.v);motion.clear();
    if(!active){
        Owned old_skip(c,get(c,o,data,Skip));if(old_skip.error())return JS_EXCEPTION;
        if(truth(c,old_skip.v)&&!set(c,o,data,Skip,JS_FALSE))return JS_EXCEPTION;
        return JS_UNDEFINED;
    }
    Owned skip(c,truth(c,inherited)?JS_DupValue(c,inherited):get(c,o,data,Skip));if(skip.error())return JS_EXCEPTION;
    // Like the source function-scoped `children` local,retain its snapshot
    // through the parent's construction callback,then release on return.
    Owned snapshot(c,JS_UNDEFINED);
    InlineSnapshot inline_snapshot(c);
    Owned has_children(c,get(c,o,data,Children));if(has_children.error())return JS_EXCEPTION;
    bool present=truth(c,has_children.v);has_children.clear();
    if(present){
        Owned receiver(c,get(c,o,data,Children));if(receiver.error())return JS_EXCEPTION;
        Owned slice(c,get(c,receiver.v,data,Slice));if(slice.error())return JS_EXCEPTION;
        bool standard=JS_IsStrictEqual(c,slice.v,data[SourceSlice]);
        bool inlined=standard&&inline_snapshot.capture(receiver.v,slice.v);
        if(!inlined){++snapshot_fallbacks;snapshot.reset(JS_Call(c,slice.v,receiver.v,0,nullptr));if(snapshot.error())return JS_EXCEPTION;}
        slice.clear();receiver.clear();
        ++snapshots;
        if(inlined){
            for(int i=int(inline_snapshot.count)-1;i>=0;--i){
                Owned value(c,JS_DupValue(c,inline_snapshot.value(unsigned(i))));
                Owned walked(c,child(c,value.v,skip.v,data));if(walked.error())return JS_EXCEPTION;
            }
        }else if(!standard||!JS_IsArray(snapshot.v)){
            JSValueConst args[]={snapshot.v,skip.v};Owned walked(c,JS_Call(c,data[ChildrenFallback],JS_UNDEFINED,2,args));if(walked.error())return JS_EXCEPTION;
        }else{
            Owned length(c,get(c,snapshot.v,data,Length));if(length.error())return JS_EXCEPTION;
            if(JS_VALUE_GET_TAG(length.v)!=JS_TAG_INT){
                JSValueConst args[]={snapshot.v,skip.v};Owned walked(c,JS_Call(c,data[ChildrenFallback],JS_UNDEFINED,2,args));if(walked.error())return JS_EXCEPTION;
            }else for(int i=JS_VALUE_GET_INT(length.v)-1;i>=0;--i){
                Owned value(c,JS_GetPropertyUint32(c,snapshot.v,unsigned(i)));if(value.error())return JS_EXCEPTION;
                Owned walked(c,child(c,value.v,skip.v,data));if(walked.error())return JS_EXCEPTION;
            }
        }
    }
    return finish(c,o,skip.v,data);
}
JSValue invoke(JSContext* c,JSValueConst self,int argc,JSValueConst* argv,int magic,JSValue* data){
    (void)self;(void)magic;return enter(c,argc?argv[0]:JS_UNDEFINED,argc>1?argv[1]:JS_UNDEFINED,data);
}
JSValue factory(JSContext* c,JSValueConst self,int argc,JSValueConst* argv){
    (void)self;if(argc!=Functions)return JS_ThrowTypeError(c,"Original timeline helpers required");
    JSValue data[DataCount];for(auto& value:data)value=JS_UNDEFINED;
    for(unsigned i=0;i<Functions;++i){if(!JS_IsFunction(c,argv[i])){for(auto value:data)JS_FreeValue(c,value);return JS_ThrowTypeError(c,"Timeline helper must be callable");}data[i]=JS_DupValue(c,argv[i]);}
    const char* names[]={"_motionValid","_hasMotion","_skipAdvance","_children","_playing","_definition","frames","length","_frame","_pendingScript","_lastScript","_needsConstruct","_constructFull","slice"};
    bool valid=true;
    for(unsigned i=0;i<KeyCount;++i){JSAtom atom=JS_NewAtom(c,names[i]);if(!atom){valid=false;break;}data[Strings+i]=JS_AtomToString(c,atom);data[Atoms+i]=JS_NewInt32(c,int32_t(atom));JS_FreeAtom(c,atom);if(JS_IsException(data[Strings+i])){valid=false;break;}}
    JSValue fn=valid?JS_NewCFunctionData(c,invoke,2,0,DataCount,data):JS_EXCEPTION;
    for(auto value:data)JS_FreeValue(c,value);return fn;
}
JSValue stats(JSContext* c,JSValueConst self,int argc,JSValueConst* argv){
    (void)self;(void)argc;(void)argv;JSValue value=JS_NewObject(c);
    const char* names[]={"nodes","pruned","leaf_gates","snapshots","advanced","finish_fallbacks"};const uint32_t counts[]={nodes,pruned,leaf_gates,snapshots,advanced,finish_fallbacks};
    for(unsigned i=0;i<6;++i)JS_SetPropertyStr(c,value,names[i],JS_NewUint32(c,counts[i]));
    const char* extra[]={"inline_snapshots","inline_values","inline_zero","snapshot_fallbacks","max_inline_slots","snapshot_pool_used","snapshot_pool_bytes","inline_dictionary"};unsigned values[]={inline_snapshots,inline_values,inline_zero,snapshot_fallbacks,max_inline_slots,snapshot_pool.used,sizeof(snapshot_pool),inline_dictionary};
    for(unsigned i=0;i<8;i++)JS_SetPropertyStr(c,value,extra[i],JS_NewUint32(c,values[i]));return value;
}
inline void bind(JSContext* c){
    JSValue g=JS_GetGlobalObject(c);JS_SetPropertyStr(c,g,"nativeTimelineWalkerFactory",JS_NewCFunction(c,factory,"nativeTimelineWalkerFactory",Functions));JS_SetPropertyStr(c,g,"nativeTimelineWalkerStats",JS_NewCFunction(c,stats,"nativeTimelineWalkerStats",0));JS_FreeValue(c,g);
}
}
