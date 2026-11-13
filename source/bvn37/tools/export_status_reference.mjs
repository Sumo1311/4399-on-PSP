// Execute pinned EffectView/SpecialEffectView/SteelHitEffect methods in declared data stubs.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,modelFile,artFile,output]=process.argv.slice(2);
if(!output)throw Error('usage: export_status_reference.mjs reference model.json art.json STATUS.IN');
const prefix='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const sources={};
function source(relative){
    const p=prefix+relative,b=fs.readFileSync(path.join(reference,p));
    sources[p]=crypto.createHash('sha256').update(b).digest('hex');
    return b.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
}
function methods(text,names,env={}){
    env={int:v=>v|0,...env};
    const result={};
    for(const name of names){
        const m=new RegExp(`(?:public|private|protected)(?: override)? function ${name}\\(([^)]*)\\):\\w+\\s*\\{`).exec(text);
        if(!m)throw Error('missing method '+name);
        let end=m.index+m[0].length,depth=1;const start=end;
        for(;depth;end++){if(text[end]==='{')depth++;if(text[end]==='}')depth--;}
        const args=m[1].split(',').map(v=>v.trim().replace(/:\w+/,''));
        const body=text.slice(start,end-1).replace(/var (\w+):int\s*;/g,'var $1=0;')
            .replace(/var (\w+):[\w.]+(?:<[^>]+>)?/g,'var $1')
            .replace(/for each\(var (\w+) in _onRemoveFuncs\)/g,'for(var $1 of (_onRemoveFuncs||[]))')
            .replace(/v is FighterMain/g,'v instanceof FighterMain').replace(/\s+as FighterMain/g,'')
            .replace(/frame is int/g,'Number.isInteger(frame)').replace(/frame is String/g,'typeof frame === "string"')
            .replace(/function \(\):void/g,'function ()')
            .replace(/new Vector\.<\w+>\(\)/g,'[]')
            .replace(/super\.(\w+)\(([^)]*)\)/g,'Base.$1.call(this,$2)');
        result[name]=Function(...Object.keys(env),`return function(${args}){with(this){${body}}}`)(...Object.values(env));
    }
    return result;
}
const model=JSON.parse(fs.readFileSync(modelFile)),art=JSON.parse(fs.readFileSync(artFile));
const metadata=new Map(art.clips.map(c=>[c.className,c]));
let random=1;
const randomMath=Object.create(Math);randomMath.random=()=>{random^=random<<13;random^=random>>>17;random^=random<<5;return (random>>>0)/4294967296;};
const KyoMath={asRadians:v=>v*Math.PI/180,getPointByRadians:(p,a,unused,out)=>{
    out.x=p.x*Math.cos(a)-p.y*Math.sin(a);out.y=p.x*Math.sin(a)+p.y*Math.cos(a);return out;
}};
const SoundCtrl={I:{playEffectSound(){},playSwcSound(){}}};
const Base=methods(source('views/effects/EffectView.as'),['start','setTarget','setPos','render','renderAnimate',
    'gotoAndPlay','randRotate','renderDisplay','renderFrameLabel','removeSelf','addRemoveBack'],{Math:randomMath,KyoMath,SoundCtrl});
const display=Base.renderDisplay;
Base.renderDisplay=function(){this._displayFrame=this._curFrame;return display.call(this);};
class FighterMain{
    constructor(id){this.id=id;this.x=100;this.y=200;this.direct=id?-1:1;this.actionState=21;
        this.isSteelBody=true;this.isSuperSteelBody=false;this.base=[id*7,id*11,id*13];this.tint=[...this.base];}
    changeColor(c){this.tint=[c.redOffset,c.greenOffset,c.blueOffset];}
    resumeColor(){this.tint=[...this.base];}
}
class ColorTransform{redOffset=0;greenOffset=0;blueOffset=0;}
const callbacks=[],yellow=[];
const EffectCtrl={I:{shine:(color,alpha)=>yellow.push([color,alpha]),setOnFreezeOver:f=>callbacks.push(f)}};
const Special=methods(source('views/effects/SpecialEffectView.as'),['setTarget','start','render'],
    {Base,FighterMain,ColorTransform,FighterActionState:{NORMAL:0,HURT_DOWN:23,HURT_DOWN_TAN:24}});
const Steel=methods(source('views/effects/SteelHitEffect.as'),['setTarget'],{Base,FighterMain,ColorTransform,EffectCtrl});
function view(data){
    const c=metadata.get(data.className),v={...Base,_data:data,_target:null,_fighter:null,_finished:false,
        _onRemoveFuncs:null,_isDestroyed:false,_rotation:0,_direct:0,_curFrame:0,_orgX:0,_orgY:0,
        _offsetSrc:{x:0,y:0},_offsetRot:{x:0,y:0},isActive:true,autoRemove:true,loopPlay:false,holdFrame:-1,
        display:{parent:null,bitmapData:null,scaleX:1,rotation:0,x:0,y:0},
        _bitmapDatas:Array.from({length:c.cache_frames},(_,i)=>({bitmapData:i,offsetX:0,offsetY:0})),
        _frameLabels:Object.fromEntries(Object.entries(c.labels||{}).map(([k,i])=>[i,k]))};
    for(const key of ['_rotation','_direct','_curFrame','holdFrame']){
        let n=v[key];Object.defineProperty(v,key,{get:()=>n,set:value=>{n=value|0;},configurable:true});
    }
    Object.assign(v,data.isSpecial?Special:data.isSteelHit?Steel:{});return v;
}
function EffectView(d){return view(d);}function SpecialEffectView(d){return view(d);}function SteelHitEffect(d){return view(d);}
const managerMethods=methods(source('ctrler/effect/EffectManager.as'),['getEffectView'],
    {EffectView,SpecialEffectView,SteelHitEffect,BuffEffectView:EffectView});
const ctrlMethods=methods(source('ctrler/EffectCtrl.as'),['render','renderAnimate','removeEffect']);
const records=model.effects.map((e,index)=>{
    const d={...e,index};Object.defineProperty(d,Symbol.toPrimitive,{value:()=>e.id});return d;
});
const get=id=>records.find(e=>e.id===id);
const lines=[];let zeroAngles=0,finished=0,restored=0,overlaps=0;
const scenarios=[];
for(const hz of [30,60])for(const kind of [0,1,2])for(const endState of [0,23,24])scenarios.push({hz,kind,endState,steel:false});
for(const hz of [30,60])for(const kind of [0,1])for(const endState of [0,23,24])scenarios.push({hz,kind,endState,steel:true});
for(const [scenario,config]of scenarios.entries()){
    const {hz,kind,endState,steel}=config,seed=scenario%2?1:0x6d2b79f5;random=seed;
    callbacks.length=yellow.length=0;
    const manager={...managerMethods,_viewCache:{}};
    let animate=false;
    const noop=()=>{};
    const ctrl={...ctrlMethods,_effects:[],freezeEnabled:false,renderShine:noop,clearFrameEffectCount:noop,
        _slowHandler:{render:noop,isRenderAnimate:()=>animate,renderAnimate:noop},
        _blackBackHandler:{render:noop,renderAnimate:noop},_shadowHandler:{renderAnimate:noop},
        _shakeHandler:{renderAnimate:noop},renderRemoveEnemy:noop};
    const targets=[new FighterMain(0),new FighterMain(1)];
    const remove=v=>ctrl.removeEffect(v);
    let phase=0,gameTick=0;
    for(let tick=0;tick<240;tick++){
        for(const t of targets){
            t.x=100+t.id*80+tick*.5;t.y=200-tick*.25;
            t.actionState=steel?11:tick>=(t.id?190:160)?endState:tick%35===0?22:21;
            t.isSuperSteelBody=steel&&!!((t.id+kind)%2);
        }
        let emit=0,target=0;
        if(steel&&tick%21===0){emit=4;target=(tick/21)&1;}
        if(!steel&&(tick===0||tick===4||tick===36)){
            emit=1+(tick===36?(kind+1)%3:kind);target=tick===4?1:0;
        }
        const ids=['fire_ing','ice_ing','shock_ing','steel_hit_kan'];
        const freeze=steel&&[20,40,80,160,220].includes(tick)?1:0;
        const oldYellow=yellow.length;
        if(emit){
            const d=get(ids[emit-1]),v=manager.getEffectView(d);
            v.start(targets[target].x,targets[target].y,targets[target].direct);
            v.addRemoveBack(remove);ctrl._effects.push(v);v.setTarget(targets[target]);
            if(d.randRotate&&v._rotation===0)zeroAngles++;
        }
        const yellowNow=yellow.length-oldYellow;
        if(freeze){for(const cb of callbacks)cb();callbacks.length=0;restored++;}
        phase+=hz;const render=phase>=60;if(render)phase-=60;
        animate=render&&gameTick++%(hz/30)===0;
        if(render)ctrl.render();
        const values=[scenario,hz,seed,tick,emit,target,freeze,+animate,yellowNow,...targets.flatMap(t=>[t.x,t.y,t.actionState,+t.isSuperSteelBody,...t.tint]),ctrl._effects.length];
        if(ctrl._effects.length>1)overlaps++;
        for(const v of ctrl._effects){
            values.push(v._data.index,v._fighter.id,v._orgX,v._orgY,v._displayFrame,v._curFrame,
                v.display.scaleX,v._rotation,v._direct,+v._finished);
            if(v._finished)finished++;
        }
        lines.push(values.join(' '));
    }
}
fs.writeFileSync(output,`${lines.length}\n${lines.join('\n')}\n`);
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:lines.length,scenarios:scenarios.length,zeroAngles,finished,restored,overlaps,
    sources,modelSha256:sha(modelFile),artSha256:sha(artFile),fixtureSha256:sha(output),
    scope:'Pinned source view, special/steel setTarget, manager pool and controller render/remove methods executed '
        +'after declared AS3 type/cast/for-each adapters, default-zero local ints, int properties, seeded Math.random, bitmap metadata, '
        +'plain fighter/color/sound/slow/shine stubs. Exercises retained-view callback target, multiple effects, '
        +'one-based source labels and 30/60 Hz bridge. No Flash VM, actual bitmap raster, slow/shine timeline or audio output.'
},null,2)+'\n');
console.log(`Exported ${lines.length} status states, ${zeroAngles} zero angles, ${restored} callback batches`);
