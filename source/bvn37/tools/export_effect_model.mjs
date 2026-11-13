// Execute the pinned AS3 EffectModel initializers and pure selectors in a declared adapter.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference, output] = process.argv.slice(2);
if (!reference || !output) throw new Error('usage: export_effect_model.mjs reference output.json');
const kernel='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const shared='upstream/CORE_Shared/src/net/play5d/game/bvn/';
const inputs=[kernel+'data/EffectModel.as',kernel+'data/vos/EffectVO.as',shared+'data/HitType.as',
              shared+'data/fighter/FighterDefenseType.as'];
const read=p=>fs.readFileSync(path.join(reference,p),'utf8');
const digest=p=>crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex');
const clean=s=>s.replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
const source=clean(read(inputs[0]));
const constants=p=>Object.fromEntries([...read(p).matchAll(/public static const (\w+):int\s*=\s*(\d+)/g)]
                                      .map(m=>[m[1],Number(m[2])]));
const HitType=constants(inputs[2]), FighterDefenseType=constants(inputs[3]);
const BlendMode=Object.fromEntries(['NORMAL','ADD','LIGHTEN','HARDLIGHT','SCREEN'].map(k=>[k,k.toLowerCase()]));
const voDefaults={};
for(const m of clean(read(inputs[1])).matchAll(/public var (\w+):(\w+)(?:\.<[^>]+>)?\s*(?:=\s*([^;]+))?;/g)) {
  const [,key,type,initial]=m;
  if(['bitmapDataCache','frameLabelCache'].includes(key)) continue;
  voDefaults[key]=initial ? Function('BlendMode',`return ${initial}`)(BlendMode) :
      type==='Boolean'?false:type==='int'?0:null;
}
function EffectVO(className,param={}) { Object.assign(this,voDefaults,{className},param); }
const model={_effect:null};
const methods=[];
for(const m of source.matchAll(/(?:public|private) function (\w+)\(([^)]*)\):\w+\s*\{/g)) {
  const name=m[1];
  if(['initialize','cacheEffects'].includes(name)) continue;
  let pos=m.index+m[0].length, depth=1, end=pos;
  for(;depth;end++) {
    if(source[end]==='{')depth++;
    if(source[end]==='}')depth--;
    if(end>=source.length)throw new Error('unbalanced method');
  }
  const body=source.slice(pos,end-1).replace(/var (\w+):\w+/g,'var $1');
  const args=m[2].split(',').map(s=>s.trim()).filter(Boolean).map(s=>s.replace(/:\w+/,'').trim());
  model[name]=Function('EffectVO','HitType','FighterDefenseType','BlendMode',
      `return function(${args.join(',')}) { with(this) { ${body} } }`)(EffectVO,HitType,FighterDefenseType,BlendMode);
  methods.push(name);
}
model.initEffects();
const effects=Object.entries(model._effect).sort(([a],[b])=>a.localeCompare(b,'en')).map(([id,v])=>({id,...v}));
// Exhaust the source's public pure selectors. EffectManager's identity caches and
// custom hit overrides are outside this adapter and remain separate integration work.
const queries=[];
for(const type of [-1,...Array.from({length:17},(_,i)=>i),255,65535]) {
  for(const enemy of [false,true]) {
    const selected=enemy?model.getMusouEnemyHitEffect(type):model.getHitEffect(type);
    const key=selected?effects.find(e=>model._effect[e.id]===selected).id:null;
    queries.push({kind:'hit',type,enemy,defense:0,id:key});
    for(const defense of [-1,0,1,2]) {
      const value=enemy?model.getMusouEnemyDefenseEffect(type,defense):model.getDefenseEffect(type,defense);
      queries.push({kind:'defense',type,enemy,defense,id:value?effects.find(e=>model._effect[e.id]===value).id:null});
    }
  }
}
fs.writeFileSync(output,JSON.stringify({
  scope:'Original EffectModel initializer and pure lookup methods executed after AS3 type removal; '
       +'EffectVO value fields/defaults adapted, no bitmap cache, identity cache, display, sound or Flash VM execution',
  methods,constants:{HitType,FighterDefenseType},effects,queries,
  sources:Object.fromEntries(inputs.map(p=>[p,digest(p)]))
},null,2)+'\n');
console.log(`Exported ${effects.length} source effect definitions and ${queries.length} pure selection queries`);
