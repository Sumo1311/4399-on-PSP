// Run pinned original floor methods with declared AS3/display adapters.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference, output] = process.argv.slice(2);
if (!output) throw Error('usage: export_floor_reference.mjs reference FLOOR.IN');
const root='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/',sources={};
function source(name) {
    const data=fs.readFileSync(path.join(reference,root+name));
    sources[root+name]=crypto.createHash('sha256').update(data).digest('hex');
    return data.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
}
function method(text,name,env) {
    const m=new RegExp(`(?:public|private) function ${name}\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{`).exec(text);
    if(!m)throw Error('missing '+name);
    let end=m.index+m[0].length,depth=1;const start=end;
    for(;depth;end++){if(text[end]==='{')depth++;if(text[end]==='}')depth--;}
    const args=m[1].split(',').filter(Boolean).map(v=>v.trim().replace(/:\w+/,''));
    const body=text.slice(start,end-1).replace(/var (\w+):\w+/g,'var $1')
        .replace(/_target is FighterMain/g,'_target instanceof FighterMain').replace(/\s+as FighterMain/g,'');
    return Function(...Object.keys(env),`return function(${args}){with(this){${body}}}`)(...Object.values(env));
}
const config=source('GameConfig.as'),GameConfig={};
for(const m of config.matchAll(/public static const (\w+):\w+\s*=\s*(-?\d+(?:\.\d+)?)\s*;/g))GameConfig[m[1]]=Number(m[2]);
const FighterHitFloorType={TAN:0,NORMAL:1,HEAVY:2};
const FighterHurtFlyType=new Proxy({}, {get:(_,key)=>key});
const FighterSpecialFrame=FighterHurtFlyType,FighterActionState=FighterHurtFlyType;
const FighterEvent=FighterHurtFlyType,FighterEventDispatcher={dispatchEvent(){}};
class FighterMain {}
const rows=[],ids=['hit_floor','hit_floor_low','hit_floor_heavy','hit_floor_yan'];
let emitted=[],shake=null;
const EffectCtrl={I:{doEffectById(id,x,y){emitted.push([id,x,y]);},shake(x,y,time){if(x!==0||time!==0)throw Error('unexpected shake');shake=y;}}};
EffectCtrl.I.hitFloorEffect=method(source('ctrler/EffectCtrl.as'),'hitFloorEffect',{FighterHitFloorType});
const effectSource=source('fighter/ctrler/FighterEffectCtrl.as');
const hitFloor=method(effectSource,'hitFloor',{EffectCtrl,FighterMain});
const shakeMethod=method(effectSource,'shake',{EffectCtrl});
const render=method(source('fighter/FighterMC.as'),'renderHurtFly',
    {GameConfig,FighterHurtFlyType,FighterSpecialFrame,FighterActionState,FighterEvent,FighterEventDispatcher,FighterHitFloorType});
const lethal=method(source('fighter/FighterMC.as'),'playHurtDown',{GameConfig,FighterSpecialFrame,FighterHitFloorType});
for(let mode=0;mode<3;mode++)for(let kind=0;kind<4;kind++) {
    const target=kind===0?{}:new FighterMain();
    target.musouEnemyData=kind===2?{isBoss:false}:kind===3?{isBoss:true}:null;
    const effect={_target:target,_targetDisplay:{x:-25.5,y:720.125},hitFloor,shake:shakeMethod};
    const run=(type,distance,hitY,power)=>{
        emitted=[];shake=null;
        const fighter={x:-25.5,y:720.125,isAlive:true,velocity:0,setVecY(v){this.velocity=v;},setDamping(){}};
        const actor={_fighter:fighter,_mcCtrler:{effectCtrler:effect},_curFrameCount:2,_isHeavyDownAttack:!!type,
            _hurtYMin:fighter.y-distance,_hity:hitY,_hurtFlyType:FighterHurtFlyType.HURT_FLY_FALL,goFrame(){},playHurtDown2(){}};
        if(mode===0)effect.hitFloor(type,power);
        else if(mode===1)render.call(actor);
        else lethal.call(actor);
        for(const [id,x,y] of emitted)if(!ids.includes(id)||x!==fighter.x||y!==fighter.y)throw Error('unexpected effect');
        rows.push([mode,type,kind,distance,hitY,power,fighter.velocity,+(shake!==null),shake??0,emitted.length,
            ...emitted.map(e=>ids.indexOf(e[0]))].join(' '));
    };
    if(mode===0)for(let type=0;type<4;type++)for(const power of [-5,-.5,0,.125,.5,1,1.25,2,2.75,3,5,10])run(type,0,0,power);
    if(mode===1)for(let heavy=0;heavy<2;heavy++)for(const distance of [0,1,25,60,75,100,125,200,400])
        for(const hitY of [-6,0,5,5.25,6,10,20,100])run(heavy,distance,hitY,0);
    if(mode===2)run(1,0,0,0);
}
fs.writeFileSync(output,rows.length+'\n'+rows.join('\n')+'\n');
fs.writeFileSync(output+'.json',JSON.stringify({rows:rows.length,sourceHashes:sources,
    fixtureSha256:crypto.createHash('sha256').update(fs.readFileSync(output)).digest('hex'),
    scope:'Original FighterMC.renderHurtFly HURT_FLY_FALL branch and playHurtDown; FighterEffectCtrl.hitFloor/shake and EffectCtrl.hitFloorEffect after declared AS3 type, class, constant and display/event adapters. Samples supplied fall distances, hitY and target kinds. Not full original physics, controller scheduling, Flash VM or raster.'},null,2)+'\n');
console.log('Exported original floor states',rows.length);
