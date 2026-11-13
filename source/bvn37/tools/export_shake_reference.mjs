// Execute the original EffectShakeHandler methods with declared AS3 int coercions.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference, output] = process.argv.slice(2);
if (!output) throw Error('usage: export_shake_reference.mjs reference SHAKE.IN');
const relative='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/effect/EffectShakeHandler.as';
const bytes=fs.readFileSync(path.join(reference,relative));
const source=bytes.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
const methods=[];
for(const m of source.matchAll(/(?:public|private) function (\w+)\(([^)]*)\):\w+\s*\{/g)) {
    let end=m.index+m[0].length,depth=1;
    const start=end;
    for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
    const args=m[2].split(',').map(v=>v.trim()).filter(Boolean).map(v=>v.replace(/:\w+/,''));
    const body=source.slice(start,end-1).replace(/var (\w+):\w+/g,'var $1');
    methods.push([m[1],Function('EffectCtrl','GameConfig',`return function(${args}){with(this){${body}}}`)
        ({SHAKE_ENABLED:true},{FPS_ANIMATE:30})]);
}
function handler(){
    const h={_gameStage:null};
    for(const m of source.matchAll(/private (?:var|const) (\w+):int\s*=\s*(-?\d+)/g)) {
        let value=Number(m[2]);
        Object.defineProperty(h,m[1],{get:()=>value,set:v=>{value=v|0;}});
    }
    for(const [name,fn] of methods)h[name]=fn;
    return h;
}
let seed=0x1349baed;
const random=()=>{seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;return seed>>>0;};
const levels=[-14,-6,-3.75,-2.5,-.75,0,.5,1,2.25,3,6,10,15];
const times=[-1,0,1,17,100,267,400,500,1000,2000];
const lines=[];let xStates=0,yStates=0,bothStates=0;
for(let scenario=0;scenario<72;scenario++){
    const stage={x:0,y:0},h=handler();h.initialize(stage);
    for(let tick=0;tick<180;tick++){
        const hold=tick%19===0?1:tick%31===0?2:0;
        let hx=hold===1?levels[random()%levels.length]:0;
        let hy=hold===1?levels[random()%levels.length]:0;
        if(hold===1&&!hx&&!hy)hy=3;
        const pulse=tick%3===0||tick%7===0?1:0;
        const px=levels[random()%levels.length],py=levels[random()%levels.length];
        const time=times[random()%times.length];
        const animate=(tick+scenario)%3!==0?1:0;
        if(hold===1)h.startShake(hx,hy);else if(hold===2)h.endShake();
        if(pulse)h.shake(px,py,time);
        const before=[stage.x,stage.y];
        if(animate)h.renderAnimate();
        if(stage.x)xStates++;if(stage.y)yStates++;if(stage.x&&stage.y)bothStates++;
        lines.push([scenario,tick,hold,hx,hy,pulse,px,py,time,animate,...before,stage.x,stage.y].join(' '));
    }
}
fs.writeFileSync(output,`${lines.length}\n${lines.join('\n')}\n`);
const sha=data=>crypto.createHash('sha256').update(data).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({
    rows:lines.length,scenarios:72,ticksPerScenario:180,xStates,yStates,bothStates,
    source:relative,sourceSha256:sha(bytes),fixtureSha256:sha(fs.readFileSync(output)),
    methods:methods.map(([name])=>name),
    scope:'Original AS3 methods executed after type removal; private int setters coerce signed 32-bit; '
        +'FPS_ANIMATE=30, SHAKE_ENABLED=true, plain stage x/y object. No Flash VM or display tree execution.'
},null,2)+'\n');
console.log(`Exported ${lines.length} source shake states in 72 scenarios`);
