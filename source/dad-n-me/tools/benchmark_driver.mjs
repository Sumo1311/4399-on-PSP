import {readFile, writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
const out=process.argv[2],session=JSON.parse(await readFile(join(out,'session.json'),'utf8'));
const ws=new WebSocket('ws://[::1]:'+session.port+'/debugger','debugger.ppsspp.org');
await new Promise((ok,no)=>{ws.addEventListener('open',ok,{once:true});ws.addEventListener('error',no,{once:true});});
let ticket=0,sawGame=false,quit=false;const pending=new Map(),observations=[];
ws.addEventListener('message',({data})=>{const r=JSON.parse(data);if(r.event==='game.quit')quit=true;pending.get(r.ticket)?.(r);});
function status(){return new Promise((ok,no)=>{const id=++ticket,timer=setTimeout(()=>no(Error('game.status timeout')),5000);
  pending.set(id,r=>{clearTimeout(timer);pending.delete(id);r.event==='error'?no(Error(JSON.stringify(r))):ok(r);});
  ws.send(JSON.stringify({event:'game.status',ticket:id}));});}
try{
  for(let i=0;i<1000;++i){
    const state=await status();observations.push(state);if(state.game)sawGame=true;
    if(sawGame&&!state.game){
      const boot=await readFile(join(session.install,'BOOT_D27_03.LOG'),'utf8');
      if(!boot.includes('benchmark-complete'))throw Error('Exited without benchmark completion marker');
      const profile=await readFile(join(session.install,'PERF.BIN'));
      if(profile.readUInt32LE(0)!==0x31465250||(profile.length-16)%108)throw Error('Incomplete benchmark profile');
      const count=(profile.length-16)/108,lastTick=profile.readUInt32LE(16+(count-1)*108);
      if(lastTick<session.replay.frames)throw Error('Profile did not reach replay target');
      const report={status:'pass',scope:'Emulator automatic replay/capture completion and HLE exit only; physical HOME and performance remain separate',records:count,lastTick,sawGame,quitBroadcast:quit,final:state,observations};
      await writeFile(join(out,'benchmark-result.json'),JSON.stringify(report,null,2)+'\n');process.exit(0);
    }
    await delay(250);
  }
  throw Error('Benchmark did not exit');
}catch(error){await writeFile(join(out,'benchmark-result.json'),JSON.stringify({status:'fail',error:String(error),observations},null,2)+'\n');console.error(error);process.exit(1);}
