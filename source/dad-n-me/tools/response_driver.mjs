import {readNativeProbe} from './native_probe.mjs';
import {readFile,writeFile,appendFile} from 'node:fs/promises';
import {join} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
const out=process.argv[2],session=JSON.parse(await readFile(join(out,'session.json'),'utf8'));
const ws=new WebSocket('ws://[::1]:'+session.port+'/debugger','debugger.ppsspp.org');
await new Promise((ok,no)=>{ws.addEventListener('open',ok,{once:true});ws.addEventListener('error',no,{once:true});});
let ticket=0;const pending=new Map();
ws.addEventListener('message',({data})=>{const r=JSON.parse(data);pending.get(r.ticket)?.(r);});
function call(event,args={}){return new Promise((ok,no)=>{
    const id=++ticket,timer=setTimeout(()=>{pending.delete(id);no(Error(event+' timeout'));},15000);
    pending.set(id,r=>{clearTimeout(timer);pending.delete(id);r.event==='error'?no(Error(JSON.stringify(r))):ok(r);});
    ws.send(JSON.stringify({event,ticket:id,...args}));
});}
async function state(){
    const b=Buffer.from((await readNativeProbe(session,session.address,68)).base64,'base64');
    return {tick:b.readUInt32LE(0),rendered:b.readUInt32LE(4),buttons:b.readUInt32LE(8),phase:b.readUInt32LE(12),x:b.readFloatLE(16),y:b.readFloatLE(20),health:b.readFloatLE(24),heroFrame:b.readFloatLE(52),heapFree:b.readUInt32LE(60),presented:b.readUInt32LE(64)};
}
async function until(fn,seconds=90){const end=performance.now()+seconds*1000;while(performance.now()<end){try{const s=await state();if(fn(s))return s;}catch{}await delay(30);}throw Error('Progress timeout');}
const events=[],observations=[];
async function mark(name){const s=await state();observations.push({name,host_ms:performance.now(),state:s});console.log(name,JSON.stringify(s));await writeFile(join(out,'controls-observations.json'),JSON.stringify(observations,null,2)+'\n');return s;}
async function buttons(value,label){const before=performance.now();await call('input.buttons.send',{buttons:value});events.push({label,buttons:value,sent_host_ms:before,ack_host_ms:performance.now()});await appendFile(join(out,'input-events.jsonl'),JSON.stringify(events.at(-1))+'\n');}
async function pulse(key,ms,label){await buttons({[key]:true},label+'-down');await delay(ms);await buttons({[key]:false},label+'-up');await delay(100);}
try {
    await until(s=>s.phase===2&&s.health>0&&s.heroFrame===1);await mark('live-start');
    const start=await mark('before-walk');
    await buttons({right:true},'walk-right-down');await delay(350);await buttons({right:false},'walk-right-up');await delay(200);
    const right=await mark('walked-right');
    if(!(right.x>start.x+1))throw Error('Right movement did not advance hero');
    await buttons({left:true},'walk-left-down');await delay(350);await buttons({left:false},'walk-left-up');await delay(200);
    const left=await mark('walked-left');
    if(!(left.x<right.x-1))throw Error('Left movement did not reverse hero');
    for(const key of ['cross','square','right','left'])for(const ms of [16,24,32,48,80])await pulse(key,ms,key+'-'+ms);
    await buttons({cross:true,square:true},'heavy-fight-down');await delay(3000);await buttons({cross:false,square:false},'heavy-fight-up');await delay(300);
    await mark('after-combat');
    await pulse('start',80,'pause');await delay(250);const paused=await mark('paused');await delay(400);const held=await mark('still-paused');
    if(paused.tick!==held.tick)throw Error('Pause failed to stop logical timeline');
    await pulse('cross',80,'resume');await delay(500);const resumed=await mark('resumed');
    if(resumed.tick<=held.tick+5)throw Error('Timeline did not resume');
    await writeFile(join(out,'response-controls.json'),JSON.stringify({status:'pass',scope:'Fresh original package controlled only by emulated controller events; no state writes, replay or fast-forward. Sample-to-present telemetry is not hardware photon latency.',events,observations},null,2)+'\n');
    process.exit(0);
}catch(e){await writeFile(join(out,'response-controls.json'),JSON.stringify({status:'fail',error:String(e),events,observations},null,2)+'\n');console.error(e);process.exit(1);}
