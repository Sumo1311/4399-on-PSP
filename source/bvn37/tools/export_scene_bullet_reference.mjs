// SPDX-License-Identifier: GPL-3.0-or-later
import fs from'node:fs';import path from'node:path';import crypto from'node:crypto';import{methodAdapter}from'./source_method_adapter.mjs';
const[reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const file='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/fighter/Bullet.as',source=fs.readFileSync(path.join(reference,file),'utf8');class Bullet{}
const beHit=methodAdapter({bullet:source},{Bullet})('bullet','beHit');let random=0x63b01137;function next(){random^=random<<13;random^=random>>>17;random^=random<<5;return random>>>0;}
const powers=[.125,.25,.5,1,3.9,10.3,49.6,99.9,100,100.5,1000000000.5,2147483648.125,4294967295.75],rows=[];let hp,hit;
for(let session=0;session<1024;++session){hp=100;hit=false;const bullet={doHit(){hit=true;}};Object.defineProperty(bullet,'hp',{get(){return hp;},set(value){hp=value|0;}});
 for(let step=0;step<8;++step){const power=powers[next()%powers.length],fromBullet=next()%4===0;const owner=fromBullet?Object.assign(new Bullet,{hpMax:100}):{};beHit.call(bullet,{power,owner});rows.push([session,step,power,Number(fromBullet),hp,Number(hit)]);}}
const result={rows:rows.length,columns:6,sourceHashes:{[file]:crypto.createHash('sha256').update(source).digest('hex')},scope:'Original Bullet.beHit with declared Bullet type, a typed int hp property, hpMax=100 and a doHit flag adapter. Covers subtract-then-int-coerce, repeated impacts and Bullet-owner hpMax damage. No MovieClip hit-frame, onHit callback or raster proof.',trace:rows};fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>r.join(' ')).join('\n')+'\n');console.log('exported',rows.length,'Bullet HP states');
