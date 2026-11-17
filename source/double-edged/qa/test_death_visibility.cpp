#include "corpse_visibility.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

struct Result {std::vector<unsigned> hidden,dying;devisual::CorpseCensus stats;};
int main(){
    JSRuntime* rt=JS_NewRuntime();JSContext* c=JS_NewContext(rt);assert(c);
    const char* keys[]={"myFightingGame","activeBaddies","health","alive","landed","deadCount","shadow","_uid"};
    JSAtom atoms[8];for(unsigned i=0;i<8;++i)atoms[i]=JS_NewAtom(c,keys[i]);
    auto eval=[&](const std::string& code){JSValue v=JS_Eval(c,code.data(),code.size(),"death-phase-case",JS_EVAL_TYPE_GLOBAL);assert(!JS_IsException(v));return v;};
    unsigned cases=0;
    auto collect=[&](JSValueConst root){Result r;r.stats=devisual::collect_corpses(c,root,atoms[0],atoms[1],atoms[2],atoms[3],atoms[4],atoms[5],atoms[6],atoms[7],
        [&](unsigned uid,devisual::DeathVisual phase){(phase==devisual::DeathVisual::Corpse?r.hidden:r.dying).push_back(uid);});return r;};
    auto check=[&](const std::string& actor,std::vector<unsigned> hidden,std::vector<unsigned> dying){
        auto root=eval("({myFightingGame:{activeBaddies:["+actor+"]}})");auto r=collect(root);assert(r.hidden==hidden&&r.dying==dying);JS_FreeValue(c,root);++cases;
    };
    const std::string tail=",_uid:10,shadow:{_uid:11}}";
    check("{health:3,alive:true,landed:true,deadCount:0"+tail,{},{});
    check("{health:0,alive:true,landed:true,deadCount:0"+tail,{},{}); // zero-HP boss still alive
    check("{health:0,alive:true,landed:false,deadCount:40"+tail,{},{});
    check("{health:0,alive:false,landed:false,deadCount:59"+tail,{}, {10}); // airborne death
    check("{health:0,alive:false,landed:false,deadCount:0"+tail,{}, {10});
    check("{health:0,alive:false,landed:true,deadCount:90"+tail,{}, {10});
    check("{health:0,alive:false,landed:true,deadCount:60"+tail,{}, {10}); // source flash begins below60
    check("{health:0,alive:false,landed:true,deadCount:59"+tail,{10,11},{});
    check("{health:0,alive:false,landed:true,deadCount:1"+tail,{10,11},{});
    check("{health:0,alive:false,landed:true,deadCount:0"+tail,{10,11},{});
    check("{health:-1,alive:false,landed:true,deadCount:-1"+tail,{10,11},{});
    check("{health:0,alive:false,deadCount:40"+tail,{},{}); // independent boss timeline
    check("{health:0,alive:false,landed:true"+tail,{},{});
    check("{health:0,landed:true,deadCount:0"+tail,{},{});
    check("{health:1,alive:false,landed:true,deadCount:0"+tail,{},{});
    check("{health:NaN,alive:false,landed:true,deadCount:0"+tail,{},{});
    check("{health:-Infinity,alive:false,landed:true,deadCount:0"+tail,{},{});
    check("{health:0,alive:false,landed:true,deadCount:NaN"+tail,{},{});
    check("{health:'0',alive:false,landed:true,deadCount:0"+tail,{},{});
    check("{health:0,alive:0,landed:true,deadCount:0"+tail,{},{});
    check("{health:0,alive:false,landed:1,deadCount:0"+tail,{},{});
    check("{health:0,alive:false,landed:true,deadCount:'0'"+tail,{},{});
    check("{health:0,alive:false,landed:true,deadCount:0,_uid:10}",{10},{});
    check("{health:0,alive:false,landed:true,deadCount:0,_uid:0}",{},{});
    check("Object.create({health:0,alive:false,landed:true,deadCount:0,_uid:10})",{},{});
    auto v=eval("globalThis.getterHits=0");JS_FreeValue(c,v);
    check("{get health(){++getterHits;return 0},alive:false,landed:true,deadCount:0,_uid:10}",{},{});
    check("{health:0,get alive(){++getterHits;return false},landed:true,deadCount:0,_uid:10}",{},{});
    check("{health:0,alive:false,get landed(){++getterHits;return true},deadCount:0,_uid:10}",{},{});
    check("{health:0,alive:false,landed:true,get deadCount(){++getterHits;return 0},_uid:10}",{},{});
    check("{health:0,alive:false,landed:true,deadCount:0,_uid:10,get shadow(){++getterHits;return {_uid:11}}}",{10},{});
    check("new Proxy({health:0,alive:false,landed:true,deadCount:0,_uid:10},{get(){++getterHits;throw Error('trap')},getOwnPropertyDescriptor(){++getterHits;throw Error('trap')}})",{},{});
    v=eval("getterHits");int hits=-1;JS_ToInt32(c,&hits,v);JS_FreeValue(c,v);assert(hits==0);
    // The same source actor may die, land, resurrect, then be killed again.
    auto root=eval("globalThis.subject={myFightingGame:{activeBaddies:[{health:0,alive:false,landed:false,deadCount:20,_uid:20,shadow:{_uid:21}}]}};subject");
    assert(collect(root).dying==std::vector<unsigned>{20});
    v=eval("subject.myFightingGame.activeBaddies[0].landed=true");JS_FreeValue(c,v);assert((collect(root).hidden==std::vector<unsigned>{20,21}));
    v=eval("subject.myFightingGame.activeBaddies[0].alive=true;subject.myFightingGame.activeBaddies[0].health=7");JS_FreeValue(c,v);auto revived=collect(root);assert(revived.hidden.empty()&&revived.dying.empty());
    JS_FreeValue(c,root);++cases;
    for(auto a:atoms)JS_FreeAtom(c,a);JS_FreeContext(c);JS_RunGC(rt);JS_FreeRuntime(rt);
    std::cout<<cases<<" actual QuickJS death-phase,revival and no-getter cases passed\n";
}
