// Actual runtime/classes/assets; no fake Coin methods or gameplay setters.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
const R=import.meta.dirname;
const B=path.resolve(R,'../battle_logic_batch_20261114');
const packageRoot=path.join(B,'package/DoubleEdgedB154Delivery');
function run(source){
    const context=vm.createContext({nativeFloat32:Math.fround,nativeReadText:n=>fs.readFileSync(path.join(packageRoot,n),'utf8')});
    for(const file of ['src/flash_runtime.js','classes.js','visual_policy.js'])vm.runInContext(fs.readFileSync(path.join(source,file),'utf8'),context,{filename:file});
    context.movie=JSON.parse(fs.readFileSync(path.join(packageRoot,'movie.json'),'utf8'));
    vm.runInContext('FlashRuntime.seed=42;FlashRuntime.install(movie,DoubleEdgedEntry.classes);globalThis.subject=new DoubleEdged_fla.coin_spin_233();',context);
    const rows=[];
    for(let i=0;i<90;i++){
        // Exercise the loop script too; this is a component timeline test.
        if(i===30)vm.runInContext('subject.gotoAndStop(15)',context);
        vm.runInContext('FlashRuntime.tick()',context);
        rows.push(vm.runInContext('JSON.stringify({tick:FlashRuntime.frame,rng:FlashRuntime.rng,frame:subject.currentFrame,playing:subject._playing,bounds:subject.getBounds(FlashRuntime.stage)})',context));
    }
    return rows.map(JSON.parse);
}
const baseline=run(path.join(B,'source/DoubleEdgedB154'));
const candidate=run(path.join(R,'source/DoubleEdgedB155'));
for(let i=0;i<candidate.length;i++){
    const a=baseline[i],b=candidate[i];
    if(a.frame!==2||b.frame!==8||a.playing||b.playing)throw Error('Coin is not fixed on the intended source face at '+i);
    delete a.frame;delete b.frame;
    if(JSON.stringify(a)!==JSON.stringify(b))throw Error('Coin component RNG/bounds/clock changed at '+i);
}
const result={status:'passed',source_component_steps:candidate.length,baseline_frame:2,candidate_frame:8,
    actual_original_coin_scripts_preserved:true,rng_bounds_clock_and_stopped_state_exact:true,
    scope:'Actual compiled AS3 runtime component on Node,including original random frame1 and loop frame15. No native PSP pixels or performance proof.'};
fs.writeFileSync(path.join(R,'COIN_POLICY_HOST.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result));
