// Immutable host-side snapshots produced by the read-only native sampler.
// Replaces debugger memory.read, whose official implementation pauses the core.
import {readFile} from 'node:fs/promises';
import {setTimeout as delay} from 'node:timers/promises';
export async function readNativeProbes(session,requests){
 const began=performance.now();
 while(performance.now()-began<10000){
  try{
   const b=await readFile(session.nativeProbeSnapshot),records=[];let at=0;
   while(at+8<=b.length){
    const base=b.readUInt32LE(at),length=b.readUInt32LE(at+4);at+=8;
    if(!length||length>4096||at+length>b.length)throw Error('Invalid snapshot record');
    records.push({base,data:b.subarray(at,at+length)});at+=length;
   }
   if(at!==b.length)throw Error('Incomplete snapshot record');
   const locate=(target,count)=>{const r=records.find(r=>target>=r.base&&target+count<=r.base+r.data.length);return r?.data.subarray(target-r.base,target-r.base+count);};
   const main=locate(session.address,64),data=requests.map(r=>locate(r.address,r.size));
   const sane=main&&main.readUInt32LE(0)<1000000&&main.readUInt32LE(4)<2000000&&main.readUInt32LE(12)<=256&&main.readUInt32LE(56)<=1;
   if(sane&&data.every(Boolean))return data.map(b=>({base64:b.toString('base64')}));
  }catch{}
  await delay(10);
 }
 throw Error('Native probe snapshot unavailable');
}
export async function readNativeProbe(session,address,size){return(await readNativeProbes(session,[{address,size}]))[0];}
