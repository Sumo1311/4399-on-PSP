// SPDX-License-Identifier: GPL-3.0-or-later
import fs from'node:fs';import path from'node:path';import crypto from'node:crypto';import{methodAdapter}from'./source_method_adapter.mjs';
const[reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const k='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={main:k+'ctrler/game_ctrls/GameMainLogicCtrl.as',stage:k+'stage/GameStage.as',ctrl:k+'ctrler/game_ctrls/GameCtrl.as',team:k+'data/vos/TeamVO.as',map:k+'data/TeamMap.as'};
const sources=Object.fromEntries(Object.entries(files).map(([key,p])=>[key,fs.readFileSync(path.join(reference,p),'utf8')]));
function vector(){return new Proxy([],{get(a,key,r){if(typeof key==='string'&&/^\d+$/.test(key)&&Number(key)>=a.length)throw new RangeError('Vector index');return Reflect.get(a,key,r);}});}
class MovieClip{stopAllMovieClips(){}gotoAndStop(){}}
class BaseGameSprite{}
class FighterMain extends BaseGameSprite{}
class Bullet{}
let spriteMethods,teamMethods,mapMethods,stageMethods,ctrlMethods,mainMethods;
const GameData={I:{config:{soundVolume:1}}},GameConfig={SPEED_PLUS:1,G:4},GameLogic={fixGameSpritePosition(){},isInAir(){return false;}},Debugger={log(){}},TraceLang=()=>{},GetLang=()=>'';
const globals={MovieClip,BaseGameSprite,FighterMain,Bullet,GameData,GameConfig,GameLogic,Debugger,TraceLang,GetLang,BODY_SKIP_DIST:200,HIT_SKIP_DIST:600};
const method=methodAdapter(sources,globals);
teamMethods=Object.fromEntries(['addChild','removeChild'].map(n=>[n,method('team',n)]));
mapMethods=Object.fromEntries(['getTeam','getOtherTeams','add','remove','refreshTeams'].map(n=>[n,method('map',n)]));
stageMethods=Object.fromEntries(['getGameSprites','addGameSprite','addGameSpriteAt','removeGameSprite'].map(n=>[n,method('stage',n)]));
ctrlMethods=Object.fromEntries(['addGameSprite','removeGameSprite'].map(n=>[n,method('ctrl',n)]));
mainMethods=Object.fromEntries(['renderMainLogic','renderGameSprite','farApart'].map(n=>[n,method('main',n)]));
function team(id){return {id,children:vector(),...teamMethods};}
let random=0x5d630063;function next(){random^=random<<13;random^=random>>>17;random^=random<<5;return random>>>0;}
const rows=[];let trace=[],action,used=false,sprites,stage,ctrl,map,main;
function operate(op,key,id){const sp=sprites[key];switch(op){
 case 1:ctrl.addGameSprite(id,sp);break;
 case 2:ctrl.removeGameSprite(sp,false);break;
 case 3:stage.removeGameSprite(sp);break;
 case 4:{const t=map.getTeam(id);if(t)map.remove(t);break;}
 case 5:map.add(team(id));break;
 case 6:sp.dead=true;break;
 case 7:ctrl.removeGameSprite(sp,true);break;
 case 8:{const t=map.getTeam(id);if(t)t.removeChild(sp);break;}
 case 9:{const t=map.getTeam(id);if(t)t.addChild(sp);break;}
 case 10:stage.addGameSpriteAt(sp,0);break;
}}
function event(phase,a,b=0){trace.push(phase*4096+a*16+b);if(!used&&phase===action[0]&&a===action[1]){used=true;operate(action[2],action[3],action[4]);}}
function initialize(){
 trace=[];used=true;
 map={_teamObj:{},teams:vector(),...mapMethods};map.add(team(0));map.add(team(1));
 stage={...stageMethods,_gameSprites:vector(),_playerLayer:{addChild(){},addChildAt(){},removeChild(){}}};ctrl={...ctrlMethods,gameState:stage,_teamMap:map};
 sprites=Array.from({length:12},(_,key)=>{const sp=key%3===1?new FighterMain:key%3===2?new Bullet:new BaseGameSprite;return Object.assign(sp,{key,x:key*7,y:0,team:null,active:false,dead:false,display:new MovieClip,setActive(v){this.active=v;},setVolume(){},setSpeedRate(){},getDisplay(){return this.display;},applayG(){},setInAir(){},isDestroyed(){return this.dead;},destroy(dispose){if(key%3!==1||dispose)this.dead=true;},render(){if(!this.dead)event(2,key);},renderAnimate(){event(3,key);}});});
 for(let key=0;key<4;++key)ctrl.addGameSprite(key%2,sprites[key]);
 main={_teamMap:map,_gameState:stage,_renderAnimate:false,...mainMethods,checkBodyHit(a,b){event(4,a.key,b.key);},checkHit(a,b){event(5,a.key,b.key);}};
 const render=main.renderGameSprite;main.renderGameSprite=function(sp){event(1,sp.key);render.call(this,sp);};
}
function snapshot(fault){const active=sprites.reduce((n,sp,i)=>n|(sp.active?1<<i:0),0),dead=sprites.reduce((n,sp,i)=>n|(sp.dead?1<<i:0),0);const out=[fault,Number(main._renderAnimate),active,dead];
 out.push(stage._gameSprites.length,...stage._gameSprites.map(sp=>sp.key));
 for(let id=0;id<3;++id){const t=map.getTeam(id);out.push(t?t.children.length:-1);if(t)out.push(...t.children.map(sp=>sp.key));}
 out.push(trace.length,...trace);return out;
}
for(let scenario=0;scenario<512;++scenario){initialize();for(let tick=0;tick<24;++tick){
 let requested=next()%2,op=next()%11,key=next()%12,id=next()%3;action=[1+next()%5,next()%12,next()%11,next()%12,next()%3];
 // Hand-authored opening cases pin self removal, appended render visits,
 // captured team vectors, captured second-child lengths and live first-child lengths.
 if(scenario<10&&tick===0){const cases=[
  [[0,0,0],[2,0,2,0,0]],[[0,0,0],[2,0,1,4,0]],[[0,0,0],[2,0,4,0,1]],
  [[0,0,0],[4,0,2,1,0]],[[0,0,0],[4,0,1,5,1]],[[0,0,0],[4,0,1,6,0]],
  [[0,0,0],[2,0,5,0,1]],[[0,0,0],[5,0,2,0,0]],[[1,4,0],[3,0,3,0,0]],[[0,0,0],[2,1,2,0,0]]
 ];[op,key,id]=cases[scenario][0];action=cases[scenario][1];requested=1;}
 const input=[scenario,tick,requested,op,key,id,...action];used=true;operate(op,key,id);trace=[];used=false;if(requested)main._renderAnimate=true;let fault=0;
 try{main.renderMainLogic();}catch(error){if(!(error instanceof RangeError))throw error;fault=1;}
 rows.push({input,output:snapshot(fault)});
}}
const result={rows:rows.length,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),scope:'Mechanically adapted original GameStage add/remove methods, GameCtrl registration/removal, TeamVO membership, TeamMap refresh and GameMainLogicCtrl traversal/renderGameSprite. Declared numeric Object-key enumeration, strict indexed Vector bounds, retained sprite/display fixtures, no-op position/gravity/volume/layer stubs, and traced body/hit callback mutations. Sprite flags distinguish FighterMain destroy(false) from BaseGameSprite/Bullet. No full Flash VM, arbitrary display-layer geometry, real script lifetimes or physical performance proof.',cases:rows};
fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>[...r.input,r.output.length,...r.output].join(' ')).join('\n')+'\n');
console.log('exported',rows.length,'scene frames',rows.filter(r=>r.output[0]).length,'Vector range exits');
