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
let failed=false;
try {
    await waitTick(3);await mark('startup-graphics');
    if(session.traceFrames && !session.replay) {
        await waitTick(session.traceFrames);await mark('engine-trace-complete');process.exit(0);
    }
    if(session.replay) {
        let previous=await state(),lastMove=performance.now(),seenEnding=false,seenRestart=false,lastCheckpoint=-1;
        const began=performance.now(),samples=[];
        while(true) {
            const s=await state(),now=performance.now();
            samples.push({seconds:now/1000,state:s});
            await appendFile(join(out,'samples.jsonl'),JSON.stringify(samples.at(-1))+'\n');
            if(s.tick!==previous.tick)lastMove=now;
            if(now-lastMove>30000)throw Error('VM has not advanced for 30 seconds');
            if(s.phase===2 && s.checkpoint!==lastCheckpoint) {
                await mark('checkpoint-'+s.checkpoint);lastCheckpoint=s.checkpoint;
            }
            if(s.phase===(session.scenario==='gameover'?5:4) && !seenEnding){seenEnding=true;await mark(session.scenario==='gameover'?'gameover':'ending');}
            if(seenEnding && s.phase===2 && !seenRestart){seenRestart=true;await mark('play-again');}
            previous=s;
            if(session.stopAt && (s.presentation?.sourceTick??s.tick)>=session.stopAt && s.rendered>=session.stopAt){
                await writeFile(join(out,'prefix-result.json'),JSON.stringify({status:'measurement_complete',scope:'Deliberate ordinary-input prefix; no ending or full-route claim',seconds:(now-began)/1000,final:s},null,2)+'\n');break;
            }
            if(s.replayDone && (s.presentation?.sourceTick??s.tick)>=session.replay.frames+1) {
                let completionEvidence='sampled-VM-phase-transitions';
                if(session.scenario==='gameover' && session.traceFrames && (!seenEnding||!seenRestart)){
                    const text=await readFile(join(session.install,'ENGINE.CSV'),'utf8');
                    const lines=text.trim().split('\n'),header=lines.shift().split(',');
                    const frameColumn=header.indexOf('root_frame');
                    if(frameColumn<0||lines.length!==session.traceFrames)throw Error('Incomplete per-source-frame state trace');
                    let death=false,restart=false;
                    for(const line of lines){const frame=Number(line.split(',')[frameColumn]);if(frame===6)death=true;else if(death&&frame===3)restart=true;}
                    seenEnding=death;seenRestart=restart;completionEvidence='complete-per-source-frame-CSV';
                }
                const report={status:seenEnding&&seenRestart?'pass':'fail',completionEvidence,scope:'Input-only VM state completion; emulation/fast-forward settings are separately recorded. No physical performance inference.',scenario:session.scenario,fixture:session.fixture,model:session.model,replay:session.replay,seenEnding,seenRestart,seconds:(now-began)/1000,final:s};
                await writeFile(join(out,'replay-result.json'),JSON.stringify(report,null,2)+'\n');
                if(report.status!=='pass')throw Error('Replay did not show ending and Play Again');
                await mark('replay-complete');break;
            }
            await delay(500);
        }
        process.exit(0);
    }
    await waitTick(240);await mark('opening');
    await call('input.buttons.send',{buttons:{right:true}});await delay(3500);
    await call('input.buttons.send',{buttons:{right:false}});await mark('walk-right');
    await call('input.buttons.send',{buttons:{cross:true,square:true}});await delay(2200);
    await call('input.buttons.send',{buttons:{cross:false,square:false}});await mark('attacks');
    await call('input.buttons.send',{buttons:{start:true}});await delay(100);await call('input.buttons.send',{buttons:{start:false}});
    await delay(600);await mark('paused');
    const pausedState=await state();await delay(600);const stillPaused=await state();
    if(stillPaused.tick!==pausedState.tick)throw Error('The logical clock advanced while paused');
    await call('input.buttons.send',{buttons:{start:true}});await delay(100);await call('input.buttons.send',{buttons:{start:false}});
    await delay(600);await mark('resumed');
    const resumedState=await state();
    if(resumedState.tick-stillPaused.tick<10)throw Error('The logical clock did not resume');
    await writeFile(join(out,'smoke-result.json'),JSON.stringify({status:'pass',scope:'Fresh PSP-2000 profile without input replay; opening, mapped buttons, native pause and resume',model:session.model,replay:session.replay,pausedTick:pausedState.tick,stillPausedTick:stillPaused.tick,resumedTick:resumedState.tick,package:session.package},null,2)+'\n');
} catch(e) {failed=true;await mark('failure').catch(()=>{});console.error(e);}
// This emulator build can fault while parsing a WebSocket close control frame.
// End the harness process after saving results; the runner then stops PPSSPP.
finally {process.exit(failed?1:0);}
