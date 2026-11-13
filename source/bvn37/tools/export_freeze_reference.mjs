// Execute only the original freeze counter methods in a declared controller stub.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,output]=process.argv.slice(2);
if(!output)throw Error('usage: export_freeze_reference.mjs reference FREEZE.IN');
const relative='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/EffectCtrl.as';
const bytes=fs.readFileSync(path.join(reference,relative));
const source=bytes.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
function make(hz){
    const control={paused:false,slowRate:0,pause(){this.paused=true;},resume(){this.paused=false;}};
    const h={freezeEnabled:true,_onFreezeOver:null,_hitHandler:{clearHitFocusOnFreezeEnd(){}},_slowHandler:{bgBlur(){}}};
    let frame=0;Object.defineProperty(h,'_freezeFrame',{get:()=>frame,set:v=>{frame=v|0;}});
    for(const name of ['freeze','renderFreeze']){
        const m=new RegExp(`(?:public|private) function ${name}\\(([^)]*)\\):\\w+\\s*\\{`).exec(source);
        let end=m.index+m[0].length,depth=1;const start=end;
        for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
        const args=m[1].split(',').map(v=>v.trim().replace(/:\w+/,''));
        const body=source.slice(start,end-1).replace(/var (\w+):int\s*=\s*([^;]+);/g,'var $1=(($2)|0);')
            .replace(/var (\w+):\w+/g,'var $1');
        h[name]=Function('GameConfig','GameCtrl',`return function(${args}){with(this){${body}}}`)({FPS_GAME:hz},{I:control});
    }
    return {h,control};
}
const lines=[];
for(const hz of [30,60])for(const presteps of [0,1]){
    const scenario=(hz===30?0:2)+presteps,{h,control}=make(hz);
    h.freeze(400);let phase=((presteps+1)*hz)%60;
    for(let tick=0;tick<70;tick++){
        const before=h._freezeFrame,locked=before>(phase+hz>=60?1:0);
        phase+=hz;if(phase>=60){phase-=60;h.renderFreeze();}
        lines.push([scenario,hz,presteps,tick,before,+locked,h._freezeFrame,+control.paused].join(' '));
    }
}
fs.writeFileSync(output,`${lines.length}\n${lines.join('\n')}\n`);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:lines.length,scenarios:4,source:relative,sourceSha256:sha(bytes),
    fixtureSha256:sha(fs.readFileSync(output)),
    scope:'Only original freeze/renderFreeze counter methods; AS3 int assignment adapted; GameCtrl pause/resume stub, '
        +'slowRate=0, no callbacks. Native 60 Hz bridge schedules source 30/60 Hz calls at two phases. '
        +'Does not establish full Flash ENTER_FRAME, blur or callback ordering.'
},null,2)+'\n');
console.log(`Exported ${lines.length} source freeze-counter states`);
