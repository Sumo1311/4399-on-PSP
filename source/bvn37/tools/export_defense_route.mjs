// Source defense-handler normalization after the separately checked model/manager boundary.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const [reference,modelFile,output]=process.argv.slice(2);if(!output)throw Error('usage: export_defense_route.mjs reference model.json DEFENSE.IN');
const relative='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/effect/EffectHitHandler.as';
const bytes=fs.readFileSync(path.join(reference,relative)),model=JSON.parse(fs.readFileSync(modelFile));
const text=bytes.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
const m=/public function doDefenseEffect\(([^)]*)\):void\s*\{/.exec(text);let end=m.index+m[0].length,depth=1;const start=end;
for(;depth;end++){if(text[end]==='{')depth++;if(text[end]==='}')depth--;}
const args=m[1].split(',').map(v=>v.trim().replace(/:\w+/,''));
const body=text.slice(start,end-1).replace(/var (\w+):\w+/g,'var $1')
    .replace(/hitvo.owner is IGameSprite/g,'true').replace(/\s+as IGameSprite/g,'');
const handler={_manager:{getDefenseEffectVOByHitVO(hit,type){
    const q=model.queries.find(q=>q.kind==='defense'&&q.type===hit.hitType&&q.defense===type&&!q.enemy);
    return q?.id?structuredClone(model.effects.find(e=>e.id===q.id)):null;
}},_ctrl:{doEffectVO(effect,x,y,direct){handler.result={effect,x,y,direct};}}};
handler.run=Function(`return function(${args}){with(this){${body}}}`)();
const lines=[];let promoted=0;
for(const type of [...Array(17).keys(),255,65535])for(const defense of [-1,0,1,2])for(const facing of [-1,1]){
    handler.result=null;handler.run({hitType:type,owner:{direct:facing}},{x:1691.5,y:945,width:21,height:40},defense);
    const r=handler.result;if(!r)throw Error('unexpected absent defense');const e=r.effect,s=e.shake||{};
    if(s.y)promoted++;
    lines.push([type,defense,facing,e.id,s.x||0,s.y||0,s.time||0,r.x,r.y,r.direct].join(' '));
}
fs.writeFileSync(output,`${lines.length}\n${lines.join('\n')}\n`);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:lines.length,promoted,source:relative,sourceSha256:sha(bytes),
    modelSha256:sha(fs.readFileSync(modelFile)),fixtureSha256:sha(fs.readFileSync(output)),
    scope:'Original doDefenseEffect body executed after declared type/cast/owner interface adapter. '
        +'Manager stub supplies source-model pure selectors with a fresh clone; identity caching remains separately checked. '
        +'Null target matches FighterMcHurtCtrl. No Flash VM or complete EffectCtrl dispatch execution.'},null,2)+'\n');
console.log(`Exported ${lines.length} defense routes, ${promoted} normalized shake requests`);
