// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {methodAdapter} from './source_method_adapter.mjs';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const prefix='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={effect:prefix+'ctrler/EffectCtrl.as',events:prefix+'ctrler/musou_ctrls/MusouFighterEventCtrl.as',musou:prefix+'ctrler/musou_ctrls/MusouCtrl.as',mc:prefix+'fighter/FighterMC.as',hurt:prefix+'fighter/ctrler/mc/FighterMcHurtCtrl.as',ctrl:prefix+'fighter/ctrler/FighterMcCtrler.as'};
const source=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(path.join(reference,p),'utf8')]));
const method=methodAdapter(source,{}),render=method('effect','renderRemoveEnemy');
const trace=[];
for(const initial of [0,.01,.05,.1,.25,.5,.75,1]){
 let alpha=initial,removed=0;const display={};
 Object.defineProperty(display,'alpha',{get(){return alpha;},set(v){alpha=Math.min(1,Math.max(0,v));}});
 const f={getDisplay(){return display;}},controller={_removeEnemieMap:{one:{fighter:f,callback(){++removed;}}},render};
 for(let tick=0;tick<25;++tick){controller.render();trace.push([initial,tick,alpha,+(!!controller._removeEnemieMap.one),removed]);}
}
const report={rows:trace.length,columns:5,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),scope:'Mechanically adapted original EffectCtrl.renderRemoveEnemy with one declared map entry, callback counter and a declared clamped display alpha property. Provenance also pins the inspected synchronous FighterMC DEAD/HURT_DOWN and MusouFighterEventCtrl consumers. The numeric trace only qualifies fade subtraction/removal; actor and scene callback tests separately qualify their native integration. No Flash VM, arbitrary event listener order or raster proof.',trace};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),trace.length+'\n'+trace.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original enemy fade records',trace.length);
