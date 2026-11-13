// Original EffectManager identity-cache methods, with declared data-only stubs.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,modelFile,output]=process.argv.slice(2);
if(!output)throw Error('usage: export_combat_cache_reference.mjs reference model.json CACHE.IN');
const relative='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/effect/EffectManager.as';
const bytes=fs.readFileSync(path.join(reference,relative));
const source=bytes.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
const model=JSON.parse(fs.readFileSync(modelFile));
const effects=new Map(model.effects.map(e=>[e.id,e]));
const lookup=(kind,type,defense,enemy)=>model.queries.find(q=>q.kind===kind&&q.type===type&&q.defense===defense&&q.enemy===enemy)?.id;
const tags=new WeakMap();
function effect(id){
    if(!id)return null;
    const v=structuredClone(effects.get(id));tags.set(v,id);
    v.clone=()=>effect(id);return v;
}
const EffectModel={I:{
    getHitEffect:type=>effect(lookup('hit',type,0,false)),
    getMusouEnemyHitEffect:type=>effect(lookup('hit',type,0,true)),
    getDefenseEffect:(type,defense)=>effect(lookup('defense',type,defense,false)),
    getMusouEnemyDefenseEffect:(type,defense)=>effect(lookup('defense',type,defense,true))
}};
class FighterMain{constructor(enemy){this.enemy=enemy;}isMusouEnemy(){return this.enemy;}}
class EffectCacheVO{normal=null;musouEnemy=null;}
const manager={_hitCache:{},_defCache:{}};const methods=[];
for(const name of ['getHitEffectVOByHitVO','getDefenseEffectVOByHitVO']){
    const re=new RegExp(`public function ${name}\\(([^)]*)\\):\\w+\\s*\\{`),m=re.exec(source);
    if(!m)throw Error('missing method '+name);
    let end=m.index+m[0].length,depth=1;const start=end;
    for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
    const args=m[1].split(',').map(v=>v.trim().replace(/:\w+/,''));
    const body=source.slice(start,end-1).replace(/var (\w+):\w+/g,'var $1')
        .replace(/target is FighterMain/g,'target instanceof FighterMain').replace(/\s+as (?:FighterMain|EffectVO)/g,'');
    manager[name]=Function('EffectModel','FighterMain','EffectCacheVO',`return function(${args}){with(this){${body}}}`)
        (EffectModel,FighterMain,EffectCacheVO);methods.push(name);
}
const hits=new Map(),lines=[];let clones=0,normalized=0;
for(let row=0;row<320;row++){
    const key=row%13,clone=row>20&&row%17===0?(key+5)%13:-1;
    if(!hits.has(key)||clone>=0){
        const hit={customEffectVO:null,[Symbol.toPrimitive]:()=>`hit-${row}`};hits.set(key,hit);
        if(clone>=0)clones++;
    }
    const hit=hits.get(key),type=(row*7+Math.floor(row/13))%17;
    const mode=row%3===0?1:0,enemy=!!(row%4<2),defense=row%4-1;hit.hitType=type;
    const selected=mode?manager.getDefenseEffectVOByHitVO(hit,defense,null):manager.getHitEffectVOByHitVO(hit,new FighterMain(enemy));
    const id=selected?tags.get(selected):'-';
    const [x,y,time]=selected?.shake?[selected.shake.x??0,selected.shake.y??0,selected.shake.time??0]:[0,0,0];
    if(y)normalized++;
    lines.push([mode,key,clone,type,+enemy,defense,id,x,y,time].join(' '));
}
fs.writeFileSync(output,`${lines.length}\n${lines.join('\n')}\n`);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:lines.length,clones,normalized,
    source:relative,sourceSha256:sha(bytes),modelSha256:sha(fs.readFileSync(modelFile)),fixtureSha256:sha(fs.readFileSync(output)),methods,
    scope:'Original EffectManager hit/defense methods executed after type/cast removal; dictionary object keys get unique '
        +'identity strings. Data-only EffectVO clone stub preserves definition values; bitmap/deep display cloning not verified. '
        +'Model values/pure queries come from separately executed source initializer. Defense called with null target, matching FighterMcHurtCtrl.'
},null,2)+'\n');
console.log(`Exported ${lines.length} original cache selections and normalized shake parameters`);
