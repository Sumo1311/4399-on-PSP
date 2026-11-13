// Execute the pinned buff controller, VO and view methods in declared display stubs.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference, modelFile, artFile, output] = process.argv.slice(2);
if (!output) throw Error('usage: export_buff_reference.mjs reference model.json art.json BUFF.IN');
const prefix='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/',sources={};
function source(relative) {
    const bytes=fs.readFileSync(path.join(reference,prefix+relative));
    sources[prefix+relative]=crypto.createHash('sha256').update(bytes).digest('hex');
    return bytes.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
}
function methods(text,names,env={}) {
    const result={};
    for(const name of names) {
        const m=new RegExp(`(?:public|private|protected)(?: override)? function ${name}\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{`).exec(text);
        if(!m) throw Error('missing method '+name);
        let end=m.index+m[0].length,depth=1;const start=end;
        for(;depth;end++) { if(text[end]==='{')depth++;if(text[end]==='}')depth--; }
        const args=m[1].split(',').filter(Boolean).map(v=>v.trim().replace(/:\w+/,''));
        const body=text.slice(start,end-1).replace(/var (\w+):int\s*;/g,'var $1=0;')
            .replace(/var (\w+):[\w.]+(?:<[^>]+>)?/g,'var $1')
            .replace(/for each\(var (\w+) in _onRemoveFuncs\)/g,'for(var $1 of (_onRemoveFuncs||[]))')
            .replace(/v is FighterMain/g,'v instanceof FighterMain').replace(/\s+as FighterMain/g,'')
            .replace(/frame is int/g,'Number.isInteger(frame)').replace(/frame is String/g,'typeof frame === "string"')
            .replace(/super\.(\w+)\(([^)]*)\)/g,'Base.$1.call(this,$2)')
            .replace(/catch\s*\((\w+):\w+\)/g,'catch($1)');
        result[name]=Function(...Object.keys(env),`return function(${args}){with(this){${body}}}`)(...Object.values(env));
    }
    return result;
}
const model=JSON.parse(fs.readFileSync(modelFile)),art=JSON.parse(fs.readFileSync(artFile));
const effectList=[...model.effects].sort((a,b)=>a.id<b.id?-1:a.id>b.id?1:0);
const data=new Map(effectList.map((e,index)=>[e.id,{...e,index,bitmapDataCache:
    Array.from({length:art.clips.find(c=>c.className===e.className)?.cache_frames||0},()=>({bitmapData:{},offsetX:0,offsetY:0})),frameLabelCache:{}}]));
class FighterMain {
    constructor(id) { this.id=id;this.x=0;this.y=0;this.direct=id?-1:1;this.speed=7;this.attackRate=1;this.defenseRate=0; }
    changeColor() { throw Error('Unexpected authored buff color transform'); }
    resumeColor() { throw Error('Unexpected authored buff color restore'); }
}
class ColorTransform {}
const SoundCtrl={I:{playEffectSound(){},playSwcSound(){}}};
const Base=methods(source('views/effects/EffectView.as'),['start','setTarget','setPos','render','renderAnimate','gotoAndPlay',
    'renderDisplay','renderFrameLabel','removeSelf','remove'],{SoundCtrl});
const display=Base.renderDisplay;Base.renderDisplay=function(){this._displayFrame=this._curFrame;return display.call(this);};
const BuffView=methods(source('views/effects/BuffEffectView.as'),['setTarget','render','setBuff'],{Base,FighterMain,ColorTransform});
const GameConfig={FPS_GAME:30},EffectCtrl={I:{}};
const vo=methods(source('fighter/vos/FighterBuffVO.as'),['FighterBuffVO','setHold','render'],{GameConfig});
let nextBuff=0,nodes=[];
class FighterBuffVO {
    constructor(param,hold) {
        this.param=null;this.resumeValue=0;this.finished=false;this._holdFrame=1;this.id=nextBuff++;
        Object.assign(this,vo);vo.FighterBuffVO.call(this,param,hold);nodes.push(this);
    }
}
const controller=methods(source('fighter/ctrler/FighterBuffCtrler.as'),['speedUp','attackUp','defenseUp','speedDown','addBuff','render'],
    {EffectCtrl,FighterBuffVO,trace:e=>{throw e;}});
let active=[],cache=new Map(),counts=new Map();
function view(id,x,y,direct,target,buff) {
    const d=data.get(id);if(!d?.bitmapDataCache.length)throw Error('Missing buff art '+id);
    const pool=cache.get(d)||[];cache.set(d,pool);
    let v=pool.find(v=>!v.isActive);
    if(!v) {
        v={...Base,...(d.isBuff?BuffView:{}),_data:d,_bitmapDatas:d.bitmapDataCache,_frameLabels:{},
            _orgX:0,_orgY:0,_curFrame:0,_rotation:0,_direct:1,_target:null,_fighter:null,_buff:null,
            _isDestroyed:false,_onRemoveFuncs:null,display:{},isActive:false,autoRemove:true,loopPlay:!!d.isBuff,holdFrame:-1};
        pool.push(v);
    }
    v._onRemoveFuncs=[value=>{const i=active.indexOf(value);if(i>=0)active.splice(i,1);}];
    v.start(x,y,direct);active.push(v);
    if(target)v.setTarget(target);
    if(buff)v.setBuff(buff);
    return v;
}
EffectCtrl.I.doEffectById=(id,x,y)=>{const n=1+(counts.get(id)||0);counts.set(id,n);if(n<=3)view(id,x,y,1);};
EffectCtrl.I.doBuffEffect=(id,target,buff)=>view(id,target.x,target.y,target.direct,target,buff);
const lines=[];
for(const hz of [30,60])for(let kind=0;kind<3;kind++)for(let variant=0;variant<2;variant++) {
    const scenario=lines.length/240;GameConfig.FPS_GAME=hz;nextBuff=0;nodes=[];active=[];cache=new Map();counts=new Map();
    const targets=[new FighterMain(0),new FighterMain(1)];
    const controls=targets.map(t=>({...controller,_fighter:t,_buffObj:{}}));
    const up=['speedUp','attackUp','defenseUp'][kind],param=['speed','attackRate','defenseRate'][kind];
    let phase=0;
    for(let tick=0;tick<240;tick++) {
        for(let i=0;i<2;i++) {targets[i].x=100+i*80+tick*(i?-0.25:0.5);targets[i].y=200+i*30-tick*0.125;targets[i].direct=(tick%40<20?1:-1)*(i?-1:1);}
        let target=0,action=0,count=0,hold=0,value=0;
        if(tick===0) {action=1;count=1;hold=1.5;value=kind?0.3:2.5;}
        if(tick===20) {action=1;count=variant?4:1;hold=2.4;value=kind?0.6:1.5;}
        if(tick===40) {action=1;target=1;count=1;hold=0.5;value=kind?0.1:2;}
        if(tick===125) {action=1;target=1;count=1;hold=0.8;value=kind?0.2:3;}
        if(tick===180&&kind===0) {action=2;count=1;hold=0.3;value=4;}
        for(let n=0;n<count;n++) controls[target][action===2?'speedDown':up](value,hold);
        const node=action?controls[target]._buffObj[param]:null;
        const animate=variant&&tick>=64&&tick<100?tick%4===0:tick%2===0;
        const input=[scenario,hz,tick,+animate,kind,action,count,target,node?.id??65535];
        for(const t of targets)input.push(t.x,t.y,t.direct);
        input.push(nodes.length,...nodes.map(n=>+n.finished));
        phase+=hz;
        if(phase>=60) {phase-=60;counts.clear();for(let i=0;i<active.length;i++)active[i].render();}
        if(animate)for(let i=0;i<active.length;i++)active[i].renderAnimate();
        input.push(active.length);
        for(const v of active) input.push(v._data.index,v._target?.id??65535,v._buff?.id??65535,
            v._orgX,v._orgY,v.display.scaleX,v._curFrame,v._displayFrame);
        lines.push(input.join(' '));
        // Declared post-effects actor phase. The fixture feeds these VO states
        // to native views; it does not establish the full actor-controller clock.
        if((tick+1)*hz%60===0 && !(variant&&tick>=80&&tick<110)) for(const c of controls)c.render();
    }
}
fs.writeFileSync(output,`${lines.length}\n${lines.join('\n')}\n`);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:lines.length,scenarios:12,sourceHashes:sources,
    fixtureSha256:sha(fs.readFileSync(output)),modelSha256:sha(fs.readFileSync(modelFile)),artSha256:sha(fs.readFileSync(artFile)),
    scope:'Pinned BuffEffectView, EffectView, FighterBuffCtrler and FighterBuffVO methods after declared AS3 adapters, bitmap metadata and display stubs. '
        +'Tests follow, looping, renewal, duplicate views, expiry, forward-removal skips and pool reuse with observed VO completion inputs. '
        +'Actor phase, pauses and animation calls are declared fixture inputs, not full original controller scheduling or Flash VM execution.'},null,2)+'\n');
console.log(`Exported ${lines.length} original buff view states`);
