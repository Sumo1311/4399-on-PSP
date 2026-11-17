import {readFile,writeFile,access} from 'node:fs/promises';
import {join} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
const out=process.argv[2],session=JSON.parse(await readFile(join(out,'session.json'),'utf8'));
const ws=new WebSocket('ws://[::1]:'+session.port+'/debugger','debugger.ppsspp.org');
await new Promise((ok,no)=>{ws.addEventListener('open',ok,{once:true});ws.addEventListener('error',no,{once:true});});
let ticket=0,sawGame=false,quit=false;const pending=new Map(),observations=[];
ws.addEventListener('message',({data})=>{const r=JSON.parse(data);if(r.event==='game.quit')quit=true;pending.get(r.ticket)?.(r);});
function status(){return new Promise((ok,no)=>{const id=++ticket,timer=setTimeout(()=>no(Error('game.status timeout')),5000);pending.set(id,r=>{clearTimeout(timer);pending.delete(id);r.event==='error'?no(Error(JSON.stringify(r))):ok(r);});ws.send(JSON.stringify({event:'game.status',ticket:id}));});}
async function exists(path){try{await access(path);return true;}catch{return false;}}
try{
 for(let i=0;i<1100;++i){
  const state=await status();observations.push(state);if(state.game)sawGame=true;
  if(sawGame&&!state.game){
   const boot=await readFile(join(session.install,'BOOT_D27_03.LOG'),'utf8');if(!boot.includes('benchmark-complete'))throw Error('Exit lacks benchmark completion');
   for(const name of ['PERF.QA','TRACE.DNM','LOG.QA','FRAMES.QA','RESPONSE.QA','PERF.BIN','PRESENT60.CSV','SOURCE.BIN'])if(await exists(join(session.install,name)))throw Error('Unexpected target instrumentation: '+name);
   const report={status:'pass',scope:'Packaged native executable,deterministic replay to automatic exit,no PERF/TRACE/response flags or continuous timing writes. Pacing is read separately from RAM ring; not physical PSP acceptance.',sawGame,quitBroadcast:quit,sourceReplayFrames:session.replay.frames,final:state,observations};
   await writeFile(join(out,'ram-replay-result.json'),JSON.stringify(report,null,2)+'\n');process.exit(0);
  }
  await delay(250);
 }
 throw Error('RAM replay did not finish');
}catch(error){await writeFile(join(out,'ram-replay-result.json'),JSON.stringify({status:'fail',error:String(error),observations},null,2)+'\n');console.error(error);process.exit(1);}
