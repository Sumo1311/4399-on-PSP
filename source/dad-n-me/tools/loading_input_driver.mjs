import {readNativeProbe} from './native_probe.mjs';
import {readFile,writeFile,appendFile} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
const out=process.argv[2],session=JSON.parse(await readFile(join(out,'session.json'),'utf8'));
const ws=new WebSocket('ws://[::1]:'+session.port+'/debugger','debugger.ppsspp.org');
await new Promise((ok,no)=>{ws.addEventListener('open',ok,{once:true});ws.addEventListener('error',no,{once:true});});
let seq=0;const pending=new Map();
ws.addEventListener('message',({data})=>{const r=JSON.parse(data);pending.get(r.ticket)?.(r);});
function call(event,args={}) {if(event==='memory.read'&&session.nativeProbeSnapshot)return readNativeProbe(session,args.address,args.size);return new Promise((ok,no)=>{
    const ticket=++seq,timer=setTimeout(()=>{pending.delete(ticket);no(Error(event+' timeout'));},30000);
    pending.set(ticket,r=>{clearTimeout(timer);pending.delete(ticket);r.event==='error'?no(Error(JSON.stringify(r))):ok(r);});
    ws.send(JSON.stringify({event,ticket,...args}));
});}
async function state(){
    const r=await call('memory.read',{address:session.address,size:64}),b=Buffer.from(r.base64,'base64');
    const presentation=session.presentationAddress?Buffer.from((await call('memory.read',{address:session.presentationAddress,size:20})).base64,'base64'):null;
    return {presentation:presentation?{sourceTick:presentation.readUInt32LE(0),presentations:presentation.readUInt32LE(4),midpoint:presentation.readUInt32LE(8),movingSamples:presentation.readUInt32LE(12),vcount:presentation.readUInt32LE(16)}:null,tick:b.readUInt32LE(0),rendered:b.readUInt32LE(4),buttons:b.readUInt32LE(8),phase:b.readUInt32LE(12),x:b.readFloatLE(16),y:b.readFloatLE(20),health:b.readFloatLE(24),camera:b.readFloatLE(28),freeMemory:b.readUInt32LE(32),checkpoint:b.readUInt32LE(36),kills:b.readUInt32LE(40),goal:b.readUInt32LE(44),combo:b.readUInt32LE(48),heroFrame:b.readFloatLE(52),replayDone:!!b.readUInt32LE(56),heapFree:b.readUInt32LE(60)};
}
const rows=[];
async function mark(name){
    const row={name,seconds:performance.now()/1000,state:await state().catch(e=>({error:String(e)}))};rows.push(row);console.log(JSON.stringify(row));
    await writeFile(join(out,'observations.json'),JSON.stringify(rows,null,2)+'\n');

}
async function waitTick(tick){for(let i=0,attempts=Math.max(240,Math.ceil(tick/30*3+60));i<attempts;i++){try{const s=await state();if(i%20===0)console.log('Progress',JSON.stringify(s));if(s.tick>=tick)return;}catch(e){if(!String(e).includes('CPU not started'))throw e;}await delay(500);}throw Error('tick '+tick+' timeout');}

async function waitFor(test,label,limit=60000){const start=performance.now();while(performance.now()-start<limit){let s=await state();if(test(s))return s;await delay(10);}throw Error('Timed out: '+label);}
async function buttons(mask){const b={left:!!(mask&0x80),right:!!(mask&0x20),up:!!(mask&0x10),down:!!(mask&0x40),cross:!!(mask&0x4000),square:!!(mask&0x8000)};await call('input.buttons.send',{buttons:b});}
try{
 await waitTick(3);
 // An input-only deterministic prefix reaches Game Over; native QA then
 // detaches it. Subsequent events use the normal physical-pad adapter.
 await waitFor(s=>s.tick>=860&&s.phase===5,'deterministic prefix handoff');
 await buttons(0);await waitFor(s=>s.phase===5,'original Game Over');await mark('gameover');await delay(4200);
 await buttons(0xc000);await mark('restart buttons held');
 const started=performance.now();let load=null;
 while(performance.now()-started<15000){try{let lines=(await readFile(join(session.install,'LOADING.CSV'),'utf8')).trim().split('\n');if(lines.length>=2){load=lines.at(-1).split(',');break;}}catch{}await delay(25);}
 if(!load)throw Error('Actual restart initialization did not complete');
 const loaded=await waitFor(s=>s.phase===2,'new game after loading');await waitFor(s=>s.phase===2&&s.health===1&&s.heroFrame===1,'original player entrance animation complete',18000);const ready=await state();await waitFor(s=>s.tick>=ready.tick+15,'held buttons after player entrance');const suppressed=await state();
 if(suppressed.buttons&0xc000||suppressed.heroFrame!==1)throw Error('Loading-held attack buttons leaked into the idle player');await mark('held attacks suppressed after initialization');
 await buttons(0);await delay(150);await buttons(0x4000);await waitFor(s=>!!(s.buttons&0x4000),'fresh Cross after release');await mark('fresh light attack accepted');await buttons(0);await delay(900);
 await buttons(0x8000);await waitFor(s=>!!(s.buttons&0x8000),'fresh Square after release');await mark('fresh heavy attack accepted');await buttons(0);await delay(900);
 const before=await state();await buttons(0x20);await waitFor(s=>s.tick>=before.tick+10,'movement after loading');await buttons(0);const moved=await state();if(Math.abs(moved.x-before.x)<.01)throw Error('Player did not move after loading');await mark('movement and logic resumed');
 await writeFile(join(out,'smoke-result.json'),JSON.stringify({status:'pass',scope:'Native PSP-2000 emulator, physical-pad events with explicit low-health SWF fixture; actual restart barrier, held-key suppression, release/fresh attacks and movement; not physical PSP or full waveform parity',fixture:session.fixture,model:session.model,replay:session.replay,replayHandoffTick:855,observations:rows,loadingRow:load,package:session.package},null,2)+'\n');
 process.exit(0);
}catch(error){console.error(error);await mark('failure').catch(()=>{});process.exit(1);}
