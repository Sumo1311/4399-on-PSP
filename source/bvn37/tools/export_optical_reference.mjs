// SPDX-License-Identifier: GPL-3.0-or-later
// Run original map optical/caching methods with declared display/bitmap stubs.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,art,output]=process.argv.slice(2);if(!output)throw Error('usage: reference art OPTICAL.IN');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/map/';
const sources=Object.fromEntries(['MapLayer.as','MapMain.as'].map(n=>[n,fs.readFileSync(path.join(reference,base+n))]));
class Rectangle{
 constructor(x=0,y=0,width=0,height=0){this.setTo(x,y,width,height);}
 setTo(x,y,width,height){Object.assign(this,{x,y,width,height});}
 isEmpty(){return this.width<=0||this.height<=0;}
 intersects(r){return this.x+this.width>r.x&&r.x+r.width>this.x&&this.y+this.height>r.y&&r.y+r.height>this.y;}
}
class Sprite{constructor(children){this.children=children;this.x=this.y=0;this.numChildren=children.length;}getChildAt(i){return this.children[i];}}
class MovieClip{constructor(bounds){this.bounds=bounds;this.alpha=1;}getBounds(){return this.bounds;}}
class BlurFilter{constructor(x,y,quality){Object.assign(this,{x,y,quality});}}
class Point{constructor(x=0,y=0){Object.assign(this,{x,y});}}
function method(file,name,checkHitGameSprite){
 const source=sources[file].toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const m=new RegExp(`(?:public|private)(?: static)? function ${name}\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{`).exec(source);if(!m)throw Error(name);
 let end=m.index+m[0].length,depth=1;const start=end;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
 let body=source.slice(start,end-1).replace(/var (\w+):(?:Vector\.<?\w+>?|\w+)/g,'var $1')
  .replace(/for each\s*\(var (\w+) in (\w+)\)/g,'for(var $1 of $2)')
  .replace(/(\w+) is MovieClip/g,'$1 instanceof MovieClip')
  .replace(/catch\s*\((\w+):\w+\)/g,'catch($1)')
  .replace(/_view as Sprite/g,'(_view instanceof Sprite ? _view : null)');
 const args=m[1].split(',').filter(Boolean).map(x=>x.trim().replace(/:[\w.<>]+/,''));
 return Function('MovieClip','Sprite','Rectangle','BlurFilter','Point','trace','checkHitGameSprite',`return function(${args}){with(this){${body}}}`)
  (MovieClip,Sprite,Rectangle,BlurFilter,Point,()=>{},checkHitGameSprite);
}
const check=method('MapLayer.as','checkHitGameSprite');
const renderOptical=method('MapLayer.as','renderOptical',check),getBlurBitmap=method('MapLayer.as','getBlurBitmap'),setSmoothing=method('MapLayer.as','setSmoothing');
const render=method('MapMain.as','render'),mapSmoothing=method('MapMain.as','setSmoothing');
const names=fs.readdirSync(art).filter(n=>fs.existsSync(path.join(art,n,'optical.tsv'))).sort();
const lines=[String(names.length)];let total=0,half=0,captureRecords=0;
for(const name of names){
 const rows=fs.readFileSync(path.join(art,name,'optical.tsv'),'utf8').trim().split('\n').map(x=>x.split('\t'));
 const bounds=rows.filter(r=>r[0]==='child').map(r=>r.slice(4).map(Number));
 const children=bounds.map(b=>new MovieClip(new Rectangle(...b))),view=new Sprite(children);
 const mask=()=>children.reduce((n,c,i)=>n|(c.alpha===.5?1<<i:0),0);
 const layer={_view:view,_smoothing:false,_opticalFrame:0,_opticalRect:new Rectangle(),_blurBitmaps:{},x:0,y:0,enabled:true,
  OPTICAL_GAP:Number(sources['MapLayer.as'].toString().match(/OPTICAL_GAP:int\s*=\s*(\d+)/)[1]),
  renderOptical,getBlurBitmap,setSmoothing,show(bitmap){this.current=bitmap;},
  drawBitmap(){return {mask:mask(),bitmapData:{rect:{},applyFilter(){}}};}};
 let sprites=[];
 const map={frontLayer:layer,frontFixLayer:null,mapLayer:null,bgLayer:null,_defaultFrontPos:new Point(),bottom:600,
  gameState:{getGameSprites:()=>sprites},_smoothing:new Point(),render,setSmoothing:mapSmoothing};
 lines.push(`${name.toUpperCase()} ${bounds.length} 640`,...bounds.map(b=>b.join(' ')));
 for(let tick=0;tick<640;tick++){
  const cameraX=(tick%31)*75-450,cameraY=(tick%19)*37-60;
  const target=bounds[Math.floor(tick/20)%bounds.length];const shiftX=-cameraX*.1,shiftY=Math.max(0,(600-cameraY)*.1);
  const mode=tick%10;let input=[];
  if(tick%23!==0){
   const [x,y,w,h]=target;
   const rect=mode===0?[x+w+shiftX,y+shiftY,20,20]:mode===1?[x+shiftX-20,y+shiftY,20,20]:
    mode===2?[x+shiftX,y+h+shiftY,20,20]:mode===3?[x+shiftX,y+shiftY,0,20]:
    [x+shiftX+Math.min(w/2,5),y+shiftY+Math.min(h/2,5),20,20];
   input.push([+(mode!==4),+(mode===5),...rect]);
   if(tick%7===0)input.push([1,0,bounds[0][0]+shiftX+1,bounds[0][1]+shiftY+1,12,12]);
  }
  sprites=input.map(([valid,destroyed,...b])=>({isDestroyed:()=>!!destroyed,getArea:()=>valid?new Rectangle(...b):null}));
  const width=({0:3,3:4,17:6,60:4,97:8,130:10,190:3,330:10,501:6})[tick]||0;
  if(width)map.setSmoothing(width,0);
  map.render(-cameraX,-cameraY,2.2);
  const cached=[3,4,6,8,10].map(v=>layer._blurBitmaps[`${v*2}|0`]?.mask??0xffffffff);
  const captures=Object.keys(layer._blurBitmaps).length;captureRecords+=+!!width;half+=+!!mask();
  lines.push([tick,cameraX,cameraY,width,input.length,...input.flat(),mask(),layer._opticalFrame,...cached,captures].join(' '));total++;
 }
}
fs.writeFileSync(output,lines.join('\n')+'\n');const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:total,maps:names.length,nonzeroOpticalStates:half,captureInputs:captureRecords,
 sourceHashes:Object.fromEntries(Object.entries(sources).map(([n,b])=>[base+n,sha(b)])),fixtureSha256:sha(fs.readFileSync(output)),
 scope:'Mechanically adapted original MapMain.render/setSmoothing and MapLayer.renderOptical/checkHitGameSprite/setSmoothing/getBlurBitmap methods, with explicit Rectangle/display/bitmap/filter stubs and supplied game-sprite area/camera/accepted smoothing inputs. Checks two-call cadence, strict overlap edges, empty lists/areas, destroyed/null-area sprites, parallax clamp and first-use cache reuse. Bitmap contents, actor area production and full event clock are separate evidence.'},null,2)+'\n');
console.log('Exported',total,'source optical/cache states');
